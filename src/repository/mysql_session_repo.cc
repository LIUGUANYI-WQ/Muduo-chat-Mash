#include "mysql_session_repo.h"
#include <cstdio>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

bool MySQLSessionRepo::createSession(const std::string& uid, const std::string& token,
                                     const std::string& node_id, int ttlSeconds) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql =
        "INSERT INTO sessions (uid, token, node_id, expired_at) VALUES ('"
        + escape(conn, uid) + "', '" + escape(conn, token) + "', '" + escape(conn, node_id) + "', "
        "DATE_ADD(NOW(), INTERVAL " + std::to_string(ttlSeconds) + " SECOND)) ON DUPLICATE KEY UPDATE "
        "node_id = VALUES(node_id), expired_at = VALUES(expired_at), revoked = 0";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "createSession failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

std::optional<SessionInfo> MySQLSessionRepo::getSessionByToken(const std::string& token) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string sql =
        "SELECT uid, token, node_id, ip_address, created_at, expired_at, revoked "
        "FROM sessions WHERE token = '" + escape(conn, token) + "' AND revoked = 0 AND expired_at > NOW()";
    SessionInfo info;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                info.uid = row[0] ? row[0] : "";
                info.token = row[1] ? row[1] : "";
                info.node_id = row[2] ? row[2] : "";
                info.ip_address = row[3] ? row[3] : "";
                info.created_at = row[4] ? row[4] : "";
                info.expired_at = row[5] ? row[5] : "";
                info.revoked = row[6] ? atoi(row[6]) : 0;
                info.exists = true;
            }
            mysql_free_result(result);
        }
    }
    pool_.returnConnection(conn);
    return info.exists ? std::optional<SessionInfo>(info) : std::nullopt;
}

bool MySQLSessionRepo::revokeSession(const std::string& token) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE sessions SET revoked = 1 WHERE token = '" + escape(conn, token) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "revokeSession failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

bool MySQLSessionRepo::revokeAllSessions(const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE sessions SET revoked = 1 WHERE uid = '" + escape(conn, uid) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "revokeAllSessions failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

bool MySQLSessionRepo::isValidSession(const std::string& token) {
    return getSessionByToken(token).has_value();
}

bool MySQLSessionRepo::updateSessionNode(const std::string& token, const std::string& node_id) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE sessions SET node_id = '" + escape(conn, node_id) + "' WHERE token = '" + escape(conn, token) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "updateSessionNode failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}
