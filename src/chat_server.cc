#include "src/chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/base/Types.h"

#include <boost/any.hpp>
#include <functional>
#include <random>
#include <cstdlib>

using namespace muduo;
using namespace muduo::net;

ChatServer::ChatServer(EventLoop* loop, const InetAddress& listenAddr)
    : server_(loop, listenAddr, "ChatServer"),
      codec_(std::bind(&ChatServer::onEnvelope, this, _1, _2, _3)),
      loop_(loop)
{
    const char* host = std::getenv("MYSQL_HOST");
    const char* user = std::getenv("MYSQL_USER");
    const char* pass = std::getenv("MYSQL_PASSWORD");
    const char* db   = std::getenv("MYSQL_DATABASE");
    const char* poolStr = std::getenv("MYSQL_POOL_SIZE");

    std::string s_host = host ? host : "127.0.0.1";
    std::string s_user = user ? user : "root";
    std::string s_pass = pass ? pass : "123456";
    std::string s_db   = db   ? db   : "chat";
    int poolSize = poolStr ? atoi(poolStr) : 8;

    if (!db_.init(s_host, 3306, s_user, s_pass, s_db, poolSize)) {
        LOG_FATAL << "MySQL pool init failed, check MYSQL_* env vars";
    }

    const char* redisHost = std::getenv("REDIS_HOST");
    const char* redisPortStr = std::getenv("REDIS_PORT");
    std::string s_redisHost = redisHost ? redisHost : "127.0.0.1";
    int redisPort = redisPortStr ? atoi(redisPortStr) : 6379;

    if (!redis_.init(s_redisHost, redisPort)) {
        LOG_WARN << "Redis connect failed, running without cache";
    }

    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&ChatCodec::onMessage, &codec_, _1, _2, _3));
}

void ChatServer::start()
{
    server_.start();
}

void ChatServer::onConnection(const TcpConnectionPtr& conn)
{
    LOG_INFO << conn->peerAddress().toIpPort() << " -> "
             << conn->localAddress().toIpPort() << " is "
             << (conn->connected() ? "UP" : "DOWN");

    if (!conn->connected())
    {
        if (conn->getContext().empty())
            return;
        try
        {
            const Session& s = boost::any_cast<const Session&>(conn->getContext());
            if (!s.uid.empty())
            {
                users_.erase(s.uid);
                tokens_.erase(s.token);
                LOG_INFO << "User " << s.uid << " disconnected";
            }
        }
        catch (const boost::bad_any_cast&)
        {
        }
    }
}

void ChatServer::onEnvelope(const TcpConnectionPtr& conn,
                            const chat::Envelope& env,
                            Timestamp receiveTime)
{
    switch (env.payload_case())
    {
        case chat::Envelope::kLoginReq:
            handleLogin(conn, env.login_req());
            break;
        case chat::Envelope::kLogoutReq:
            handleLogout(conn, env.logout_req());
            break;
        case chat::Envelope::kRegisterReq:
            handleRegister(conn, env.register_req());
            break;
        case chat::Envelope::kChatMsg:
            handleChatMessage(conn, env.chat_msg());
            break;
        case chat::Envelope::kCreateRoom:
            handleCreateRoom(conn, env.create_room());
            break;
        case chat::Envelope::kJoinRoom:
            handleJoinRoom(conn, env.join_room());
            break;
        case chat::Envelope::kFriendReq:
            handleFriendRequest(conn, env.friend_req());
            break;
        case chat::Envelope::kFriendResp:
            handleFriendResponse(conn, env.friend_resp());
            break;
        case chat::Envelope::kFriendRemove:
            handleFriendRemove(conn, env.friend_remove());
            break;
        case chat::Envelope::kRecallMsg:
            handleRecall(conn, env.recall_msg());
            break;
        case chat::Envelope::kHeartbeat:
            break;
        default:
            conn->shutdown();
            break;
    }
}

static bool isAuthenticated(const TcpConnectionPtr& conn)
{
    if (conn->getContext().empty())
        return false;
    try
    {
        const ChatServer::Session& s =
            boost::any_cast<const ChatServer::Session&>(conn->getContext());
        return s.authenticated;
    }
    catch (const boost::bad_any_cast&)
    {
        return false;
    }
}

static const string& currentUid(const TcpConnectionPtr& conn)
{
    static const string empty;
    try
    {
        const ChatServer::Session& s =
            boost::any_cast<const ChatServer::Session&>(conn->getContext());
        return s.uid;
    }
    catch (const boost::bad_any_cast&)
    {
        return empty;
    }
}

