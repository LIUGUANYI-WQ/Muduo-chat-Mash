#ifndef ROOM_SERVICE_H
#define ROOM_SERVICE_H

#include "../repository/room_repository.h"
#include "../repository/user_repository.h"
#include "../repository/redis_pubsub.h"

#include <string>
#include <optional>
#include <vector>

struct RoomResult {
    bool success = false;
    int64_t room_id = 0;
    std::string reason;
};

class RoomService {
public:
    RoomService(RoomRepository& roomRepo, UserRepository& userRepo,
                RedisPubSub* pubsub = nullptr)
        : roomRepo_(roomRepo), userRepo_(userRepo), pubsub_(pubsub) {}

    RoomResult createRoom(const std::string& name, const std::string& creator);

    RoomResult joinRoom(const std::string& room_name, const std::string& uid);

    bool leaveRoom(int64_t room_id, const std::string& uid);

    std::optional<RoomInfo> getRoomByName(const std::string& name);

    std::optional<RoomInfo> getRoomById(int64_t room_id);

    bool roomExists(const std::string& name);

    std::vector<std::string> getRoomMembers(int64_t room_id);

    std::vector<RoomInfo> getUserRooms(const std::string& uid);

private:
    RoomRepository& roomRepo_;
    UserRepository& userRepo_;
    RedisPubSub* pubsub_;
};

#endif
