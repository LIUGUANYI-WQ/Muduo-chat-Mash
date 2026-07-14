#ifndef MUDUO_CHAT_SERVER_H
#define MUDUO_CHAT_SERVER_H

#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/base/noncopyable.h"
#include "src/codec.h"
#include "src/db.h"
#include "src/redis.h"
#include "src/thread_pool.h"
#include "chat.pb.h"

#include <map>
#include <set>
#include <unordered_map>

class ChatServer : muduo::noncopyable
{
public:
    ChatServer(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& listenAddr);

    void start();
    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

    struct Session {
        muduo::string uid;
        muduo::string token;
        bool authenticated = false;
    };

 private:
    void onConnection(const muduo::net::TcpConnectionPtr& conn);
    void onEnvelope(const muduo::net::TcpConnectionPtr& conn,
                    const chat::Envelope& env,
                    muduo::Timestamp receiveTime);

    void handleLogin(const muduo::net::TcpConnectionPtr& conn,
                     const chat::LoginRequest& req);
    void handleLogout(const muduo::net::TcpConnectionPtr& conn,
                      const chat::LogoutRequest& req);
    void handleRegister(const muduo::net::TcpConnectionPtr& conn,
                        const chat::RegisterRequest& req);
    void handleChatMessage(const muduo::net::TcpConnectionPtr& conn,
                           const chat::ChatMessage& msg);
    void handleCreateRoom(const muduo::net::TcpConnectionPtr& conn,
                          const chat::CreateRoom& req);
    void handleJoinRoom(const muduo::net::TcpConnectionPtr& conn,
                        const chat::JoinRoom& req);
    void handleFriendRequest(const muduo::net::TcpConnectionPtr& conn,
                             const chat::FriendRequest& req);
    void handleFriendResponse(const muduo::net::TcpConnectionPtr& conn,
                              const chat::FriendResponse& req);
    void handleFriendRemove(const muduo::net::TcpConnectionPtr& conn,
                             const chat::FriendRemove& req);
    void handleFriendList(const muduo::net::TcpConnectionPtr& conn);
    void handleRecall(const muduo::net::TcpConnectionPtr& conn,
                      const chat::RecallMessage& req);

    void sendError(const muduo::net::TcpConnectionPtr& conn,
                   uint32_t code, const muduo::string& reason);
    void sendFriendList(const muduo::net::TcpConnectionPtr& conn);

    muduo::net::TcpServer server_;
    ChatCodec codec_;
    muduo::net::EventLoop* loop_;
    MySQLPool db_;
    RedisCache redis_;
    ThreadPool threadPool_;

    std::unordered_map<muduo::string, muduo::net::TcpConnectionPtr> users_;
    std::map<muduo::string, std::set<muduo::string>> rooms_;
    std::map<muduo::string, std::set<muduo::string>> friendships_;
    std::set<muduo::string> pending_friend_requests_;
};

#endif
