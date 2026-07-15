#ifndef ROOM_MANAGER_H
#define ROOM_MANAGER_H

#include "muduo/base/noncopyable.h"

#include <map>
#include <set>
#include <string>
#include <mutex>

class RoomManager : muduo::noncopyable {
public:
    RoomManager() {}

    bool createRoom(const std::string& name, const std::string& creator);

    bool joinRoom(const std::string& name, const std::string& uid);

    bool leaveRoom(const std::string& name, const std::string& uid);

    bool roomExists(const std::string& name) const;

    const std::set<std::string>& getRoomMembers(const std::string& name) const;

    void addRoomMember(const std::string& name, const std::string& uid);

    void removeRoomMember(const std::string& name, const std::string& uid);

    bool isInRoom(const std::string& name, const std::string& uid) const;

    size_t roomCount() const;

private:
    mutable std::mutex mutex_;
    std::map<std::string, std::set<std::string>> rooms_;
};

#endif
