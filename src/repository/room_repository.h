#ifndef ROOM_REPOSITORY_H
#define ROOM_REPOSITORY_H

#include <string>
#include <vector>
#include <optional>

struct RoomInfo {
    int64_t room_id = 0;
    std::string name;
    std::string creator_uid;
    int type = 0;
    bool exists = false;
};

struct RoomMember {
    std::string uid;
    int role = 0;
};

class RoomRepository {
public:
    virtual ~RoomRepository() = default;

    virtual int64_t createRoom(const std::string& name, const std::string& creator) = 0;

    virtual std::optional<RoomInfo> getRoomByName(const std::string& name) = 0;

    virtual std::optional<RoomInfo> getRoomById(int64_t room_id) = 0;

    virtual bool roomExists(const std::string& name) = 0;

    virtual bool addRoomMember(int64_t room_id, const std::string& uid, int role = 0) = 0;

    virtual bool removeRoomMember(int64_t room_id, const std::string& uid) = 0;

    virtual std::vector<std::string> getRoomMembers(int64_t room_id) = 0;

    virtual std::vector<RoomInfo> getUserRooms(const std::string& uid) = 0;
};

#endif
