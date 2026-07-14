#include "src/db.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (MYSQL* conn : conns_) {
        if (conn) mysql_close(conn);
    }
}

bool MySQLPool::init(const std::string& host, int port,
                     const std::string& user, const std::string& password,
                     const std::string& database, int poolSize) {
    info_.host = host;
    info_.port = port;
    info_.user = user;
    info_.password = password;
    info_.database = database;

    if (poolSize <= 0) {
        poolSize = std::thread::hardware_concurrency();
        if (poolSize <= 0) poolSize = 8;
    }
    maxSize_ = poolSize;

    for (int i = 0; i < poolSize; ++i) {
        MYSQL* conn = createConnection();
        if (!conn) {
            fprintf(stderr, "MySQL pool init failed at slot %d\n", i);
            return false;
        }
        conns_.push_back(conn);
        idle_.push(conn);
    }

    fprintf(stdout, "MySQL pool: %d connections to %s@%s/%s (CPU cores: %d)\n",
            poolSize, user.c_str(), host.c_str(), database.c_str(),
            std::thread::hardware_concurrency());
    return true;
}

MYSQL* MySQLPool::createConnection() {
    MYSQL* conn = mysql_init(nullptr);
    if (!conn) {
        fprintf(stderr, "mysql_init failed\n");
        return nullptr;
    }

    int timeout = 3;
    mysql_options(conn, MYSQL_OPT_CONNECT_TIMEOUT, &timeout);
    mysql_options(conn, MYSQL_OPT_READ_TIMEOUT, &timeout);
    mysql_options(conn, MYSQL_OPT_WRITE_TIMEOUT, &timeout);

    if (!mysql_real_connect(conn, info_.host.c_str(), info_.user.c_str(),
                            info_.password.c_str(), info_.database.c_str(),
                            info_.port, nullptr, 0)) {
        fprintf(stderr, "MySQL connect failed: %s\n", mysql_error(conn));
        mysql_close(conn);
        return nullptr;
    }

    mysql_set_character_set(conn, "utf8mb4");

    if (!ensureTable(conn)) {
        mysql_close(conn);
        return nullptr;
    }

    return conn;
}

bool MySQLPool::ensureTable(MYSQL* conn) {
    const char* sqls[] = {
        "CREATE TABLE IF NOT EXISTS users ("
        "  uid VARCHAR(64) PRIMARY KEY,"
        "  passwd VARCHAR(128) NOT NULL,"
        "  nickname VARCHAR(128) NOT NULL DEFAULT '',"
        "  email VARCHAR(256) NOT NULL DEFAULT '',"
        "  avatar_url TEXT,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  last_login DATETIME"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS friendships ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  requester_uid VARCHAR(64) NOT NULL,"
        "  target_uid VARCHAR(64) NOT NULL,"
        "  status TINYINT DEFAULT 0,"
        "  message VARCHAR(256) DEFAULT '',"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "  UNIQUE KEY uk_friendship (requester_uid, target_uid),"
        "  FOREIGN KEY (requester_uid) REFERENCES users(uid),"
        "  FOREIGN KEY (target_uid) REFERENCES users(uid)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS rooms ("
        "  room_id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  name VARCHAR(128) NOT NULL UNIQUE,"
        "  creator_uid VARCHAR(64) NOT NULL,"
        "  type TINYINT DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (creator_uid) REFERENCES users(uid)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS room_members ("
        "  room_id BIGINT NOT NULL,"
        "  uid VARCHAR(64) NOT NULL,"
        "  role TINYINT DEFAULT 0,"
        "  joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (room_id, uid),"
        "  FOREIGN KEY (room_id) REFERENCES rooms(room_id),"
        "  FOREIGN KEY (uid) REFERENCES users(uid)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS messages ("
        "  msg_id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  seq BIGINT NOT NULL,"
        "  room_id BIGINT,"
        "  from_uid VARCHAR(64) NOT NULL,"
        "  to_uid VARCHAR(64),"
        "  content TEXT NOT NULL,"
        "  msg_type TINYINT DEFAULT 0,"
        "  recalled TINYINT DEFAULT 0,"
        "  recall_time DATETIME,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (from_uid) REFERENCES users(uid)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS offline_messages ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  target_uid VARCHAR(64) NOT NULL,"
        "  msg_id BIGINT NOT NULL,"
        "  delivered TINYINT DEFAULT 0,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  FOREIGN KEY (target_uid) REFERENCES users(uid),"
        "  FOREIGN KEY (msg_id) REFERENCES messages(msg_id)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4"
    };

    for (auto sql : sqls) {
        if (mysql_query(conn, sql)) {
            fprintf(stderr, "Create table failed: %s\n", mysql_error(conn));
            return false;
        }
    }
    return true;
}

