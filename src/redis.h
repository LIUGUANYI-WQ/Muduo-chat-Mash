#ifndef MUDUO_CHAT_REDIS_H
#define MUDUO_CHAT_REDIS_H

#include <string>
#include <mutex>
#include <hiredis/hiredis.h>

class RedisCache {
public:
    RedisCache();
    ~RedisCache();

    bool init(const std::string& host = "127.0.0.1", int port = 6379);
    void close();

    void cacheUserToken(const std::string& uid, const std::string& token, int ttlSeconds = 3600);
    bool getCachedToken(const std::string& uid, std::string& token);

    void cacheUserAuth(const std::string& uid, bool exists, int ttlSeconds = 300);
    bool getCachedUserAuth(const std::string& uid, bool& exists);

    void invalidateUser(const std::string& uid);

private:
    redisContext* ctx_;
    std::mutex mutex_;
};

#endif
