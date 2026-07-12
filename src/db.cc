#include "src/db.h"
#include <cstdio>
#include <cstdlib>

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
    lock.unlock();

    // 健康检查
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

bool MySQLPool::registerUser(const std::string& uid, const std::string& passwd) {
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

    std::string sql = "INSERT INTO users (uid, passwd) VALUES ('" + uid + "', SHA2('" + passwd + "', 256))";
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