bool MySQLPool::pingConnection(MYSQL* conn) {
    return mysql_ping(conn) == 0;
}

MYSQL* MySQLPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);

    // 带超时等待（最多1秒）
    if (!cond_.wait_for(lock, std::chrono::seconds(1), [this] {
        return !idle_.empty();
    })) {
        fprintf(stderr, "MySQL pool: connection timeout\n");
        return nullptr;
    }

    MYSQL* conn = idle_.front();
    idle_.pop();
    activeConns_++;
    lock.unlock();  // 释放锁，ping 和重连在锁外执行，避免阻塞其他线程

    // 健康检查（锁外执行，ping 可能耗时较长）
    if (!pingConnection(conn)) {
        fprintf(stderr, "MySQL pool: stale connection detected, reconnecting\n");
        mysql_close(conn);
        conn = createConnection();
        if (!conn) {
            activeConns_--;
            return nullptr;
        }
    }

    return conn;
}

void MySQLPool::returnConnection(MYSQL* conn) {
    if (!conn) return;

    std::lock_guard<std::mutex> lock(mutex_);
    idle_.push(conn);
    activeConns_--;
    cond_.notify_one();
}

int MySQLPool::idleConns() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return idle_.size();
}

bool MySQLPool::registerUser(const std::string& uid, const std::string& passwd,
                              const std::string& nickname,
                              const std::string& email) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string checkSql = "SELECT uid FROM users WHERE uid = '" + uid + "'";
    if (mysql_query(conn, checkSql.c_str())) {
        returnConnection(conn);
        return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    bool exists = result && mysql_num_rows(result) > 0;
    if (result) mysql_free_result(result);

    if (exists) {
        returnConnection(conn);
        return false;
    }

    std::string nick = nickname.empty() ? uid : escape(conn, nickname);
    std::string mail = escape(conn, email);
    std::string escapedUid = escape(conn, uid);
    std::string sql = "INSERT INTO users (uid, nickname, email, passwd) VALUES ('"
        + escapedUid + "', '" + nick + "', '" + mail + "', SHA2('" + passwd + "', 256))";
    bool ok = (mysql_query(conn, sql.c_str()) == 0);
    if (!ok) {
        fprintf(stderr, "Register failed: %s\n", mysql_error(conn));
    }
    returnConnection(conn);
    return ok;
}

bool MySQLPool::verifyUser(const std::string& uid, const std::string& passwd) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "' AND passwd = SHA2('" + passwd + "', 256)";
    bool ok = false;

    // 重试一次
    for (int retry = 0; retry < 2; ++retry) {
        if (mysql_query(conn, sql.c_str()) == 0) {
            MYSQL_RES* result = mysql_store_result(conn);
            if (result) {
                ok = mysql_num_rows(result) > 0;
                mysql_free_result(result);
                break;
            }
        }
        // 查询失败，可能是连接断了，重试
        if (retry == 0) {
            fprintf(stderr, "MySQL query failed, retrying: %s\n", mysql_error(conn));
            mysql_close(conn);
            conn = createConnection();
            if (!conn) return false;
        }
    }

    // 验证成功，更新 last_login
    if (ok) {
        std::string updateSql = "UPDATE users SET last_login = NOW() WHERE uid = '" + uid + "'";
        mysql_query(conn, updateSql.c_str());
    }

    returnConnection(conn);
    return ok;
}

