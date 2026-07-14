#include "src/chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/base/Types.h"

#include <boost/any.hpp>
#include <functional>
#include <random>
#include <cstdlib>
#include <ctime>
#include <cstdio>

using namespace muduo;
using namespace muduo::net;

ChatServer::ChatServer(EventLoop* loop, const InetAddress& listenAddr)
    : server_(loop, listenAddr, "ChatServer"),
      codec_(std::bind(&ChatServer::onEnvelope, this, _1, _2, _3)),
      loop_(loop),
      threadPool_([]() {
          const char* env = std::getenv("THREAD_POOL_SIZE");
          int n = env ? atoi(env) : 0;
          if (n <= 0) n = std::thread::hardware_concurrency() * 2;
          if (n <= 0) n = 8;
          return n;
      }())
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

    // token 登录：查 Redis 获取 uid
    if (!req.token().empty())
    {
        string uid = req.uid();
        if (!uid.empty())
        {
            // Redis GET 放到线程池
            threadPool_.enqueue([this, conn, uid, token = req.token()]() {
                string cachedToken;
                bool ok = redis_.getToken(uid, cachedToken) && cachedToken == token;

                UserInfo info;
                std::vector<UserInfo> friends;
                std::vector<string> pendingReqs;
                std::vector<MessageInfo> offlineMsgs;
                if (ok) {
                    info = db_.getUserInfo(uid);
                    friends = db_.getFriendList(uid);
                    pendingReqs = db_.getPendingRequests(uid);
                    offlineMsgs = db_.getUndeliveredMessages(uid);
                }

                auto* ioLoop = conn->getLoop();
                ioLoop->runInLoop([this, conn, uid, token, ok, info, friends, pendingReqs, offlineMsgs]() {
                    if (ok) {
                        Session s;
                        s.uid = uid;
                        s.token = token;
                        s.authenticated = true;
                        conn->setContext(s);
                        users_[uid] = conn;

                        // 缓存好友列表
                        for (const auto& f : friends)
                            friendships_[uid].insert(f.uid);

                        LOG_INFO << "User " << uid << " reconnected via token (Redis)";

                        chat::ServerMessage reply;
                        reply.mutable_login_resp()->set_ok(true);
                        reply.mutable_login_resp()->set_token(token);
                        reply.mutable_login_resp()->set_nickname(info.nickname);
                        reply.mutable_login_resp()->set_email(info.email);
                        reply.mutable_login_resp()->set_avatar_url(info.avatarUrl);
                        reply.mutable_login_resp()->set_server_time(
                            Timestamp::now().microSecondsSinceEpoch());
                        codec_.sendServerMessage(conn, reply);

                        // 推送待处理的好友请求
                        for (const auto& requester : pendingReqs) {
                            pending_friend_requests_.insert(requester + ":" + uid);
                            chat::ServerMessage pending;
                            chat::FriendRequest* fr = pending.mutable_friend_req();
                            fr->set_from_uid(requester);
                            fr->set_to_uid(uid);
                            codec_.sendServerMessage(conn, pending);
                        }

                        // 推送离线消息
                        for (const auto& om : offlineMsgs) {
                            chat::ServerMessage om_reply;
                            chat::ChatMessage* cm = om_reply.mutable_chat_msg();
                            cm->set_from(om.from_uid);
                            if (!om.to_uid.empty()) cm->set_to(om.to_uid);
                            if (!om.room_name.empty()) cm->set_room(om.room_name);
                            cm->set_content(om.content);
                            cm->set_msg_id(om.msg_id);
                            cm->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
                            codec_.sendServerMessage(conn, om_reply);
                        }
                        if (!offlineMsgs.empty()) {
                            threadPool_.enqueue([this, uid]() {
                                db_.markMessagesDelivered(uid);
                            });
                        }

                        // 推送好友列表
                        sendFriendList(conn);
                    } else {
                        sendError(conn, 6, "invalid token");
                    }
                });
            });
            return;
        }
    }

    // 密码登录
    string uid = req.uid();
    if (uid.empty())
    {
        sendError(conn, 2, "uid required");
        return;
    }

    string passwd = req.passwd();

    // MySQL verifyUser + Redis cacheToken 放到线程池
    threadPool_.enqueue([this, conn, uid, passwd]() {
        bool userOk = db_.verifyUser(uid, passwd);

        if (!userOk) {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 6, "invalid uid or password");
            });
            return;
        }

        // 查用户资料 + 好友列表 + 待处理请求 + 离线消息
        UserInfo info = db_.getUserInfo(uid);
        auto friends = db_.getFriendList(uid);
        auto pendingReqs = db_.getPendingRequests(uid);
        auto offlineMsgs = db_.getUndeliveredMessages(uid);

        // 生成 token，写入 Redis
        static thread_local std::mt19937 gen(std::random_device{}());
        static const char hex[] = "0123456789abcdef";
        std::uniform_int_distribution<> dis(0, 15);
        string token(32, ' ');
        for (int i = 0; i < 32; ++i)
            token[i] = hex[dis(gen)];

        redis_.cacheToken(uid, token, 3600);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, token, info, friends, pendingReqs, offlineMsgs]() {
            Session s;
            s.uid = uid;
            s.token = token;
            s.authenticated = true;
            conn->setContext(s);
            users_[uid] = conn;

            // 缓存好友列表到内存
            for (const auto& f : friends)
                friendships_[uid].insert(f.uid);

            LOG_INFO << "User " << uid << " logged in, token=" << token;

            // 登录响应
            chat::ServerMessage loginReply;
            loginReply.mutable_login_resp()->set_ok(true);
            loginReply.mutable_login_resp()->set_token(token);
            loginReply.mutable_login_resp()->set_nickname(info.nickname);
            loginReply.mutable_login_resp()->set_email(info.email);
            loginReply.mutable_login_resp()->set_avatar_url(info.avatarUrl);
            loginReply.mutable_login_resp()->set_server_time(
                Timestamp::now().microSecondsSinceEpoch());
            codec_.sendServerMessage(conn, loginReply);

            // 推送待处理的好友请求
            for (const auto& requester : pendingReqs) {
                pending_friend_requests_.insert(requester + ":" + uid);
                chat::ServerMessage pending;
                chat::FriendRequest* fr = pending.mutable_friend_req();
                fr->set_from_uid(requester);
                fr->set_to_uid(uid);
                codec_.sendServerMessage(conn, pending);
            }

            // 推送离线消息
            for (const auto& om : offlineMsgs) {
                chat::ServerMessage om_reply;
                chat::ChatMessage* cm = om_reply.mutable_chat_msg();
                cm->set_from(om.from_uid);
                if (!om.to_uid.empty()) cm->set_to(om.to_uid);
                if (!om.room_name.empty()) cm->set_room(om.room_name);
                cm->set_content(om.content);
                cm->set_msg_id(om.msg_id);
                cm->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
                codec_.sendServerMessage(conn, om_reply);
            }
            if (!offlineMsgs.empty()) {
                threadPool_.enqueue([this, uid]() {
                    db_.markMessagesDelivered(uid);
                });
            }

            // 推送好友列表
            sendFriendList(conn);
        });
    });
}

