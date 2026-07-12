#include "src/redis.h"
#include <cstdio>

RedisCache::RedisCache() : ctx_(nullptr) {}

RedisCache::~RedisCache() {
    close();
}

void RedisCache::close() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (ctx_) {
        redisFree(ctx_);
        ctx_ = nullptr;
    }
}

bool RedisCache::init(const std::string& host, int port) {
    std::lock_guard<std::mutex> lock(mutex_);
    ctx_ = redisConnect(host.c_str(), port);
    if (!ctx_ || ctx_->err) {
        if (ctx_) {
            fprintf(stderr, "Redis connect error: %s\n", ctx_->errstr);
            redisFree(ctx_);
            ctx_ = nullptr;
        } else {
            fprintf(stderr, "Redis connect error: can't allocate redis context\n");
        }
        return false;
    }

    fprintf(stdout, "Redis connected to %s:%d\n", host.c_str(), port);
    return true;
}

void RedisCache::cacheUserToken(const std::string& uid, const std::string& token, int ttlSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return;

    std::string key = "token:" + uid;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SETEX %s %d %s",
                                                   key.c_str(), ttlSeconds, token.c_str());
    if (reply) freeReplyObject(reply);
}

bool RedisCache::getCachedToken(const std::string& uid, std::string& token) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    std::string key = "token:" + uid;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "GET %s", key.c_str());
    if (!reply) return false;

    bool found = false;
    if (reply->type == REDIS_REPLY_STRING) {
        token = reply->str;
        found = true;
    }
    freeReplyObject(reply);
    return found;
}

void RedisCache::cacheUserAuth(const std::string& uid, bool exists, int ttlSeconds) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return;

    std::string key = "auth:" + uid;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "SETEX %s %d %d",
                                                   key.c_str(), ttlSeconds, exists ? 1 : 0);
    if (reply) freeReplyObject(reply);
}

bool RedisCache::getCachedUserAuth(const std::string& uid, bool& exists) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return false;

    std::string key = "auth:" + uid;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "GET %s", key.c_str());
    if (!reply) return false;

    bool found = false;
    if (reply->type == REDIS_REPLY_STRING) {
        exists = (reply->str[0] == '1');
        found = true;
    }
    freeReplyObject(reply);
    return found;
}

void RedisCache::invalidateUser(const std::string& uid) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!ctx_) return;

    std::string key = "auth:" + uid;
    redisReply* reply = (redisReply*)redisCommand(ctx_, "DEL %s", key.c_str());
    if (reply) freeReplyObject(reply);
}