bool MySQLPool::userExists(const std::string& uid) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "'";
    bool exists = false;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            exists = mysql_num_rows(result) > 0;
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return exists;
}

UserInfo MySQLPool::getUserInfo(const std::string& uid) {
    UserInfo info;
    MYSQL* conn = getConnection();
    if (!conn) return info;

    std::string sql = "SELECT uid, nickname, email, avatar_url FROM users WHERE uid = '" + uid + "'";
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                info.uid = row[0] ? row[0] : "";
                info.nickname = row[1] ? row[1] : "";
                info.email = row[2] ? row[2] : "";
                info.avatarUrl = row[3] ? row[3] : "";
                info.exists = true;
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return info;
}

bool MySQLPool::addFriendRequest(const std::string& from, const std::string& to,
                                  const std::string& message) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "INSERT IGNORE INTO friendships (requester_uid, target_uid, status, message) VALUES ('"
        + escape(conn, from) + "', '" + escape(conn, to) + "', 0, '" + escape(conn, message) + "')";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addFriendRequest failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}

bool MySQLPool::respondFriendRequest(const std::string& requester, const std::string& responder,
                                      bool accepted) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    int status = accepted ? 1 : 2;
    std::string sql = "UPDATE friendships SET status = " + std::to_string(status)
        + " WHERE requester_uid = '" + escape(conn, requester) + "' AND target_uid = '" + escape(conn, responder) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) {
        fprintf(stderr, "respondFriendRequest failed: %s\n", mysql_error(conn));
        returnConnection(conn);
        return false;
    }

    if (accepted) {
        std::string insertSql = "INSERT IGNORE INTO friendships (requester_uid, target_uid, status) VALUES ('"
            + escape(conn, responder) + "', '" + escape(conn, requester) + "', 1)";
        mysql_query(conn, insertSql.c_str());
    }

    returnConnection(conn);
    return true;
}

bool MySQLPool::removeFriends(const std::string& uid1, const std::string& uid2) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "DELETE FROM friendships WHERE (requester_uid = '" + escape(conn, uid1)
        + "' AND target_uid = '" + escape(conn, uid2) + "')"
        + " OR (requester_uid = '" + escape(conn, uid2) + "' AND target_uid = '" + escape(conn, uid1) + "')";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "removeFriends failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}

std::vector<UserInfo> MySQLPool::getFriendList(const std::string& uid) {
    std::vector<UserInfo> friends;
    MYSQL* conn = getConnection();
    if (!conn) return friends;

    // 查所有 status=1 的双向好友
    std::string sql =
        "SELECT u.uid, u.nickname, u.email, u.avatar_url FROM users u "
        "JOIN friendships f ON (f.requester_uid = '" + escape(conn, uid) + "' AND f.target_uid = u.uid)"
        " WHERE f.status = 1"
        " UNION "
        "SELECT u.uid, u.nickname, u.email, u.avatar_url FROM users u "
        "JOIN friendships f ON (f.target_uid = '" + escape(conn, uid) + "' AND f.requester_uid = u.uid)"
        " WHERE f.status = 1";
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                UserInfo info;
                info.uid = row[0] ? row[0] : "";
                info.nickname = row[1] ? row[1] : "";
                info.email = row[2] ? row[2] : "";
                info.avatarUrl = row[3] ? row[3] : "";
                info.exists = true;
                friends.push_back(info);
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return friends;
}