void ChatServer::handleLogout(const TcpConnectionPtr& conn,
                             const chat::LogoutRequest& req)
{
    string uid = req.uid();
    users_.erase(uid);

    // Redis DEL 放到线程池
    threadPool_.enqueue([this, uid]() {
        redis_.invalidateToken(uid);
    });

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

    string uid = req.uid();
    string passwd = req.passwd();
    string nickname = req.nickname();
    string email = req.email();

    // MySQL userExists + registerUser 放到线程池
    threadPool_.enqueue([this, conn, uid, passwd, nickname, email]() {
        if (db_.userExists(uid)) {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                chat::ServerMessage reply;
                reply.mutable_register_resp()->set_ok(false);
                reply.mutable_register_resp()->set_reason("user already exists");
                codec_.sendServerMessage(conn, reply);
            });
            return;
        }

        if (!db_.registerUser(uid, passwd, nickname, email)) {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                chat::ServerMessage reply;
                reply.mutable_register_resp()->set_ok(false);
                reply.mutable_register_resp()->set_reason("register failed");
                codec_.sendServerMessage(conn, reply);
            });
            return;
        }

        LOG_INFO << "User " << uid << " registered";

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn]() {
            chat::ServerMessage reply;
            reply.mutable_register_resp()->set_ok(true);
            codec_.sendServerMessage(conn, reply);
        });
    });
}

