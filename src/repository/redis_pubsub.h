#ifndef REDIS_PUBSUB_H
#define REDIS_PUBSUB_H

#include <string>
#include <functional>
#include <thread>
#include <atomic>
#include <hiredis/hiredis.h>

class RedisPubSub {
public:
    using MessageCallback = std::function<void(const std::string& channel, const std::string& message)>;

    RedisPubSub();
    ~RedisPubSub();

    bool init(const std::string& host, int port, const std::string& password = "", int db = 0);

    bool publish(const std::string& channel, const std::string& message);

    bool subscribe(const std::string& channel, const MessageCallback& callback);

    bool unsubscribe(const std::string& channel);

    void start();

    void stop();

private:
    void runLoop();
    redisContext* createConnection();

    std::string host_;
    int port_;
    std::string password_;
    int db_;

    redisContext* pub_conn_;
    redisContext* sub_conn_;

    std::thread sub_thread_;
    std::atomic<bool> running_{false};

    std::unordered_map<std::string, MessageCallback> callbacks_;
};

#endif
