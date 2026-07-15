#ifndef MUDUO_CHAT_SERVER_H
#define MUDUO_CHAT_SERVER_H

#include "muduo/net/TcpServer.h"
#include "muduo/net/EventLoop.h"
#include "muduo/base/noncopyable.h"
#include "muduo/base/ThreadPool.h"
#include "codec.h"
#include "session_manager.h"
#include "room_manager.h"
#include "service/login_service.h"
#include "service/chat_service.h"
#include "service/friend_service.h"
#include "service/room_service.h"
#include "chat.pb.h"

class ChatServer : muduo::noncopyable {
public:
    ChatServer(muduo::net::EventLoop* loop,
               const muduo::net::InetAddress& listenAddr,
               LoginService& loginService,
               ChatService& chatService,
               FriendService& friendService,
               RoomService& roomService,
               const std::string& node_id = "node-1");

    void start();
    void setThreadNum(int numThreads) { server_.setThreadNum(numThreads); }

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
    void sendOfflineMessages(const muduo::net::TcpConnectionPtr& conn,
                             const std::vector<MessageInfo>& messages);
    void sendPendingRequests(const muduo::net::TcpConnectionPtr& conn,
                             const std::vector<std::string>& requests);

    muduo::net::TcpServer server_;
    ChatCodec codec_;
    muduo::net::EventLoop* loop_;

    LoginService& loginService_;
    ChatService& chatService_;
    FriendService& friendService_;
    RoomService& roomService_;

    SessionManager sessionManager_;
    RoomManager roomManager_;
    muduo::ThreadPool threadPool_;
    std::string node_id_;
};

#endif
