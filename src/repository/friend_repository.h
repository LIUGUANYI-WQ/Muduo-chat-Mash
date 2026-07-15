#ifndef FRIEND_REPOSITORY_H
#define FRIEND_REPOSITORY_H

#include "../db.h"

#include <vector>

class FriendRepository {
public:
    virtual ~FriendRepository() = default;

    virtual bool addFriendRequest(const std::string& from, const std::string& to,
                                  const std::string& message = "") = 0;

    virtual bool respondFriendRequest(const std::string& requester, const std::string& responder,
                                      bool accepted) = 0;

    virtual bool removeFriends(const std::string& uid1, const std::string& uid2) = 0;

    virtual std::vector<UserInfo> getFriendList(const std::string& uid) = 0;

    virtual std::vector<std::string> getPendingRequests(const std::string& uid) = 0;
};

#endif
