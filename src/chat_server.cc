#include "chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/base/Types.h"
#include <functional>

using namespace muduo;
using namespace muduo::net;

ChatServer::ChatServer(EventLoop* loop, const InetAddress& listenAddr,
                       LoginService& loginService, ChatService& chatService,
                       FriendService& friendService, RoomService& roomService,
                       const std::string& node_id)
    : server_(loop, listenAddr, "ChatServer"),
      codec_(std::bind(&ChatServer::onEnvelope, this, _1, _2, _3)),
      loop_(loop),
      loginService_(loginService),
      chatService_(chatService),
      friendService_(friendService),
      roomService_(roomService),
      node_id_(node_id),
      threadPool_("ChatThreadPool") {
    threadPool_.setMaxQueueSize(10000);
    threadPool_.start(std::thread::hardware_concurrency());

    server_.setConnectionCallback(
        std::bind(&ChatServer::onConnection, this, _1));
    server_.setMessageCallback(
        std::bind(&ChatCodec::onMessage, &codec_, _1, _2, _3));
}

void ChatServer::start() {
    server_.start();
}

void ChatServer::onConnection(const TcpConnectionPtr& conn) {
    if (conn->connected()) {
        sessionManager_.onConnection(conn);
    } else {
        sessionManager_.onDisconnection(conn);
    }
}

void ChatServer::onEnvelope(const TcpConnectionPtr& conn,
                            const chat::Envelope& env,
                            Timestamp receiveTime) {
    switch (env.payload_case()) {
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

void ChatServer::handleLogin(const TcpConnectionPtr& conn,
                            const chat::LoginRequest& req) {
    if (req.token().empty() && req.passwd().empty()) {
        sendError(conn, 1, "need password or token");
        return;
    }

    if (!req.token().empty()) {
        threadPool_.run([this, conn, token = req.token()]() {
            auto result = loginService_.loginWithToken(token, node_id_);

            auto* ioLoop = conn->getLoop();
            ioLoop->runInLoop([this, conn, result, token]() {
                if (result.success) {
                    sessionManager_.authenticate(conn, result.userInfo.uid, token);

                    for (const auto& f : result.friends)
                        sessionManager_.addFriendship(result.userInfo.uid, f.uid);

                    chat::ServerMessage reply;
                    reply.mutable_login_resp()->set_ok(true);
                    reply.mutable_login_resp()->set_token(token);
                    reply.mutable_login_resp()->set_nickname(result.userInfo.nickname);
                    reply.mutable_login_resp()->set_email(result.userInfo.email);
                    reply.mutable_login_resp()->set_avatar_url(result.userInfo.avatarUrl);
                    reply.mutable_login_resp()->set_server_time(
                        Timestamp::now().microSecondsSinceEpoch());
                    codec_.sendServerMessage(conn, reply);

                    sendPendingRequests(conn, result.pendingRequests);
                    sendOfflineMessages(conn, result.offlineMessages);
                    sendFriendList(conn);
                } else {
                    sendError(conn, 6, result.reason);
                }
            });
        });
        return;
    }

    std::string uid = req.uid();
    if (uid.empty()) {
        sendError(conn, 2, "uid required");
        return;
    }

    threadPool_.run([this, conn, uid, passwd = req.passwd()]() {
        auto result = loginService_.login(uid, passwd, node_id_);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, result]() {
            if (result.success) {
                sessionManager_.authenticate(conn, result.userInfo.uid, result.token);

                for (const auto& f : result.friends)
                    sessionManager_.addFriendship(result.userInfo.uid, f.uid);

                chat::ServerMessage reply;
                reply.mutable_login_resp()->set_ok(true);
                reply.mutable_login_resp()->set_token(result.token);
                reply.mutable_login_resp()->set_nickname(result.userInfo.nickname);
                reply.mutable_login_resp()->set_email(result.userInfo.email);
                reply.mutable_login_resp()->set_avatar_url(result.userInfo.avatarUrl);
                reply.mutable_login_resp()->set_server_time(
                    Timestamp::now().microSecondsSinceEpoch());
                codec_.sendServerMessage(conn, reply);

                sendPendingRequests(conn, result.pendingRequests);
                sendOfflineMessages(conn, result.offlineMessages);
                sendFriendList(conn);
            } else {
                sendError(conn, 6, result.reason);
            }
        });
    });
}

void ChatServer::handleLogout(const TcpConnectionPtr& conn,
                             const chat::LogoutRequest& req) {
    std::string uid = req.uid();
    std::string token = req.token();

    threadPool_.run([this, uid, token]() {
        loginService_.logout(uid, token);
    });

    sessionManager_.deauthenticate(conn);

    chat::ServerMessage reply;
    reply.mutable_login_resp()->set_ok(true);
    reply.mutable_login_resp()->set_reason("logged out");
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::handleRegister(const TcpConnectionPtr& conn,
                               const chat::RegisterRequest& req) {
    if (req.uid().empty() || req.passwd().empty()) {
        sendError(conn, 7, "uid and password required");
        return;
    }

    std::string uid = req.uid();
    std::string passwd = req.passwd();
    std::string nickname = req.nickname();
    std::string email = req.email();

    threadPool_.run([this, conn, uid, passwd, nickname, email]() {
        bool ok = loginService_.registerUser(uid, passwd, nickname, email);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, ok]() {
            chat::ServerMessage reply;
            reply.mutable_register_resp()->set_ok(ok);
            if (!ok) reply.mutable_register_resp()->set_reason("register failed");
            codec_.sendServerMessage(conn, reply);
        });
    });
}

