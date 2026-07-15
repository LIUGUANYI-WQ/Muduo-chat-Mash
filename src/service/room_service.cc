#include "room_service.h"

RoomResult RoomService::createRoom(const std::string& name, const std::string& creator) {
    RoomResult result;

    if (roomRepo_.roomExists(name)) {
        result.success = false;
        result.reason = "room already exists";
        return result;
    }

    int64_t room_id = roomRepo_.createRoom(name, creator);
    if (room_id <= 0) {
        result.success = false;
        result.reason = "create room failed";
        return result;
    }

    roomRepo_.addRoomMember(room_id, creator, 2);

    result.success = true;
    result.room_id = room_id;
    return result;
}

RoomResult RoomService::joinRoom(const std::string& room_name, const std::string& uid) {
    RoomResult result;

    auto room = roomRepo_.getRoomByName(room_name);
    if (!room) {
        result.success = false;
        result.reason = "room not found";
        return result;
    }

    bool ok = roomRepo_.addRoomMember(room->room_id, uid);
    result.success = ok;
    result.room_id = room->room_id;
    if (!ok) result.reason = "join room failed";

    return result;
}

bool RoomService::leaveRoom(int64_t room_id, const std::string& uid) {
    return roomRepo_.removeRoomMember(room_id, uid);
}

std::optional<RoomInfo> RoomService::getRoomByName(const std::string& name) {
    return roomRepo_.getRoomByName(name);
}

std::optional<RoomInfo> RoomService::getRoomById(int64_t room_id) {
    return roomRepo_.getRoomById(room_id);
}

bool RoomService::roomExists(const std::string& name) {
    return roomRepo_.roomExists(name);
}

std::vector<std::string> RoomService::getRoomMembers(int64_t room_id) {
    return roomRepo_.getRoomMembers(room_id);
}

std::vector<RoomInfo> RoomService::getUserRooms(const std::string& uid) {
    return roomRepo_.getUserRooms(uid);
}