void ChatServer::handleChatMessage(const TcpConnectionPtr& conn,
                                  const chat::ChatMessage& msg)
{
    if (!isAuthenticated(conn))
        return;

    const string& from = currentUid(conn);

    // MySQL 持久化 + 转发放在线程池
    threadPool_.enqueue([this, conn, from, to = msg.to(), room = msg.room(), content = msg.content()]() {
        int64_t msg_id = db_.storeMessage(from, to, room, content);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, from, to, room, content, msg_id]() {
            int64_t now = Timestamp::now().microSecondsSinceEpoch();

            if (!to.empty())
            {
                // 私聊
                auto it = users_.find(to);
                bool offline = (it == users_.end());

                chat::ServerMessage reply;
                chat::ChatMessage* cm = reply.mutable_chat_msg();
                cm->set_from(from);
                cm->set_to(to);
                cm->set_content(content);
                cm->set_msg_id(msg_id);
                cm->set_timestamp(now);
                if (!offline)
                    codec_.sendServerMessage(it->second, reply);

                // 发给发送者回执（含 msg_id）
                chat::ServerMessage echo;
                chat::ChatMessage* em = echo.mutable_chat_msg();
                em->set_from(from);
                em->set_to(to);
                em->set_content(content);
                em->set_msg_id(msg_id);
                em->set_timestamp(now);
                codec_.sendServerMessage(conn, echo);

                if (offline) {
                    sendError(conn, 3, "user offline");
                    // 离线消息写入 offline_messages，上线后补推
                    threadPool_.enqueue([this, to, msg_id]() {
                        db_.addOfflineMessage(to, msg_id);
                    });
                }
            }
            else if (!room.empty())
            {
                // 群聊
                auto rit = rooms_.find(room);
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
                            cm->set_room(room);
                            cm->set_content(content);
                            cm->set_msg_id(msg_id);
                            cm->set_timestamp(now);
                            codec_.sendServerMessage(uit->second, reply);
                        }
                    }
                }
                else
                {
                    sendError(conn, 4, "room not found");
                }
            }
        });
    });
}

void ChatServer::handleCreateRoom(const TcpConnectionPtr& conn,
                                 const chat::CreateRoom& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);

    if (rooms_.find(req.name()) != rooms_.end())
    {
        sendError(conn, 5, "room already exists");
        return;
    }

    threadPool_.enqueue([this, conn, uid, name = req.name()]() {
        if (db_.roomExists(name)) {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 5, "room already exists");
            });
            return;
        }

        int64_t roomId = db_.createRoom(name, uid);
        if (roomId > 0) {
            db_.addRoomMember(roomId, uid);
        }

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, name]() {
            rooms_[name].insert(uid);
            LOG_INFO << "Room created: " << name << " by " << uid;
        });
    });
}

void ChatServer::handleJoinRoom(const TcpConnectionPtr& conn,
                               const chat::JoinRoom& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);

    threadPool_.enqueue([this, conn, uid, roomName = req.room_name()]() {
        int64_t roomId = db_.getRoomIdByName(roomName);
        if (roomId <= 0) {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 4, "room not found");
            });
            return;
        }

        db_.addRoomMember(roomId, uid);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, roomName]() {
            rooms_[roomName].insert(uid);
            LOG_INFO << "User " << uid << " joined room " << roomName;
        });
    });
}

void ChatServer::handleFriendRequest(const TcpConnectionPtr& conn,
                                   const chat::FriendRequest& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);

    // MySQL 持久化
    threadPool_.enqueue([this, conn, from = req.from_uid(), to = req.to_uid(), msg = req.message()]() {
        db_.addFriendRequest(from, to, msg);

        // 更新内存表
        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, from, to, msg]() {
            friendships_[from].insert(to);
            pending_friend_requests_.insert(from + ":" + to);

            // 如果对方在线，实时推送
            auto it = users_.find(to);
            if (it != users_.end())
            {
                chat::ServerMessage reply;
                chat::FriendRequest* fr = reply.mutable_friend_req();
                fr->set_from_uid(from);
                fr->set_message(msg);
                codec_.sendServerMessage(it->second, reply);
            }
            // 离线：请求已存 MySQL，对方登录时通过 getPendingRequests 拉取
        });
    });
}

void ChatServer::handleFriendResponse(const TcpConnectionPtr& conn,
                                      const chat::FriendResponse& req)
{
    if (!isAuthenticated(conn))
        return;

    // from_uid = 响应者(Bob), to_uid = 原请求者(Alice)
    string responder = req.from_uid();
    string requester = req.to_uid();

    threadPool_.enqueue([this, conn, requester, responder, accepted = req.accepted()]() {
        db_.respondFriendRequest(requester, responder, accepted);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, requester, responder, accepted]() {
            if (accepted)
            {
                friendships_[requester].insert(responder);
                friendships_[responder].insert(requester);
            }
            pending_friend_requests_.erase(requester + ":" + responder);

            // 通知原请求方(Alice)
            auto it = users_.find(requester);
            if (it != users_.end())
            {
                chat::ServerMessage reply;
                chat::FriendResponse* fr = reply.mutable_friend_resp();
                fr->set_from_uid(responder);  // 响应者是 Bob
                fr->set_to_uid(requester);     // 通知的是 Alice
                fr->set_accepted(accepted);
                codec_.sendServerMessage(it->second, reply);
            }

            // 双方刷新好友列表
            sendFriendList(conn);
            auto uit = users_.find(requester);
            if (uit != users_.end())
                sendFriendList(uit->second);
        });
    });
}

