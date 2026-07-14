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
    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  uid VARCHAR(64) PRIMARY KEY,"
        "  passwd VARCHAR(128) NOT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn, sql)) {
        fprintf(stderr, "Create table failed: %s\n", mysql_error(conn));
        return false;
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
