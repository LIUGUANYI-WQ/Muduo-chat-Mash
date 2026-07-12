#include "src/db.h"
#include <cstdio>

MySQLPool::~MySQLPool() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (MYSQL* conn : conns_) {
        if (conn) mysql_close(conn);
    }
}

bool MySQLPool::init(const std::string& host, int port,
                     const std::string& user, const std::string& password,
                     const std::string& database, int poolSize) {
    host_ = host;
    port_ = port;
    user_ = user;
    password_ = password;
    database_ = database;

    for (int i = 0; i < poolSize; ++i) {
        MYSQL* conn = mysql_init(nullptr);
        if (!conn) {
            fprintf(stderr, "mysql_init failed for pool slot %d\n", i);
            return false;
        }

        if (!mysql_real_connect(conn, host.c_str(), user.c_str(),
                                password.c_str(), database.c_str(),
                                port, nullptr, 0)) {
            fprintf(stderr, "MySQL connect failed: %s\n", mysql_error(conn));
            mysql_close(conn);
            return false;
        }

        if (!ensureTable(conn)) {
            mysql_close(conn);
            return false;
        }

        conns_.push_back(conn);
        idle_.push(conn);
    }

    fprintf(stdout, "MySQL pool connected: %d connections to %s@%s/%s\n",
            poolSize, user.c_str(), host.c_str(), database.c_str());
    return true;
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

MYSQL* MySQLPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return !idle_.empty(); });
    MYSQL* conn = idle_.front();
    idle_.pop();
    return conn;
}

void MySQLPool::returnConnection(MYSQL* conn) {
    std::lock_guard<std::mutex> lock(mutex_);
    idle_.push(conn);
    cond_.notify_one();
}

bool MySQLPool::registerUser(const std::string& uid, const std::string& passwd) {
    MYSQL* conn = getConnection();

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

    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "' AND passwd = SHA2('" + passwd + "', 256)";
    bool ok = false;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            ok = mysql_num_rows(result) > 0;
            mysql_free_result(result);
        }
    } else {
        fprintf(stderr, "Verify query failed: %s\n", mysql_error(conn));
    }
    returnConnection(conn);
    return ok;
}

bool MySQLPool::userExists(const std::string& uid) {
    MYSQL* conn = getConnection();

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
