#ifndef LOGIN_SERVICE_H
#define LOGIN_SERVICE_H

#include "../repository/user_repository.h"
#include "../repository/session_repository.h"
#include "../repository/friend_repository.h"
#include "../repository/message_repository.h"
#include "../repository/room_repository.h"

#include <string>
#include <optional>
#include <vector>

struct LoginResult {
    bool success = false;
    std::string token;
    UserInfo userInfo;
    std::vector<UserInfo> friends;
    std::vector<std::string> pendingRequests;
    std::vector<MessageInfo> offlineMessages;
    std::string reason;
};

class LoginService {
public:
    LoginService(UserRepository& userRepo, SessionRepository& sessionRepo,
                 FriendRepository& friendRepo, MessageRepository& msgRepo)
        : userRepo_(userRepo), sessionRepo_(sessionRepo),
          friendRepo_(friendRepo), msgRepo_(msgRepo) {}

    LoginResult login(const std::string& uid, const std::string& passwd,
                      const std::string& node_id);

    LoginResult loginWithToken(const std::string& token, const std::string& node_id);

    bool registerUser(const std::string& uid, const std::string& passwd,
                      const std::string& nickname = "", const std::string& email = "");

    bool logout(const std::string& uid, const std::string& token);

private:
    std::string generateToken();

    UserRepository& userRepo_;
    SessionRepository& sessionRepo_;
    FriendRepository& friendRepo_;
    MessageRepository& msgRepo_;
};

#endif