void ChatServer::handleChatMessage(const TcpConnectionPtr& conn,
                                  const chat::ChatMessage& msg) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& from = sessionManager_.getUid(conn);

    threadPool_.run([this, conn, from, to = msg.to(), room = msg.room(), content = msg.content()]() {
        auto result = chatService_.sendMessage(from, to, room, content);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, from, to, room, content, result]() {
            if (!result.success) {
                sendError(conn, 3, result.reason);
                return;
            }

            int64_t now = Timestamp::now().microSecondsSinceEpoch();

            if (!to.empty()) {
                auto targetConn = sessionManager_.getConnection(to);
                bool offline = !targetConn;

                chat::ServerMessage reply;
                chat::ChatMessage* cm = reply.mutable_chat_msg();
                cm->set_from(from);
                cm->set_to(to);
                cm->set_content(content);
                cm->set_msg_id(result.msg_id);
                cm->set_timestamp(now);
                if (!offline)
                    codec_.sendServerMessage(targetConn, reply);

                chat::ServerMessage echo;
                chat::ChatMessage* em = echo.mutable_chat_msg();
                em->set_from(from);
                em->set_to(to);
                em->set_content(content);
                em->set_msg_id(result.msg_id);
                em->set_timestamp(now);
                codec_.sendServerMessage(conn, echo);

                if (offline) {
                    sendError(conn, 3, "user offline");
                    threadPool_.run([this, to, msg_id = result.msg_id]() {
                        chatService_.getMessage(msg_id);
                    });
                }
            } else if (!room.empty()) {
                const auto& members = roomManager_.getRoomMembers(room);
                for (const auto& member : members) {
                    auto memberConn = sessionManager_.getConnection(member);
                    if (memberConn) {
                        chat::ServerMessage reply;
                        chat::ChatMessage* cm = reply.mutable_chat_msg();
                        cm->set_from(from);
                        cm->set_room(room);
                        cm->set_content(content);
                        cm->set_msg_id(result.msg_id);
                        cm->set_timestamp(now);
                        codec_.sendServerMessage(memberConn, reply);
                    }
                }
            }
        });
    });
}

void ChatServer::handleCreateRoom(const TcpConnectionPtr& conn,
                                 const chat::CreateRoom& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& uid = sessionManager_.getUid(conn);

    threadPool_.run([this, conn, uid, name = req.name()]() {
        auto result = roomService_.createRoom(name, uid);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, name, result]() {
            if (!result.success) {
                sendError(conn, 5, result.reason);
                return;
            }
            roomManager_.createRoom(name, uid);
        });
    });
}

void ChatServer::handleJoinRoom(const TcpConnectionPtr& conn,
                               const chat::JoinRoom& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& uid = sessionManager_.getUid(conn);

    threadPool_.run([this, conn, uid, roomName = req.room_name()]() {
        auto result = roomService_.joinRoom(roomName, uid);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, roomName, result]() {
            if (!result.success) {
                sendError(conn, 4, result.reason);
                return;
            }
            roomManager_.joinRoom(roomName, uid);
        });
    });
}

void ChatServer::handleFriendRequest(const TcpConnectionPtr& conn,
                                   const chat::FriendRequest& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& uid = sessionManager_.getUid(conn);

    threadPool_.run([this, conn, from = req.from_uid(), to = req.to_uid(), msg = req.message()]() {
        friendService_.addFriendRequest(from, to, msg);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, from, to, msg]() {
            sessionManager_.addPendingRequest(from, to);

            auto targetConn = sessionManager_.getConnection(to);
            if (targetConn) {
                chat::ServerMessage reply;
                chat::FriendRequest* fr = reply.mutable_friend_req();
                fr->set_from_uid(from);
                fr->set_message(msg);
                codec_.sendServerMessage(targetConn, reply);
            }
        });
    });
}

void ChatServer::handleFriendResponse(const TcpConnectionPtr& conn,
                                      const chat::FriendResponse& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    std::string responder = req.from_uid();
    std::string requester = req.to_uid();

    threadPool_.run([this, conn, requester, responder, accepted = req.accepted()]() {
        friendService_.respondFriendRequest(requester, responder, accepted);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, requester, responder, accepted]() {
            if (accepted) {
                sessionManager_.addFriendship(requester, responder);
            }
            sessionManager_.removePendingRequest(requester, responder);

            auto requesterConn = sessionManager_.getConnection(requester);
            if (requesterConn) {
                chat::ServerMessage reply;
                chat::FriendResponse* fr = reply.mutable_friend_resp();
                fr->set_from_uid(responder);
                fr->set_to_uid(requester);
                fr->set_accepted(accepted);
                codec_.sendServerMessage(requesterConn, reply);
                sendFriendList(requesterConn);
            }

            sendFriendList(conn);
        });
    });
}

