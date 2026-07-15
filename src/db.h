#ifndef MUDUO_CHAT_DB_H
#define MUDUO_CHAT_DB_H

#include <string>
#include <vector>
#include <mutex>
#include <condition_variable>
#include <queue>
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

struct MessageInfo {
    int64_t msg_id = 0;
    int64_t seq = 0;
    std::string from_uid;
    std::string to_uid;
    std::string room_name;
    std::string content;
    int recalled = 0;
    std::string created_at;
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

    bool init(const std::string& host, const std::string& user,
              const std::string& password, const std::string& database,
              int port = 3306, int poolSize = 0);

    MYSQL* getConnection();
    void returnConnection(MYSQL* conn);

    int activeConns() const { return activeConns_.load(); }
    int idleConns() const;

private:
    MYSQL* createConnection();
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
