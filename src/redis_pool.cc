#include "redis_pool.h"
#include <cstdio>
#include <cstring>

thread_local redisContext* RedisPool::tls_conn_ = nullptr;
thread_local bool RedisPool::tls_initialized_ = false;

RedisPool::RedisPool() : port_(6379), db_(0) {}

RedisPool::~RedisPool() {
    close();
}

bool RedisPool::init(const std::string& host, int port, const std::string& password, int db) {
    host_ = host;
    port_ = port;
    password_ = password;
    db_ = db;
    return true;
}

redisContext* RedisPool::getConnection() {
    if (!tls_initialized_) {
        tls_conn_ = createConnection();
        tls_initialized_ = true;
    } else if (tls_conn_ && tls_conn_->err) {
        reconnect(tls_conn_);
    }
    return tls_conn_;
}

void RedisPool::returnConnection(redisContext* conn) {
    (void)conn;
}

void RedisPool::close() {
    if (tls_conn_) {
        redisFree(tls_conn_);
        tls_conn_ = nullptr;
        tls_initialized_ = false;
    }
}

redisContext* RedisPool::createConnection() {
    redisContext* conn = redisConnect(host_.c_str(), port_);
    if (!conn || conn->err) {
        if (conn) {
            fprintf(stderr, "Redis connect error: %s\n", conn->errstr);
            redisFree(conn);
            return nullptr;
        } else {
            fprintf(stderr, "Redis connect error: can't allocate redis context\n");
            return nullptr;
        }
    }

    if (!password_.empty()) {
        redisReply* reply = (redisReply*)redisCommand(conn, "AUTH %s", password_.c_str());
        if (reply) {
            if (reply->type != REDIS_REPLY_STATUS || strcmp(reply->str, "OK") != 0) {
                fprintf(stderr, "Redis AUTH failed: %s\n", reply->str);
                freeReplyObject(reply);
                redisFree(conn);
                return nullptr;
            }
            freeReplyObject(reply);
        } else {
            fprintf(stderr, "Redis AUTH failed: no reply\n");
            redisFree(conn);
            return nullptr;
        }
    }

    if (db_ != 0) {
        redisReply* reply = (redisReply*)redisCommand(conn, "SELECT %d", db_);
        if (reply) {
            freeReplyObject(reply);
        }
    }

    fprintf(stdout, "Redis connected to %s:%d (thread_local)\n", host_.c_str(), port_);
    return conn;
}

bool RedisPool::reconnect(redisContext* conn) {
    fprintf(stderr, "Redis connection error, reconnecting...\n");
    redisFree(conn);
    tls_conn_ = createConnection();
    return tls_conn_ != nullptr;
}
