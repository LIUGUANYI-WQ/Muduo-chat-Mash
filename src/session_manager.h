#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include "muduo/net/TcpConnection.h"
#include "muduo/base/noncopyable.h"
#include "service/login_service.h"

#include <unordered_map>
#include <map>
#include <set>
#include <string>
#include <memory>

struct Session {
    std::string uid;
    std::string token;
    bool authenticated = false;
};

class SessionManager : muduo::noncopyable {
public:
    SessionManager() {}

    void onConnection(const muduo::net::TcpConnectionPtr& conn);

    void onDisconnection(const muduo::net::TcpConnectionPtr& conn);

    bool authenticate(const muduo::net::TcpConnectionPtr& conn,
                      const std::string& uid, const std::string& token);

    void deauthenticate(const muduo::net::TcpConnectionPtr& conn);

    bool isAuthenticated(const muduo::net::TcpConnectionPtr& conn) const;

    const std::string& getUid(const muduo::net::TcpConnectionPtr& conn) const;

    muduo::net::TcpConnectionPtr getConnection(const std::string& uid) const;

    bool isOnline(const std::string& uid) const;

    size_t onlineCount() const;

    void addFriendship(const std::string& uid1, const std::string& uid2);

    void removeFriendship(const std::string& uid1, const std::string& uid2);

    const std::set<std::string>& getFriends(const std::string& uid) const;

    void addPendingRequest(const std::string& requester, const std::string& target);

    void removePendingRequest(const std::string& requester, const std::string& target);

    bool hasPendingRequest(const std::string& requester, const std::string& target) const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, muduo::net::TcpConnectionPtr> users_;
    std::map<std::string, std::set<std::string>> friendships_;
    std::set<std::string> pending_friend_requests_;
};

#endif
