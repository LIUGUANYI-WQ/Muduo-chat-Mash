#ifndef MUDUO_CHAT_DB_H
#define MUDUO_CHAT_DB_H

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <atomic>
#include <chrono>
#include <thread>
#include <mysql/mysql.h>

struct UserInfo {
    std::string uid;
    std::string nickname;
    std::string email;
    std::string avatarUrl;
    bool exists = false;
};

struct ConnInfo {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string database;
};

class MySQLPool {
public:
    MySQLPool() = default;
    ~MySQLPool();

    bool init(const std::string& host, int port,
              const std::string& user, const std::string& password,
              const std::string& database, int poolSize = 0);

    bool registerUser(const std::string& uid, const std::string& passwd,
                       const std::string& nickname = "",
                       const std::string& email = "");
    bool verifyUser(const std::string& uid, const std::string& passwd);
    bool userExists(const std::string& uid);
    UserInfo getUserInfo(const std::string& uid);

    // 好友系统
    bool addFriendRequest(const std::string& from, const std::string& to,
                          const std::string& message);
    bool respondFriendRequest(const std::string& from, const std::string& to,
                              bool accepted);
    bool removeFriends(const std::string& uid1, const std::string& uid2);
    std::vector<UserInfo> getFriendList(const std::string& uid);
    std::vector<std::string> getPendingRequests(const std::string& uid);

    int activeConns() const { return activeConns_.load(); }
    int idleConns() const;

private:
    MYSQL* createConnection();
    MYSQL* getConnection();
    void returnConnection(MYSQL* conn);
    bool ensureTable(MYSQL* conn);
    bool pingConnection(MYSQL* conn);

    ConnInfo info_;
    std::vector<MYSQL*> conns_;
    std::queue<MYSQL*> idle_;
    mutable std::mutex mutex_;
    std::condition_variable cond_;
    std::atomic<int> activeConns_{0};
    int maxSize_ = 0;
};

#endif
