#ifndef FRIEND_SERVICE_H
#define FRIEND_SERVICE_H

#include "../repository/friend_repository.h"
#include "../repository/user_repository.h"
#include "../repository/redis_pubsub.h"

#include <string>
#include <vector>

struct FriendResult {
    bool success = false;
    std::string reason;
};

class FriendService {
public:
    FriendService(FriendRepository& friendRepo, UserRepository& userRepo,
                  RedisPubSub* pubsub = nullptr)
        : friendRepo_(friendRepo), userRepo_(userRepo), pubsub_(pubsub) {}

    FriendResult addFriendRequest(const std::string& from, const std::string& to,
                                  const std::string& message = "");

    FriendResult respondFriendRequest(const std::string& requester, const std::string& responder,
                                      bool accepted);

    FriendResult removeFriend(const std::string& uid1, const std::string& uid2);

    std::vector<UserInfo> getFriendList(const std::string& uid);

    std::vector<std::string> getPendingRequests(const std::string& uid);

private:
    FriendRepository& friendRepo_;
    UserRepository& userRepo_;
    RedisPubSub* pubsub_;
};

#endif
