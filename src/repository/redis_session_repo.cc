#include "redis_session_repo.h"
#include <cstdio>
#include <ctime>

bool RedisSessionRepo::createSession(const std::string& uid, const std::string& token,
                                     const std::string& node_id, int ttlSeconds) {
    redisContext* conn = pool_.getConnection();
    if (!conn) return false;

    std::string key = "session:" + token;

    redisReply* reply = (redisReply*)redisCommand(conn, "HMSET %s uid %s node_id %s login_time %ld",
                                                   key.c_str(), uid.c_str(), node_id.c_str(), time(nullptr));
    if (reply) {
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(conn, "EXPIRE %s %d", key.c_str(), ttlSeconds);
    if (reply) {
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(conn, "SET online:%s %s", uid.c_str(), node_id.c_str());
    if (reply) {
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(conn, "HSET user_route %s %s", uid.c_str(), node_id.c_str());
    if (reply) {
        freeReplyObject(reply);
    }

    return true;
}

std::optional<SessionInfo> RedisSessionRepo::getSessionByToken(const std::string& token) {
    redisContext* conn = pool_.getConnection();
    if (!conn) return std::nullopt;

    std::string key = "session:" + token;
    redisReply* reply = (redisReply*)redisCommand(conn, "HMGET %s uid node_id", key.c_str());
    if (!reply) return std::nullopt;

    SessionInfo info;
    if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 2) {
        if (reply->element[0] && reply->element[0]->type == REDIS_REPLY_STRING) {
            info.uid = reply->element[0]->str;
            info.token = token;
            info.exists = true;
        }
        if (reply->element[1] && reply->element[1]->type == REDIS_REPLY_STRING) {
            info.node_id = reply->element[1]->str;
        }
    }

    freeReplyObject(reply);
    return info.exists ? std::optional<SessionInfo>(info) : std::nullopt;
}

bool RedisSessionRepo::revokeSession(const std::string& token) {
    redisContext* conn = pool_.getConnection();
    if (!conn) return false;

    std::string key = "session:" + token;
    redisReply* reply = (redisReply*)redisCommand(conn, "DEL %s", key.c_str());
    if (reply) {
        freeReplyObject(reply);
    }
    return true;
}

bool RedisSessionRepo::revokeAllSessions(const std::string& uid) {
    redisContext* conn = pool_.getConnection();
    if (!conn) return false;

    redisReply* reply = (redisReply*)redisCommand(conn, "GET online:%s", uid.c_str());
    if (reply) {
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(conn, "DEL online:%s", uid.c_str());
    if (reply) {
        freeReplyObject(reply);
    }

    reply = (redisReply*)redisCommand(conn, "HDEL user_route %s", uid.c_str());
    if (reply) {
        freeReplyObject(reply);
    }

    return true;
}

bool RedisSessionRepo::isValidSession(const std::string& token) {
    return getSessionByToken(token).has_value();
}

bool RedisSessionRepo::updateSessionNode(const std::string& token, const std::string& node_id) {
    redisContext* conn = pool_.getConnection();
    if (!conn) return false;

    std::string key = "session:" + token;
    redisReply* reply = (redisReply*)redisCommand(conn, "HMSET %s node_id %s", key.c_str(), node_id.c_str());
    if (reply) {
        freeReplyObject(reply);
    }
    return true;
}
