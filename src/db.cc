#include "src/db.h"
#include <cstdio>

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (MYSQL* conn : conns_) {
        if (conn) mysql_close(conn);
    }
}

bool MySQLPool::init(const std::string& host, const std::string& user,
                     const std::string& password, const std::string& database,
                     int port, int poolSize) {
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

    fprintf(stdout, "MySQL pool: %d connections to %s@%s/%s\n",
            poolSize, user.c_str(), host.c_str(), database.c_str());
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
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",

        "CREATE TABLE IF NOT EXISTS sessions ("
        "  id BIGINT AUTO_INCREMENT PRIMARY KEY,"
        "  uid VARCHAR(64) NOT NULL,"
        "  token VARCHAR(128) NOT NULL UNIQUE,"
        "  node_id VARCHAR(64) NOT NULL,"
        "  ip_address VARCHAR(45),"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP,"
        "  expired_at DATETIME NOT NULL,"
        "  revoked TINYINT DEFAULT 0,"
        "  FOREIGN KEY (uid) REFERENCES users(uid)"
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

    if (!cond_.wait_for(lock, std::chrono::seconds(1), [this] {
        return !idle_.empty();
    })) {
        fprintf(stderr, "MySQL pool: connection timeout\n");
        return nullptr;
    }

    MYSQL* conn = idle_.front();
    idle_.pop();
    activeConns_++;
    lock.unlock();

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
