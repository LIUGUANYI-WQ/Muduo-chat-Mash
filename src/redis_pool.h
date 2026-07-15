#ifndef REDIS_POOL_H
#define REDIS_POOL_H

#include <string>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <hiredis/hiredis.h>

class RedisPool {
public:
    RedisPool();
    ~RedisPool();

    bool init(const std::string& host, int port, const std::string& password = "", int db = 0);

    redisContext* getConnection();

    void returnConnection(redisContext* conn);

    void close();

private:
    redisContext* createConnection();
    bool reconnect(redisContext* conn);

    std::string host_;
    int port_;
    std::string password_;
    int db_;

    thread_local static redisContext* tls_conn_;
    thread_local static bool tls_initialized_;
};

#endif