std::vector<std::string> MySQLPool::getPendingRequests(const std::string& uid) {
    std::vector<std::string> requesters;
    MYSQL* conn = getConnection();
    if (!conn) return requesters;

    std::string sql = "SELECT requester_uid FROM friendships WHERE target_uid = '"
        + escape(conn, uid) + "' AND status = 0";
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                if (row[0]) requesters.push_back(row[0]);
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return requesters;
}

// ─── 消息系统 ───

int64_t MySQLPool::storeMessage(const std::string& from, const std::string& to,
                                 const std::string& room, const std::string& content) {
    MYSQL* conn = getConnection();
    if (!conn) return 0;

    int64_t roomId = 0;
    if (!room.empty()) {
        std::string sql = "SELECT room_id FROM rooms WHERE name = '" + escape(conn, room) + "'";
        if (mysql_query(conn, sql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) roomId = atoll(row[0]);
                mysql_free_result(res);
            }
        }
    }

    int64_t seq = 1;
    if (roomId > 0) {
        std::string seqSql = "SELECT COALESCE(MAX(seq), 0) + 1 FROM messages WHERE room_id = " + std::to_string(roomId);
        if (mysql_query(conn, seqSql.c_str()) == 0) {
            MYSQL_RES* res = mysql_store_result(conn);
            if (res) {
                MYSQL_ROW row = mysql_fetch_row(res);
                if (row && row[0]) seq = atoll(row[0]);
                mysql_free_result(res);
            }
        }
    }

    std::string escapedFrom = escape(conn, from);
    std::string escapedTo = escape(conn, to);
    std::string escapedContent = escape(conn, content);

    std::string roomIdStr = roomId > 0 ? std::to_string(roomId) : "NULL";
    std::string toStr = to.empty() ? "NULL" : ("'" + escapedTo + "'");

    std::string sql = "INSERT INTO messages (seq, room_id, from_uid, to_uid, content) VALUES ("
        + std::to_string(seq) + ", " + roomIdStr + ", '"
        + escapedFrom + "', " + toStr + ", '" + escapedContent + "')";

    int64_t msg_id = 0;
    if (mysql_query(conn, sql.c_str()) == 0) {
        msg_id = (int64_t)mysql_insert_id(conn);
    } else {
        fprintf(stderr, "storeMessage failed: %s\n", mysql_error(conn));
    }
    returnConnection(conn);
    return msg_id;
}

MessageInfo MySQLPool::getMessage(int64_t msg_id) {
    MessageInfo info;
    MYSQL* conn = getConnection();
    if (!conn) return info;

    std::string sql = "SELECT m.msg_id, m.seq, m.from_uid, m.to_uid, "
        "COALESCE(r.name, ''), m.content, m.recalled, m.created_at "
        "FROM messages m LEFT JOIN rooms r ON m.room_id = r.room_id "
        "WHERE m.msg_id = " + std::to_string(msg_id);

    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                info.msg_id = row[0] ? atoll(row[0]) : 0;
                info.seq = row[1] ? atoll(row[1]) : 0;
                info.from_uid = row[2] ? row[2] : "";
                info.to_uid = row[3] ? row[3] : "";
                info.room_name = row[4] ? row[4] : "";
                info.content = row[5] ? row[5] : "";
                info.recalled = row[6] ? atoi(row[6]) : 0;
                info.created_at = row[7] ? row[7] : "";
                info.exists = true;
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return info;
}

bool MySQLPool::recallMessage(int64_t msg_id, const std::string& uid) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE messages SET recalled = 1, recall_time = NOW() "
        "WHERE msg_id = " + std::to_string(msg_id) + " AND from_uid = '" + escape(conn, uid) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "recallMessage failed: %s\n", mysql_error(conn));

    bool affected = ok && (mysql_affected_rows(conn) > 0);

    returnConnection(conn);
    return affected;
}

// ─── 房间系统 ───

