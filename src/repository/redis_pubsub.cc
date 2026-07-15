#include "redis_pubsub.h"
#include <cstdio>
#include <cstring>

RedisPubSub::RedisPubSub() : port_(6379), db_(0), pub_conn_(nullptr), sub_conn_(nullptr) {}

RedisPubSub::~RedisPubSub() {
    stop();
}

bool RedisPubSub::init(const std::string& host, int port, const std::string& password, int db) {
    host_ = host;
    port_ = port;
    password_ = password;
    db_ = db;

    pub_conn_ = createConnection();
    sub_conn_ = createConnection();

    if (!pub_conn_ || !sub_conn_) {
        return false;
    }

    fprintf(stdout, "Redis Pub/Sub initialized\n");
    return true;
}

bool RedisPubSub::publish(const std::string& channel, const std::string& message) {
    if (!pub_conn_) return false;

    redisReply* reply = (redisReply*)redisCommand(pub_conn_, "PUBLISH %s %s", channel.c_str(), message.c_str());
    if (reply) {
        freeReplyObject(reply);
        return true;
    }
    return false;
}

bool RedisPubSub::subscribe(const std::string& channel, const MessageCallback& callback) {
    if (!sub_conn_) return false;

    callbacks_[channel] = callback;

    redisReply* reply = (redisReply*)redisCommand(sub_conn_, "SUBSCRIBE %s", channel.c_str());
    if (reply) {
        freeReplyObject(reply);
        return true;
    }
    return false;
}

bool RedisPubSub::unsubscribe(const std::string& channel) {
    if (!sub_conn_) return false;

    callbacks_.erase(channel);

    redisReply* reply = (redisReply*)redisCommand(sub_conn_, "UNSUBSCRIBE %s", channel.c_str());
    if (reply) {
        freeReplyObject(reply);
        return true;
    }
    return false;
}

void RedisPubSub::start() {
    if (running_.load()) return;

    running_ = true;
    sub_thread_ = std::thread(&RedisPubSub::runLoop, this);
}

void RedisPubSub::stop() {
    running_ = false;

    if (sub_thread_.joinable()) {
        sub_thread_.join();
    }

    if (pub_conn_) {
        redisFree(pub_conn_);
        pub_conn_ = nullptr;
    }

    if (sub_conn_) {
        redisFree(sub_conn_);
        sub_conn_ = nullptr;
    }
}

void RedisPubSub::runLoop() {
    if (!sub_conn_) return;

    while (running_.load()) {
        redisReply* reply = nullptr;
        if (redisGetReply(sub_conn_, (void**)&reply) != REDIS_OK) {
            fprintf(stderr, "Redis Pub/Sub recv error\n");
            continue;
        }

        if (!reply) continue;

        if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
            std::string type = reply->element[0]->str;
            std::string channel = reply->element[1]->str;
            std::string message;

            if (reply->elements > 2 && reply->element[2]->type == REDIS_REPLY_STRING) {
                message = reply->element[2]->str;
            }

            if (type == "message") {
                auto it = callbacks_.find(channel);
                if (it != callbacks_.end()) {
                    it->second(channel, message);
                }
            }
        }

        freeReplyObject(reply);
    }
}

redisContext* RedisPubSub::createConnection() {
    redisContext* conn = redisConnect(host_.c_str(), port_);
    if (!conn || conn->err) {
        if (conn) {
            fprintf(stderr, "Redis connect error: %s\n", conn->errstr);
            redisFree(conn);
        } else {
            fprintf(stderr, "Redis connect error: can't allocate context\n");
        }
        return nullptr;
    }

    if (!password_.empty()) {
        redisReply* reply = (redisReply*)redisCommand(conn, "AUTH %s", password_.c_str());
        if (reply) {
            if (reply->type != REDIS_REPLY_STATUS || strcmp(reply->str, "OK") != 0) {
                freeReplyObject(reply);
                redisFree(conn);
                return nullptr;
            }
            freeReplyObject(reply);
        } else {
            redisFree(conn);
            return nullptr;
        }
    }

    if (db_ != 0) {
        redisReply* reply = (redisReply*)redisCommand(conn, "SELECT %d", db_);
        if (reply) freeReplyObject(reply);
    }

    return conn;
}