void ChatServer::handleFriendRemove(const TcpConnectionPtr& conn,
                                   const chat::FriendRemove& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& uid = sessionManager_.getUid(conn);

    threadPool_.run([this, conn, uid, target = req.target_uid()]() {
        friendService_.removeFriend(uid, target);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, target]() {
            sessionManager_.removeFriendship(uid, target);

            auto targetConn = sessionManager_.getConnection(target);
            if (targetConn) {
                chat::ServerMessage reply;
                chat::FriendRemove* fr = reply.mutable_friend_remove();
                fr->set_target_uid(uid);
                codec_.sendServerMessage(targetConn, reply);
                sendFriendList(targetConn);
            }
            sendFriendList(conn);
        });
    });
}

void ChatServer::handleFriendList(const TcpConnectionPtr& conn) {
    sendFriendList(conn);
}

void ChatServer::sendFriendList(const TcpConnectionPtr& conn) {
    const std::string& uid = sessionManager_.getUid(conn);
    const auto& friends = sessionManager_.getFriends(uid);

    chat::ServerMessage reply;
    chat::FriendList* fl = reply.mutable_friend_list();
    for (const auto& fuid : friends) {
        chat::FriendInfo* fi = fl->add_friends();
        fi->set_uid(fuid);
        fi->set_nickname(fuid);
        fi->set_online(sessionManager_.isOnline(fuid));
    }
    codec_.sendServerMessage(conn, reply);
}

void ChatServer::sendOfflineMessages(const TcpConnectionPtr& conn,
                                     const std::vector<MessageInfo>& messages) {
    for (const auto& om : messages) {
        chat::ServerMessage reply;
        chat::ChatMessage* cm = reply.mutable_chat_msg();
        cm->set_from(om.from_uid);
        if (!om.to_uid.empty()) cm->set_to(om.to_uid);
        if (!om.room_name.empty()) cm->set_room(om.room_name);
        cm->set_content(om.content);
        cm->set_msg_id(om.msg_id);
        cm->set_timestamp(Timestamp::now().microSecondsSinceEpoch());
        codec_.sendServerMessage(conn, reply);
    }
    if (!messages.empty()) {
        const std::string& uid = sessionManager_.getUid(conn);
        threadPool_.run([this, uid]() {
            chatService_.markMessagesDelivered(uid);
        });
    }
}

void ChatServer::sendPendingRequests(const TcpConnectionPtr& conn,
                                     const std::vector<std::string>& requests) {
    const std::string& uid = sessionManager_.getUid(conn);
    for (const auto& requester : requests) {
        sessionManager_.addPendingRequest(requester, uid);
        chat::ServerMessage reply;
        chat::FriendRequest* fr = reply.mutable_friend_req();
        fr->set_from_uid(requester);
        fr->set_to_uid(uid);
        codec_.sendServerMessage(conn, reply);
    }
}

void ChatServer::handleRecall(const TcpConnectionPtr& conn,
                             const chat::RecallMessage& req) {
    if (!sessionManager_.isAuthenticated(conn))
        return;

    const std::string& uid = sessionManager_.getUid(conn);
    int64_t msg_id = req.msg_id();

    threadPool_.run([this, conn, uid, msg_id, to = req.to_uid(), room = req.room()]() {
        auto result = chatService_.recallMessage(msg_id, uid, to, room);

        auto* ioLoop = conn->getLoop();
        ioLoop->runInLoop([this, conn, uid, msg_id, to, room, result]() {
            if (!result.success) {
                sendError(conn, 8, result.reason);
                return;
            }

            int64_t recallTime = Timestamp::now().microSecondsSinceEpoch();

            if (!to.empty()) {
                auto targetConn = sessionManager_.getConnection(to);
                if (targetConn) {
                    chat::ServerMessage reply;
                    chat::RecallNotify* rn = reply.mutable_recall_notify();
                    rn->set_msg_id(msg_id);
                    rn->set_from_uid(uid);
                    rn->set_to_uid(to);
                    rn->set_recall_time(recallTime);
                    codec_.sendServerMessage(targetConn, reply);
                }
            } else if (!room.empty()) {
                const auto& members = roomManager_.getRoomMembers(room);
                for (const auto& member : members) {
                    auto memberConn = sessionManager_.getConnection(member);
                    if (memberConn) {
                        chat::ServerMessage reply;
                        chat::RecallNotify* rn = reply.mutable_recall_notify();
                        rn->set_msg_id(msg_id);
                        rn->set_from_uid(uid);
                        rn->set_room(room);
                        rn->set_recall_time(recallTime);
                        codec_.sendServerMessage(memberConn, reply);
                    }
                }
            }

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
                          uint32_t code, const string& reason) {
    chat::ServerMessage reply;
    reply.mutable_error()->set_code(code);
    reply.mutable_error()->set_reason(reason);
    codec_.sendServerMessage(conn, reply);
}
