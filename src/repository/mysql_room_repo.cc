#include "mysql_room_repo.h"
#include <cstdio>

static std::string escape(MYSQL* conn, const std::string& s) {
    if (s.empty()) return "";
    std::string out(s.size() * 2 + 1, '\0');
    unsigned long len = mysql_real_escape_string(conn, &out[0], s.data(), s.size());
    out.resize(len);
    return out;
}

int64_t MySQLRoomRepo::createRoom(const std::string& name, const std::string& creator) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return 0;

    std::string sql = "INSERT INTO rooms (name, creator_uid) VALUES ('"
        + escape(conn, name) + "', '" + escape(conn, creator) + "')";
    int64_t room_id = 0;
    if (mysql_query(conn, sql.c_str()) == 0) {
        room_id = (int64_t)mysql_insert_id(conn);
    } else {
        fprintf(stderr, "createRoom failed: %s\n", mysql_error(conn));
    }
    pool_.returnConnection(conn);
    return room_id;
}

std::optional<RoomInfo> MySQLRoomRepo::getRoomByName(const std::string& name) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT room_id, name, creator_uid, type FROM rooms WHERE name = '" + escape(conn, name) + "'";
    RoomInfo info;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                info.room_id = row[0] ? atoll(row[0]) : 0;
                info.name = row[1] ? row[1] : "";
                info.creator_uid = row[2] ? row[2] : "";
                info.type = row[3] ? atoi(row[3]) : 0;
                info.exists = true;
            }
            mysql_free_result(result);
        }
    }
    pool_.returnConnection(conn);
    return info.exists ? std::optional<RoomInfo>(info) : std::nullopt;
}

std::optional<RoomInfo> MySQLRoomRepo::getRoomById(int64_t room_id) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string sql = "SELECT room_id, name, creator_uid, type FROM rooms WHERE room_id = " + std::to_string(room_id);
    RoomInfo info;
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row = mysql_fetch_row(result);
            if (row) {
                info.room_id = row[0] ? atoll(row[0]) : 0;
                info.name = row[1] ? row[1] : "";
                info.creator_uid = row[2] ? row[2] : "";
                info.type = row[3] ? atoi(row[3]) : 0;
                info.exists = true;
            }
            mysql_free_result(result);
        }
    }
    pool_.returnConnection(conn);
    return info.exists ? std::optional<RoomInfo>(info) : std::nullopt;
}

bool MySQLRoomRepo::roomExists(const std::string& name) {
    return getRoomByName(name).has_value();
}

bool MySQLRoomRepo::addRoomMember(int64_t room_id, const std::string& uid, int role) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "INSERT IGNORE INTO room_members (room_id, uid, role) VALUES ("
        + std::to_string(room_id) + ", '" + escape(conn, uid) + "', " + std::to_string(role) + ")";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "addRoomMember failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

bool MySQLRoomRepo::removeRoomMember(int64_t room_id, const std::string& uid) {
    MYSQL* conn = pool_.getConnection();
    if (!conn) return false;

    std::string sql = "DELETE FROM room_members WHERE room_id = "
        + std::to_string(room_id) + " AND uid = '" + escape(conn, uid) + "'";
    bool ok = mysql_query(conn, sql.c_str()) == 0;
    if (!ok) fprintf(stderr, "removeRoomMember failed: %s\n", mysql_error(conn));
    pool_.returnConnection(conn);
    return ok;
}

std::vector<std::string> MySQLRoomRepo::getRoomMembers(int64_t room_id) {
    std::vector<std::string> members;
    MYSQL* conn = pool_.getConnection();
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
    pool_.returnConnection(conn);
    return members;
}

std::vector<RoomInfo> MySQLRoomRepo::getUserRooms(const std::string& uid) {
    std::vector<RoomInfo> rooms;
    MYSQL* conn = pool_.getConnection();
    if (!conn) return rooms;

    std::string sql =
        "SELECT r.room_id, r.name, r.creator_uid, r.type FROM rooms r "
        "JOIN room_members rm ON r.room_id = rm.room_id "
        "WHERE rm.uid = '" + escape(conn, uid) + "'";
    if (mysql_query(conn, sql.c_str()) == 0) {
        MYSQL_RES* result = mysql_store_result(conn);
        if (result) {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(result))) {
                RoomInfo info;
                info.room_id = row[0] ? atoll(row[0]) : 0;
                info.name = row[1] ? row[1] : "";
                info.creator_uid = row[2] ? row[2] : "";
                info.type = row[3] ? atoi(row[3]) : 0;
                info.exists = true;
                rooms.push_back(info);
            }
            mysql_free_result(result);
        }
    }
    pool_.returnConnection(conn);
    return rooms;
}