void ChatServer::handleFriendRemove(const TcpConnectionPtr& conn,
                                   const chat::FriendRemove& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);

    threadPool_.enqueue([this, conn, uid, target = req.target_uid()]() {
        db_.removeFriends(uid, target);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, target]() {
            friendships_[uid].erase(target);
            friendships_[target].erase(uid);

            // 通知对方
            auto it = users_.find(target);
            if (it != users_.end())
            {
                chat::ServerMessage reply;
                chat::FriendRemove* fr = reply.mutable_friend_remove();
                fr->set_target_uid(uid);
                codec_.sendServerMessage(it->second, reply);
                sendFriendList(it->second);
            }
            sendFriendList(conn);
        });
    });
}

void ChatServer::handleFriendList(const TcpConnectionPtr& conn)
{
    sendFriendList(conn);
}

void ChatServer::sendFriendList(const TcpConnectionPtr& conn)
{
    const string& uid = currentUid(conn);

    chat::ServerMessage reply;
    chat::FriendList* fl = reply.mutable_friend_list();
    for (const auto& fuid : friendships_[uid])
    {
        chat::FriendInfo* fi = fl->add_friends();
        fi->set_uid(fuid);
        fi->set_nickname(fuid);
        fi->set_online(users_.find(fuid) != users_.end());
    }
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::handleRecall(const TcpConnectionPtr& conn,
                             const chat::RecallMessage& req)
{
    if (!isAuthenticated(conn))
        return;
    const string& uid = currentUid(conn);
    int64_t msg_id = req.msg_id();

    threadPool_.enqueue([this, conn, uid, msg_id, to = req.to_uid(), room = req.room()]() {
        // 查消息是否存在
        MessageInfo msg = db_.getMessage(msg_id);
        if (!msg.exists)
        {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 8, "message not found");
            });
            return;
        }

        // 校验：只能撤回自己的消息
        if (msg.from_uid != uid)
        {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 9, "can only recall your own messages");
            });
            return;
        }

        // 校验：2 分钟时间窗口
        time_t now = time(nullptr);
        struct tm tm = {};
        sscanf(msg.created_at.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
               &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
               &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
        tm.tm_year -= 1900;
        tm.tm_mon -= 1;
        time_t msgTime = mktime(&tm);
        if (now - msgTime > 120)
        {
            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn]() {
                sendError(conn, 10, "recall window expired (2 min)");
            });
            return;
        }

        // MySQL 标记撤回
        bool recalled = db_.recallMessage(msg_id, uid);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, msg_id, to, room, recalled]() {
            if (!recalled)
            {
                sendError(conn, 11, "recall failed");
                return;
            }

            int64_t recallTime = Timestamp::now().microSecondsSinceEpoch();

            if (!to.empty())
            {
                // 私聊撤回 — 通知对方
                auto it = users_.find(to);
                if (it != users_.end())
                {
                    chat::ServerMessage reply;
                    chat::RecallNotify* rn = reply.mutable_recall_notify();
                    rn->set_msg_id(msg_id);
                    rn->set_from_uid(uid);
                    rn->set_to_uid(to);
                    rn->set_recall_time(recallTime);
                    codec_.sendServerMessage(it->second, reply);
                }
            }
            else if (!room.empty())
            {
                // 群聊撤回 — 广播给房间所有在线成员
                auto rit = rooms_.find(room);
                if (rit != rooms_.end())
                {
                    for (const auto& member : rit->second)
                    {
                        auto uit = users_.find(member);
                        if (uit != users_.end())
                        {
                            chat::ServerMessage reply;
                            chat::RecallNotify* rn = reply.mutable_recall_notify();
                            rn->set_msg_id(msg_id);
                            rn->set_from_uid(uid);
                            rn->set_room(room);
                            rn->set_recall_time(recallTime);
                            codec_.sendServerMessage(uit->second, reply);
                        }
                    }
                }
            }

            // 给发送者回执
            chat::ServerMessage echo;
            chat::RecallNotify* en = echo.mutable_recall_notify();
            en->set_msg_id(msg_id);
            en->set_from_uid(uid);
            if (!to.empty()) en->set_to_uid(to);
            if (!room.empty()) en->set_room(room);
            en->set_recall_time(recallTime);
            codec_.sendServerMessage(conn, echo);
        });
    });
}

void ChatServer::sendError(const TcpConnectionPtr& conn,
                          uint32_t code, const string& reason)
{
    chat::ServerMessage reply;
    reply.mutable_error()->set_code(code);
    reply.mutable_error()->set_reason(reason);
    codec_.sendServerMessage(conn, reply);
}
