#include "mysql_message_repo.h"
#include <cstdio>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

int64_t MySQLMessageRepo::storeMessage(const std::string& from, const std::string& to,
                                       const std::string& room, const std::string& content) {
    MYSQL* conn = pool_.getConnection();
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
    pool_.returnConnection(conn);
    return msg_id;
}

std::optional<MessageInfo> MySQLMessageRepo::getMessage(int64_t msg_id) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT m.msg_id, m.seq, m.from_uid, m.to_uid, "
        "COALESCE(r.name, ''), m.content, m.recalled, m.created_at "
        "FROM messages m LEFT JOIN rooms r ON m.room_id = r.room_id "
        "WHERE m.msg_id = " + std::to_string(msg_id);

    MessageInfo info;
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
    pool_.returnConnection(conn);
    return info.exists ? std::optional<MessageInfo>(info) : std::nullopt;
}

bool MySQLMessageRepo::recallMessage(int64_t msg_id, const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE messages SET recalled = 1, recall_time = NOW() "
        "WHERE msg_id = " + std::to_string(msg_id) + " AND from_uid = '" + escape(conn, uid) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "recallMessage failed: %s\n", mysql_error(conn));

    bool affected = ok && (mysql_affected_rows(conn) > 0);

    pool_.returnConnection(conn);
    return affected;
}

bool MySQLMessageRepo::addOfflineMessage(const std::string& target_uid, int64_t msg_id) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "INSERT INTO offline_messages (target_uid, msg_id) VALUES ('"
        + escape(conn, target_uid) + "', " + std::to_string(msg_id) + ")";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addOfflineMessage failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

std::vector<MessageInfo> MySQLMessageRepo::getUndeliveredMessages(const std::string& uid) {
    std::vector<MessageInfo> messages;
    MYSQL* conn = pool_.getConnection();
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
    pool_.returnConnection(conn);
    return messages;
}

bool MySQLMessageRepo::markMessagesDelivered(const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "UPDATE offline_messages SET delivered = 1 WHERE target_uid = '"
        + escape(conn, uid) + "' AND delivered = 0";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "markMessagesDelivered failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}