int64_t MySQLPool::createRoom(const std::string& name, const std::string& creator) {
    MYSQL* conn = getConnection();
    if (!conn) return 0;

    std::string sql = "INSERT INTO rooms (name, creator_uid) VALUES ('"
        + escape(conn, name) + "', '" + escape(conn, creator) + "')";
    int64_t room_id = 0;
    if (mysql_query(conn, sql.c_str()) == 0) {
        room_id = (int64_t)mysql_insert_id(conn);
    } else {
        fprintf(stderr, "createRoom failed: %s\n", mysql_error(conn));
    }
    returnConnection(conn);
    return room_id;
}

int64_t MySQLPool::getRoomIdByName(const std::string& name) {
    MYSQL* conn = getConnection();
    if (!conn) return 0;

    std::string sql = "SELECT room_id FROM rooms WHERE name = '" + escape(conn, name) + "'";
    int64_t room_id = 0;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row && row[0]) room_id = atoll(row[0]);
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return room_id;
}

bool MySQLPool::roomExists(const std::string& name) {
    return getRoomIdByName(name) > 0;
}

bool MySQLPool::addRoomMember(int64_t room_id, const std::string& uid) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "INSERT IGNORE INTO room_members (room_id, uid) VALUES ("
        + std::to_string(room_id) + ", '" + escape(conn, uid) + "')";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addRoomMember failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}

bool MySQLPool::removeRoomMember(int64_t room_id, const std::string& uid) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "DELETE FROM room_members WHERE room_id = "
        + std::to_string(room_id) + " AND uid = '" + escape(conn, uid) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "removeRoomMember failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}

std::vector<std::string> MySQLPool::getRoomMembers(int64_t room_id) {
    std::vector<std::string> members;
    MYSQL* conn = getConnection();
    if (!conn) return members;

    std::string sql = "SELECT uid FROM room_members WHERE room_id = " + std::to_string(room_id);
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                if (row[0]) members.push_back(row[0]);
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return members;
}

// ─── 离线消息 ───

bool MySQLPool::addOfflineMessage(const std::string& target_uid, int64_t msg_id) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "INSERT INTO offline_messages (target_uid, msg_id) VALUES ('"
        + escape(conn, target_uid) + "', " + std::to_string(msg_id) + ")";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addOfflineMessage failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}

std::vector<MessageInfo> MySQLPool::getUndeliveredMessages(const std::string& uid) {
    std::vector<MessageInfo> messages;
    MYSQL* conn = getConnection();
    if (!conn) return messages;

    std::string sql =
        "SELECT m.msg_id, m.seq, m.from_uid, m.to_uid, COALESCE(r.name, ''), "
        "m.content, m.recalled, m.created_at "
        "FROM offline_messages o "
        "JOIN messages m ON o.msg_id = m.msg_id "
        "LEFT JOIN rooms r ON m.room_id = r.room_id "
        "WHERE o.target_uid = '" + escape(conn, uid) + "' AND o.delivered = 0 "
        "ORDER BY o.id ASC";

    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                MessageInfo info;
                info.msg_id = row[0] ? atoll(row[0]) : 0;
                info.seq = row[1] ? atoll(row[1]) : 0;
                info.from_uid = row[2] ? row[2] : "";
                info.to_uid = row[3] ? row[3] : "";
                info.room_name = row[4] ? row[4] : "";
                info.content = row[5] ? row[5] : "";
                info.recalled = row[6] ? atoi(row[6]) : 0;
                info.created_at = row[7] ? row[7] : "";
                info.exists = true;
                messages.push_back(info);
            }
            mysql_free_result(result);
        }
    }
    returnConnection(conn);
    return messages;
}

bool MySQLPool::markMessagesDelivered(const std::string& uid) {
    MYSQL* conn = getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE offline_messages SET delivered = 1 WHERE target_uid = '"
        + escape(conn, uid) + "' AND delivered = 0";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "markMessagesDelivered failed: %s\n", mysql_error(conn));
    returnConnection(conn);
    return ok;
}
