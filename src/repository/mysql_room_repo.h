#ifndef MYSQL_ROOM_REPO_H
#define MYSQL_ROOM_REPO_H

#include "room_repository.h"
#include "../db.h"

class MySQLRoomRepo : public RoomRepository {
public:
    MySQLRoomRepo(MySQLPool& pool) : pool_(pool) {}

    int64_t createRoom(const std::string& name, const std::string& creator) override;

    std::optional<RoomInfo> getRoomByName(const std::string& name) override;

    std::optional<RoomInfo> getRoomById(int64_t room_id) override;

    bool roomExists(const std::string& name) override;

    bool addRoomMember(int64_t room_id, const std::string& uid, int role = 0) override;

    bool removeRoomMember(int64_t room_id, const std::string& uid) override;

    std::vector<std::string> getRoomMembers(int64_t room_id) override;

    std::vector<RoomInfo> getUserRooms(const std::string& uid) override;

private:
    MySQLPool& pool_;
};

#endif
