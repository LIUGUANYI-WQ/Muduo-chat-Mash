#include "login_service.h"
#include <random>

std::string LoginService::generateToken() {
    static thread_local std::mt19937 gen(std::random_device{}());
    static const char hex[] = "0123456789abcdef";
    std::uniform_int_distribution<> dis(0, 15);
    std::string token(64, ' ');
    for (int i = 0; i < 64; ++i)
        token[i] = hex[dis(gen)];
    return token;
}

LoginResult LoginService::login(const std::string& uid, const std::string& passwd,
                                const std::string& node_id) {
    LoginResult result;

    if (!userRepo_.verifyUser(uid, passwd)) {
        result.success = false;
        result.reason = "invalid uid or password";
        return result;
    }

    auto userInfo = userRepo_.getUserInfo(uid);
    if (!userInfo) {
        result.success = false;
        result.reason = "user not found";
        return result;
    }

    std::string token = generateToken();
    bool sessionOk = sessionRepo_.createSession(uid, token, node_id, 7 * 24 * 3600);
    if (!sessionOk) {
        result.success = false;
        result.reason = "session create failed";
        return result;
    }

    result.success = true;
    result.token = token;
    result.userInfo = *userInfo;
    result.friends = friendRepo_.getFriendList(uid);
    result.pendingRequests = friendRepo_.getPendingRequests(uid);
    result.offlineMessages = msgRepo_.getUndeliveredMessages(uid);

    return result;
}

LoginResult LoginService::loginWithToken(const std::string& token, const std::string& node_id) {
    LoginResult result;

    auto session = sessionRepo_.getSessionByToken(token);
    if (!session) {
        result.success = false;
        result.reason = "invalid token";
        return result;
    }

    std::string uid = session->uid;
    sessionRepo_.updateSessionNode(token, node_id);

    auto userInfo = userRepo_.getUserInfo(uid);
    if (!userInfo) {
        result.success = false;
        result.reason = "user not found";
        return result;
    }

    result.success = true;
    result.token = token;
    result.userInfo = *userInfo;
    result.friends = friendRepo_.getFriendList(uid);
    result.pendingRequests = friendRepo_.getPendingRequests(uid);
    result.offlineMessages = msgRepo_.getUndeliveredMessages(uid);

    return result;
}

bool LoginService::registerUser(const std::string& uid, const std::string& passwd,
                                const std::string& nickname, const std::string& email) {
    return userRepo_.registerUser(uid, passwd, nickname, email);
}

bool LoginService::logout(const std::string& uid, const std::string& token) {
    sessionRepo_.revokeSession(token);
    sessionRepo_.revokeAllSessions(uid);
    return true;
}