void ChatServer::handleLogin(const TcpConnectionPtr& conn,
                            const chat::LoginRequest& req)
{
    if (req.token().empty() && req.passwd().empty())
    {
        sendError(conn, 1, "need password or token");
        return;
    }

    if (!req.token().empty())
    {
        auto it = tokens_.find(req.token());
        if (it != tokens_.end())
        {
            string uid = it->second;
            Session s;
            s.uid = uid;
            s.token = req.token();
            s.authenticated = true;
            conn->setContext(s);

            users_[uid] = conn;
            LOG_INFO << "User " << uid << " reconnected via token";

            chat::ServerMessage reply;
            reply.mutable_login_resp()->set_ok(true);
            reply.mutable_login_resp()->set_token(req.token());
            reply.mutable_login_resp()->set_server_time(
                Timestamp::now().microSecondsSinceEpoch());
            codec_.sendServerMessage(conn, reply);
            return;
        }
    }

    string uid = req.uid();
    if (uid.empty())
    {
        sendError(conn, 2, "uid required");
        return;
    }

    bool userOk = false;

    // 先查 Redis 缓存
    bool cached = false;
    if (redis_.getCachedUserAuth(uid, userOk)) {
        cached = true;
    } else {
        // 缓存未命中，查 MySQL
        userOk = db_.verifyUser(uid, req.passwd());
        // 缓存结果（存在缓存5分钟）
        redis_.cacheUserAuth(uid, userOk, 300);
    }

    if (!userOk)
    {
        sendError(conn, 6, "invalid uid or password");
        return;
    }

    static thread_local std::mt19937 gen(std::random_device{}());
    static const char hex[] = "0123456789abcdef";
    std::uniform_int_distribution<> dis(0, 15);
    string token(32, ' ');
    for (int i = 0; i < 32; ++i)
        token[i] = hex[dis(gen)];

    Session s;
    s.uid = uid;
    s.token = token;
    s.authenticated = true;
    conn->setContext(s);

    tokens_[token] = uid;
    users_[uid] = conn;

    LOG_INFO << "User " << uid << " logged in, token=" << token;

    chat::ServerMessage reply;
    reply.mutable_login_resp()->set_ok(true);
    reply.mutable_login_resp()->set_token(token);
    reply.mutable_login_resp()->set_server_time(
        Timestamp::now().microSecondsSinceEpoch());
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::handleLogout(const TcpConnectionPtr& conn,
                             const chat::LogoutRequest& req)
{
    string uid = req.uid();
    users_.erase(uid);
    tokens_.erase(req.token());
    LOG_INFO << "User " << uid << " logged out";

    Session s;
    s.uid = "";
    s.authenticated = false;
    conn->setContext(s);

    chat::ServerMessage reply;
    reply.mutable_login_resp()->set_ok(true);
    reply.mutable_login_resp()->set_reason("logged out");
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::handleRegister(const TcpConnectionPtr& conn,
                               const chat::RegisterRequest& req)
{
    if (req.uid().empty() || req.passwd().empty())
    {
        sendError(conn, 7, "uid and password required");
        return;
    }

    if (db_.userExists(req.uid()))
    {
        chat::ServerMessage reply;
        reply.mutable_register_resp()->set_ok(false);
        reply.mutable_register_resp()->set_reason("user already exists");
        codec_.sendServerMessage(conn, reply);
        return;
    }

    if (!db_.registerUser(req.uid(), req.passwd()))
    {
        chat::ServerMessage reply;
        reply.mutable_register_resp()->set_ok(false);
        reply.mutable_register_resp()->set_reason("register failed");
        codec_.sendServerMessage(conn, reply);
        return;
    }

    LOG_INFO << "User " << req.uid() << " registered";

    chat::ServerMessage reply;
    reply.mutable_register_resp()->set_ok(true);
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::handleChatMessage(const TcpConnectionPtr& conn,
                                  const chat::ChatMessage& msg)
{
    if (!isAuthenticated(conn))
        return;

    const string& from = currentUid(conn);

    if (!msg.to().empty())
    {
        auto it = users_.find(msg.to());
        if (it != users_.end())
        {
            chat::ServerMessage reply;
            chat::ChatMessage* cm = reply.mutable_chat_msg();
            cm->set_from(from);
            cm->set_to(msg.to());
            cm->set_content(msg.content());
            cm->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
            codec_.sendServerMessage(it->second, reply);

            chat::ServerMessage echo;
            chat::ChatMessage* em = echo.mutable_chat_msg();
            em->set_from(from);
            em->set_to(msg.to());
            em->set_content(msg.content());
            em->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
            codec_.sendServerMessage(conn, echo);
        }
        else
        {
            sendError(conn, 3, "user offline");
        }
    }
    else if (!msg.room().empty())
    {
        auto rit = rooms_.find(msg.room());
        if (rit != rooms_.end())
        {
            for (const auto& member : rit->second)
            {
                auto uit = users_.find(member);
                if (uit != users_.end())
                {
                    chat::ServerMessage reply;
                    chat::ChatMessage* cm = reply.mutable_chat_msg();
                    cm->set_from(from);
                    cm->set_room(msg.room());
                    cm->set_content(msg.content());
                    cm->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
                    codec_.sendServerMessage(uit->second, reply);
                }
            }
        }
        else
        {
            sendError(conn, 4, "room not found");
        }
    }
}

void ChatServer::handleCreateRoom(const TcpConnectionPtr& conn,
                                 const chat::CreateRoom& req)
{
    if (rooms_.find(req.name()) != rooms_.end())
    {
        sendError(conn, 5, "room already exists");
        return;
    }
    rooms_[req.name()] = std::set<string>();
    LOG_INFO << "Room created: " << req.name();
}

void ChatServer::handleJoinRoom(const TcpConnectionPtr& conn,
                               const chat::JoinRoom& req)
{
    auto it = rooms_.find(req.room_name());
    if (it == rooms_.end())
    {
        sendError(conn, 4, "room not found");
        return;
    }
    it->second.insert(req.uid());
    LOG_INFO << "User " << req.uid() << " joined room " << req.room_name();
}

void ChatServer::handleFriendRequest(const TcpConnectionPtr& conn,
                                  const chat::FriendRequest& req)
{
    friendships_[req.from_uid()].insert(req.to_uid());

    auto it = users_.find(req.to_uid());
    if (it != users_.end())
    {
        chat::ServerMessage reply;
        chat::FriendRequest* fr = reply.mutable_friend_req();
        fr->set_from_uid(req.from_uid());
        fr->set_message(req.message());
        codec_.sendServerMessage(it->second, reply);
    }
}

void ChatServer::handleFriendResponse(const TcpConnectionPtr& conn,
                                    const chat::FriendResponse& req)
{
    if (req.accepted())
    {
        friendships_[req.from_uid()].insert(req.to_uid());
        friendships_[req.to_uid()].insert(req.from_uid());
    }

    auto it = users_.find(req.to_uid());
    if (it != users_.end())
    {
        chat::ServerMessage reply;
        chat::FriendResponse* fr = reply.mutable_friend_resp();
        fr->set_from_uid(req.from_uid());
        fr->set_accepted(req.accepted());
        codec_.sendServerMessage(it->second, reply);
    }
}

void ChatServer::handleFriendRemove(const TcpConnectionPtr& conn,
                                  const chat::FriendRemove& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);
    friendships_[uid].erase(req.target_uid());
    friendships_[req.target_uid()].erase(uid);
}

void ChatServer::handleRecall(const TcpConnectionPtr& conn,
                             const chat::RecallMessage& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);

    if (!req.to_uid().empty())
    {
        auto it = users_.find(req.to_uid());
        if (it != users_.end())
        {
            chat::ServerMessage reply;
            chat::RecallNotify* rn = reply.mutable_recall_notify();
            rn->set_msg_id(req.msg_id());
            rn->set_from_uid(uid);
            rn->set_to_uid(req.to_uid());
            rn->set_recall_time(Timestamp::now().microSecondsSinceEpoch());
            codec_.sendServerMessage(it->second, reply);
        }
        chat::ServerMessage echo;
        chat::RecallNotify* en = echo.mutable_recall_notify();
        en->set_msg_id(req.msg_id());
        en->set_from_uid(uid);
        en->set_recall_time(Timestamp::now().microSecondsSinceEpoch());
        codec_.sendServerMessage(conn, echo);
    }
    else if (!req.room().empty())
    {
        auto rit = rooms_.find(req.room());
        if (rit != rooms_.end())
        {
            for (const auto& member : rit->second)
            {
                auto uit = users_.find(member);
                if (uit != users_.end())
                {
                    chat::ServerMessage reply;
                    chat::RecallNotify* rn = reply.mutable_recall_notify();
                    rn->set_msg_id(req.msg_id());
                    rn->set_from_uid(uid);
                    rn->set_room(req.room());
                    rn->set_recall_time(Timestamp::now().microSecondsSinceEpoch());
                    codec_.sendServerMessage(uit->second, reply);
                }
            }
        }
    }
}

void ChatServer::sendError(const TcpConnectionPtr& conn,
                          uint32_t code, const string& reason)
{
    chat::ServerMessage reply;
    reply.mutable_error()->set_code(code);
    reply.mutable_error()->set_reason(reason);
    codec_.sendServerMessage(conn, reply);
}
