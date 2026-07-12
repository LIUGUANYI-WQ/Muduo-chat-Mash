#ifndef MUDUO_CHAT_DB_H
#define MUDUO_CHAT_DB_H

#include <string>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <vector>
#include <functional>
#include <mysql/mysql.h>

class MySQLPool {
public:
    MySQLPool() = default;
    ~MySQLPool();

    bool init(const std::string& host, int port,
              const std::string& user, const std::string& password,
              const std::string& database, int poolSize);

    bool registerUser(const std::string& uid, const std::string& passwd);
    bool verifyUser(const std::string& uid, const std::string& passwd);
    bool userExists(const std::string& uid);

private:
    struct ConnWrapper {
        MYSQL* conn;
        bool inUse;
    };

    MYSQL* getConnection();
    void returnConnection(MYSQL* conn);
    bool ensureTable(MYSQL* conn);

    std::string host_;
    int port_;
    std::string user_;
    std::string password_;
    std::string database_;

    std::vector<MYSQL*> conns_;
    std::queue<MYSQL*> idle_;
    std::mutex mutex_;
    std::condition_variable cond_;
};

#endif
