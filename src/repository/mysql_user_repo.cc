#include "mysql_user_repo.h"
#include <cstdio>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

bool MySQLUserRepo::registerUser(const std::string& uid, const std::string& passwd,
                                 const std::string& nickname, const std::string& email) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string checkSql = "SELECT uid FROM users WHERE uid = '" + uid + "'";
    if (mysql_query(conn, checkSql.c_str())) {
        pool_.returnConnection(conn);
        return false;
    }
    MYSQL_RES* result = mysql_store_result(conn);
    bool exists = result && mysql_num_rows(result) > 0;
    if (result) mysql_free_result(result);

    if (exists) {
        pool_.returnConnection(conn);
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
    pool_.returnConnection(conn);
    return ok;
}

bool MySQLUserRepo::verifyUser(const std::string& uid, const std::string& passwd) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "SELECT uid FROM users WHERE uid = '" + uid + "' AND passwd = SHA2('" + passwd + "', 256)";
    bool ok = false;

    for (int retry = 0; retry < 2; ++retry) {
        if (mysql_query(conn, sql.c_str()) == 0) {
            MYSQL_RES* result = mysql_store_result(conn);
            if (result) {
                ok = mysql_num_rows(result) > 0;
                mysql_free_result(result);
                break;
            }
        }
        if (retry == 0) {
            fprintf(stderr, "MySQL query failed, retrying: %s\n", mysql_error(conn));
            mysql_close(conn);
            conn = pool_.getConnection();
            if (!conn) return false;
        }
    }

    if (ok) {
        std::string updateSql = "UPDATE users SET last_login = NOW() WHERE uid = '" + uid + "'";
        mysql_query(conn, updateSql.c_str());
    }

    pool_.returnConnection(conn);
    return ok;
}

bool MySQLUserRepo::userExists(const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
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
    pool_.returnConnection(conn);
    return exists;
}

std::optional<UserInfo> MySQLUserRepo::getUserInfo(const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT uid, nickname, email, avatar_url FROM users WHERE uid = '" + uid + "'";
    UserInfo info;
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
    pool_.returnConnection(conn);
    return info.exists ? std::optional<UserInfo>(info) : std::nullopt;
}

bool MySQLUserRepo::updateLastLogin(const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE users SET last_login = NOW() WHERE uid = '" + uid + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    pool_.returnConnection(conn);
    return ok;
}
