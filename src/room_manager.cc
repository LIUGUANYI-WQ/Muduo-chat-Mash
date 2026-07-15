#include "room_manager.h"

bool RoomManager::createRoom(const std::string& name, const std::string& creator) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (rooms_.find(name) != rooms_.end())
        return false;

    rooms_[name].insert(creator);
    return true;
}

bool RoomManager::joinRoom(const std::string& name, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(name);
    if (it == rooms_.end())
        return false;

    it->second.insert(uid);
    return true;
}

bool RoomManager::leaveRoom(const std::string& name, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(name);
    if (it == rooms_.end())
        return false;

    it->second.erase(uid);
    return true;
}

bool RoomManager::roomExists(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.find(name) != rooms_.end();
}

const std::set<std::string>& RoomManager::getRoomMembers(const std::string& name) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(name);
    static const std::set<std::string> empty;
    return it != rooms_.end() ? it->second : empty;
}

void RoomManager::addRoomMember(const std::string& name, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    rooms_[name].insert(uid);
}

void RoomManager::removeRoomMember(const std::string& name, const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(name);
    if (it != rooms_.end()) {
        it->second.erase(uid);
    }
}

bool RoomManager::isInRoom(const std::string& name, const std::string& uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = rooms_.find(name);
    if (it == rooms_.end())
        return false;
    return it->second.count(uid) > 0;
}

size_t RoomManager::roomCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return rooms_.size();
}
