#include "session_manager.h"
#include "muduo/base/Logging.h"
#include <boost/any.hpp>

void SessionManager::onConnection(const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is UP";
}

void SessionManager::onDisconnection(const muduo::net::TcpConnectionPtr& conn) {
    LOG_INFO << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is DOWN";

    if (conn->getContext().empty())
        return;

    try {
        const Session& s = boost::any_cast<const Session&>(conn->getContext());
        if (!s.uid.empty()) {
            std::lock_guard<std::mutex> lock(mutex_);
            users_.erase(s.uid);
            LOG_INFO << "User " << s.uid << " disconnected";
        }
    } catch (const boost::bad_any_cast&) {
    }
}

bool SessionManager::authenticate(const muduo::net::TcpConnectionPtr& conn,
                                  const std::string& uid, const std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    users_[uid] = conn;

    Session s;
    s.uid = uid;
    s.token = token;
    s.authenticated = true;
    conn->setContext(s);

    LOG_INFO << "User " << uid << " authenticated";
    return true;
}

void SessionManager::deauthenticate(const muduo::net::TcpConnectionPtr& conn) {
    std::lock_guard<std::mutex> lock(mutex_);

    try {
        const Session& s = boost::any_cast<const Session&>(conn->getContext());
        if (!s.uid.empty()) {
            users_.erase(s.uid);
        }
    } catch (const boost::bad_any_cast&) {
    }

    Session s;
    s.uid = "";
    s.authenticated = false;
    conn->setContext(s);
}

bool SessionManager::isAuthenticated(const muduo::net::TcpConnectionPtr& conn) const {
    if (conn->getContext().empty())
        return false;

    try {
        const Session& s = boost::any_cast<const Session&>(conn->getContext());
        return s.authenticated;
    } catch (const boost::bad_any_cast&) {
        return false;
    }
}

const std::string& SessionManager::getUid(const muduo::net::TcpConnectionPtr& conn) const {
    static const std::string empty;
    try {
        const Session& s = boost::any_cast<const Session&>(conn->getContext());
        return s.uid;
    } catch (const boost::bad_any_cast&) {
        return empty;
    }
}

muduo::net::TcpConnectionPtr SessionManager::getConnection(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = users_.find(uid);
    return it != users_.end() ? it->second : nullptr;
}

bool SessionManager::isOnline(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.find(uid) != users_.end();
}

size_t SessionManager::onlineCount() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return users_.size();
}

void SessionManager::addFriendship(const std::string& uid1, const std::string& uid2) {
    std::lock_guard<std::mutex> lock(mutex_);
    friendships_[uid1].insert(uid2);
    friendships_[uid2].insert(uid1);
}

void SessionManager::removeFriendship(const std::string& uid1, const std::string& uid2) {
    std::lock_guard<std::mutex> lock(mutex_);
    friendships_[uid1].erase(uid2);
    friendships_[uid2].erase(uid1);
}

const std::set<std::string>& SessionManager::getFriends(const std::string& uid) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = friendships_.find(uid);
    static const std::set<std::string> empty;
    return it != friendships_.end() ? it->second : empty;
}

void SessionManager::addPendingRequest(const std::string& requester, const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_friend_requests_.insert(requester + ":" + target);
}

void SessionManager::removePendingRequest(const std::string& requester, const std::string& target) {
    std::lock_guard<std::mutex> lock(mutex_);
    pending_friend_requests_.erase(requester + ":" + target);
}

bool SessionManager::hasPendingRequest(const std::string& requester, const std::string& target) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return pending_friend_requests_.count(requester + ":" + target) > 0;
}
