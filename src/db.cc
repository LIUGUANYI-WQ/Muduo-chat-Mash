#include "src/db.h"
#include <cstdio>
#include <mutex>

MySQL::MySQL() : conn_(nullptr) {}

MySQL::~MySQL() {
    if (conn_) {
        mysql_close(conn_);
    }
}

bool MySQL::init(const std::string& host, const std::string& user,
                 const std::string& password, const std::string& database) {
    conn_ = mysql_init(nullptr);
    if (!conn_) {
        fprintf(stderr, "mysql_init failed\n");
        return false;
    }

    if (!mysql_real_connect(conn_, host.c_str(), user.c_str(),
                            password.c_str(), database.c_str(),
                            3306, nullptr, 0)) {
        fprintf(stderr, "MySQL connect failed: %s\n", mysql_error(conn_));
        mysql_close(conn_);
        conn_ = nullptr;
        return false;
    }

    fprintf(stdout, "MySQL connected to %s@%s/%s\n",
            user.c_str(), host.c_str(), database.c_str());

    return ensureTable();
}

bool MySQL::ensureTable() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS users ("
        "  uid VARCHAR(64) PRIMARY KEY,"
        "  passwd VARCHAR(128) NOT NULL,"
        "  created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4";

    if (mysql_query(conn_, sql)) {
        fprintf(stderr, "Create table failed: %s\n", mysql_error(conn_));
        return false;
    }
    return true;
}

bool MySQL::registerUser(const std::string& uid, const std::string& passwd) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (userExists(uid)) {
        return false;
    }

    std::string sql = "INSERT INTO users (uid, passwd) VALUES ('" + uid + "', SHA2('" + passwd + "', 256))";

    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "Register failed: %s\n", mysql_error(conn_));
        return false;
    }
    return true;
}

bool MySQL::verifyUser(const std::string& uid, const std::string& passwd) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "' AND passwd = SHA2('" + passwd + "', 256)";

    if (mysql_query(conn_, sql.c_str())) {
        fprintf(stderr, "Verify query failed: %s\n", mysql_error(conn_));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(conn_);
    if (!result) {
        fprintf(stderr, "Store result failed: %s\n", mysql_error(conn_));
        return false;
    }

    bool ok = mysql_num_rows(result) > 0;
    mysql_free_result(result);
    return ok;
}

bool MySQL::userExists(const std::string& uid) {
    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "'";

    if (mysql_query(conn_, sql.c_str())) {
        return false;
    }

    MYSQL_RES* result = mysql_store_result(conn_);
    if (!result) {
        return false;
    }

    bool exists = mysql_num_rows(result) > 0;
    mysql_free_result(result);
    return exists;
}
