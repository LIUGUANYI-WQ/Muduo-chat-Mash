#include "mysql_friend_repo.h"
#include <cstdio>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

bool MySQLFriendRepo::addFriendRequest(const std::string& from, const std::string& to,
                                       const std::string& message) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "INSERT IGNORE INTO friendships (requester_uid, target_uid, status, message) VALUES ('"
        + escape(conn, from) + "', '" + escape(conn, to) + "', 0, '" + escape(conn, message) + "')";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addFriendRequest failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

bool MySQLFriendRepo::respondFriendRequest(const std::string& requester, const std::string& responder,
                                           bool accepted) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    int status = accepted ? 1 : 2;
    std::string sql = "UPDATE friendships SET status = " + std::to_string(status)
        + " WHERE requester_uid = '" + escape(conn, requester) + "' AND target_uid = '" + escape(conn, responder) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) {
        fprintf(stderr, "respondFriendRequest failed: %s\n", mysql_error(conn));
        pool_.returnConnection(conn);
        return false;
    }

    if (accepted) {
        std::string insertSql = "INSERT IGNORE INTO friendships (requester_uid, target_uid, status) VALUES ('"
            + escape(conn, responder) + "', '" + escape(conn, requester) + "', 1)";
        mysql_query(conn, insertSql.c_str());
    }

    pool_.returnConnection(conn);
    return true;
}

bool MySQLFriendRepo::removeFriends(const std::string& uid1, const std::string& uid2) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "DELETE FROM friendships WHERE (requester_uid = '" + escape(conn, uid1)
        + "' AND target_uid = '" + escape(conn, uid2) + "')"
        + " OR (requester_uid = '" + escape(conn, uid2) + "' AND target_uid = '" + escape(conn, uid1) + "')";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "removeFriends failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

std::vector<UserInfo> MySQLFriendRepo::getFriendList(const std::string& uid) {
    std::vector<UserInfo> friends;
    MYSQL* conn = pool_.getConnection();
    if (!conn) return friends;

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
    pool_.returnConnection(conn);
    return friends;
}

std::vector<std::string> MySQLFriendRepo::getPendingRequests(const std::string& uid) {
    std::vector<std::string> requesters;
    MYSQL* conn = pool_.getConnection();
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
    pool_.returnConnection(conn);
    return requesters;
}
