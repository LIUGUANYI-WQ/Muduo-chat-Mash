#include "friend_service.h"

FriendResult FriendService::addFriendRequest(const std::string& from, const std::string& to,
                                             const std::string& message) {
    FriendResult result;

    if (!userRepo_.userExists(to)) {
        result.success = false;
        result.reason = "target user not found";
        return result;
    }

    bool ok = friendRepo_.addFriendRequest(from, to, message);
    result.success = ok;
    if (!ok) result.reason = "add friend request failed";

    return result;
}

FriendResult FriendService::respondFriendRequest(const std::string& requester,
                                                 const std::string& responder, bool accepted) {
    FriendResult result;

    bool ok = friendRepo_.respondFriendRequest(requester, responder, accepted);
    result.success = ok;
    if (!ok) result.reason = "respond friend request failed";

    return result;
}

FriendResult FriendService::removeFriend(const std::string& uid1, const std::string& uid2) {
    FriendResult result;

    bool ok = friendRepo_.removeFriends(uid1, uid2);
    result.success = ok;
    if (!ok) result.reason = "remove friend failed";

    return result;
}

std::vector<UserInfo> FriendService::getFriendList(const std::string& uid) {
    return friendRepo_.getFriendList(uid);
}

std::vector<std::string> FriendService::getPendingRequests(const std::string& uid) {
    return friendRepo_.getPendingRequests(uid);
}
