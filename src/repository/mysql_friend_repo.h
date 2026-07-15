#ifndef MYSQL_FRIEND_REPO_H
#define MYSQL_FRIEND_REPO_H

#include "friend_repository.h"
#include "../db.h"

class MySQLFriendRepo : public FriendRepository {
public:
    MySQLFriendRepo(MySQLPool& pool) : pool_(pool) {}

    bool addFriendRequest(const std::string& from, const std::string& to,
                          const std::string& message = "") override;

    bool respondFriendRequest(const std::string& requester, const std::string& responder,
                              bool accepted) override;

    bool removeFriends(const std::string& uid1, const std::string& uid2) override;

    std::vector<UserInfo> getFriendList(const std::string& uid) override;

    std::vector<std::string> getPendingRequests(const std::string& uid) override;

private:
    MySQLPool& pool_;
};

#endif
