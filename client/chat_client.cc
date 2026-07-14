#include "chat.pb.h"

#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "muduo/net/TcpClient.h"
#include "muduo/net/InetAddress.h"
#include "muduo/net/Buffer.h"
#include "muduo/net/Endian.h"
#include "muduo/base/Timestamp.h"

#include <stdio.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <string>

using namespace muduo;
using namespace muduo::net;

static void sendEnvelope(const TcpConnectionPtr& conn, const chat::Envelope& env)
{
    string payload;
    if (!env.SerializeToString(&payload)) return;
    Buffer buf;
    buf.append(payload.data(), payload.size());
    int32_t len = static_cast<int32_t>(payload.size());
    int32_t be32 = sockets::hostToNetwork32(len);
    buf.prepend(&be32, sizeof be32);
    conn->send(&buf);
}

class ChatClient : noncopyable
{
public:
    ChatClient(EventLoop* loop, const InetAddress& serverAddr)
        : loop_(loop),
          client_(loop, serverAddr, "ChatClient")
    {
        client_.setConnectionCallback(
            std::bind(&ChatClient::onConnection, this, _1));
        client_.setMessageCallback(
            std::bind(&ChatClient::onMessage, this, _1, _2, _3));
    }

    void connect() { client_.connect(); }
    void quit() { loop_->quit(); }
    bool connected() const { return conn_ && conn_->connected(); }

    bool isLoggedIn() const { return loggedIn_; }
    const std::string& currentUid() const { return currentUid_; }

    void sendEnvelope(const chat::Envelope& env)
    {
        loop_->runInLoop([this, env]() {
            if (conn_ && conn_->connected())
                ::sendEnvelope(conn_, env);
        });
    }

    void sendRegister(const std::string& uid, const std::string& passwd)
    {
        chat::Envelope env;
        auto req = env.mutable_register_req();
        req->set_uid(uid);
        req->set_passwd(passwd);
        sendEnvelope(env);
    }

    void sendLogin(const std::string& uid, const std::string& passwd)
    {
        chat::Envelope env;
        auto req = env.mutable_login_req();
        req->set_uid(uid);
        req->set_passwd(passwd);
        sendEnvelope(env);
    }

    void sendPrivateMsg(const std::string& to, const std::string& content)
    {
        chat::Envelope env;
        auto msg = env.mutable_chat_msg();
        msg->set_to(to);
        msg->set_content(content);
        sendEnvelope(env);
    }

    void sendRoomMsg(const std::string& room, const std::string& content)
    {
        chat::Envelope env;
        auto msg = env.mutable_chat_msg();
        msg->set_room(room);
        msg->set_content(content);
        sendEnvelope(env);
    }

    void sendCreateRoom(const std::string& name)
    {
        chat::Envelope env;
        auto req = env.mutable_create_room();
        req->set_name(name);
        sendEnvelope(env);
    }

    void sendJoinRoom(const std::string& room, const std::string& uid)
    {
        chat::Envelope env;
        auto req = env.mutable_join_room();
        req->set_room_name(room);
        req->set_uid(uid);
        sendEnvelope(env);
    }

    void sendFriendReq(const std::string& to, const std::string& msg)
    {
        chat::Envelope env;
        auto req = env.mutable_friend_req();
        req->set_to_uid(to);
        req->set_message(msg);
        sendEnvelope(env);
    }

    void sendFriendResp(const std::string& requester, bool accept)
    {
        chat::Envelope env;
        auto resp = env.mutable_friend_resp();
        resp->set_from_uid(currentUid_);   // 响应者（自己）
        resp->set_to_uid(requester);        // 原请求方
        resp->set_accepted(accept);
        sendEnvelope(env);
    }

    void sendRecall(uint64_t msgId, const std::string& toUid, const std::string& room)
    {
        chat::Envelope env;
        auto req = env.mutable_recall_msg();
        req->set_msg_id(msgId);
        req->set_to_uid(toUid);
        req->set_room(room);
        sendEnvelope(env);
    }

    void sendLogout()
    {
        chat::Envelope env;
        auto req = env.mutable_logout_req();
        req->set_uid(currentUid_);
        sendEnvelope(env);
        loggedIn_ = false;
        currentUid_.clear();
    }

private:
    void onConnection(const TcpConnectionPtr& conn)
    {
        LOG_INFO << conn->localAddress().toIpPort() << " -> "
                 << conn->peerAddress().toIpPort() << " is "
                 << (conn->connected() ? "UP" : "DOWN");

        if (conn->connected())
            conn_ = conn;
        else
        {
            conn_.reset();
            loggedIn_ = false;
            currentUid_.clear();
            loop_->quit();
        }
    }

    void onMessage(const TcpConnectionPtr& conn,
                   Buffer* buf,
                   Timestamp receiveTime)
    {
        while (buf->readableBytes() >= 4)
        {
            const void* data = buf->peek();
            int32_t be32 = *static_cast<const int32_t*>(data);
            const int32_t len = sockets::networkToHost32(be32);
            if (len > 65536 || len < 0)
            {
                conn->shutdown();
                break;
            }
            if (buf->readableBytes() >= len + 4)
            {
                buf->retrieve(4);
                string payload(buf->peek(), len);
                buf->retrieve(len);

                chat::ServerMessage msg;
                if (!msg.ParseFromArray(payload.data(), payload.size()))
                {
                    LOG_ERROR << "Failed to parse ServerMessage";
                    continue;
                }

                switch (msg.payload_case())
                {
                    case chat::ServerMessage::kLoginResp:
                        printf("\n[login] ok=%d token=%s reason=%s\n",
                               msg.login_resp().ok(),
                               msg.login_resp().token().c_str(),
                               msg.login_resp().reason().c_str());
                        if (msg.login_resp().ok())
                        {
                            loggedIn_ = true;
                        }
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kRegisterResp:
                        printf("\n[register] ok=%d reason=%s\n",
                               msg.register_resp().ok(),
                               msg.register_resp().reason().c_str());
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kChatMsg:
                        if (!msg.chat_msg().room().empty())
                            printf("\n[room:%s] [id=%lu] %s: %s\n",
                                   msg.chat_msg().room().c_str(),
                                   msg.chat_msg().msg_id(),
                                   msg.chat_msg().from().c_str(),
                                   msg.chat_msg().content().c_str());
                        else
                            printf("\n[private] [id=%lu] %s -> me: %s\n",
                                   msg.chat_msg().msg_id(),
                                   msg.chat_msg().from().c_str(),
                                   msg.chat_msg().content().c_str());
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kFriendReq:
                        printf("\n[friend] request from: %s msg: %s\n",
                               msg.friend_req().from_uid().c_str(),
                               msg.friend_req().message().c_str());
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kFriendResp:
                        printf("\n[friend] response from: %s accepted: %d\n",
                               msg.friend_resp().from_uid().c_str(),
                               msg.friend_resp().accepted());
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kRecallNotify:
                        if (!msg.recall_notify().room().empty())
                            printf("\n[recall] [id=%lu] %s 在房间 %s 撤回了一条消息\n",
                                   msg.recall_notify().msg_id(),
                                   msg.recall_notify().from_uid().c_str(),
                                   msg.recall_notify().room().c_str());
                        else
                            printf("\n[recall] [id=%lu] %s 撤回了一条私聊消息\n",
                                   msg.recall_notify().msg_id(),
                                   msg.recall_notify().from_uid().c_str());
                        printf("> ");
                        fflush(stdout);
                        break;
                    case chat::ServerMessage::kError:
                        printf("\n[error] code=%d reason=%s\n",
                               msg.error().code(),
                               msg.error().reason().c_str());
                        printf("> ");
                        fflush(stdout);
                        break;
                    default:
                        printf("\n[unknown server message]\n");
                        printf("> ");
                        fflush(stdout);
                        break;
                }
            }
            else
                break;
        }
    }

    EventLoop* loop_;
    TcpClient client_;
    TcpConnectionPtr conn_;
    std::atomic<bool> loggedIn_{false};
    std::string currentUid_;
};

static std::string readLine(const char* prompt)
{
    printf("%s", prompt);
    fflush(stdout);
    char line[1024] = {0};
    if (fgets(line, sizeof line, stdin))
    {
        size_t len = strlen(line);
        if (len > 0 && line[len-1] == '\n')
            line[len-1] = '\0';
        return line;
    }
    return "";
}

int main(int argc, char* argv[])
{
    if (argc < 3)
    {
        printf("Usage: %s host port\n", argv[0]);
        return 0;
    }

    EventLoop loop;
    InetAddress serverAddr(argv[1], atoi(argv[2]));
    ChatClient client(&loop, serverAddr);
    client.connect();

    std::thread stdinThread([&client, &loop]() {
        // 等待连接建立
        while (!client.connected())
            usleep(10000);

        bool running = true;
        while (running)
        {
            if (!client.isLoggedIn())
            {
                // ========== 认证阶段 ==========
                printf("\n");
                printf("╔══════════════════════════════╗\n");
                printf("║        欢迎使用聊天系统      ║\n");
                printf("╠══════════════════════════════╣\n");
                printf("║  1. 登录                     ║\n");
                printf("║  2. 注册                     ║\n");
                printf("║  3. 退出                     ║\n");
                printf("╚══════════════════════════════╝\n");

                std::string choice = readLine("请选择: ");

                if (choice == "1")
                {
                    std::string uid = readLine("用户ID: ");
                    std::string pwd = readLine("密码:   ");
                    if (uid.empty() || pwd.empty())
                    {
                        printf("用户ID和密码不能为空\n");
                        continue;
                    }
                    client.sendLogin(uid, pwd);
                    usleep(200000); // 等待服务器响应
                }
                else if (choice == "2")
                {
                    std::string uid = readLine("用户ID: ");
                    std::string pwd = readLine("密码:   ");
                    if (uid.empty() || pwd.empty())
                    {
                        printf("用户ID和密码不能为空\n");
                        continue;
                    }
                    client.sendRegister(uid, pwd);
                    usleep(200000); // 等待服务器响应
                }
                else if (choice == "3")
                {
                    client.quit();
                    running = false;
                }
                else
                {
                    printf("无效选择，请重试\n");
                }
            }
            else
            {
                // ========== 主功能阶段 ==========
                printf("\n");
                printf("╔══════════════════════════════╗\n");
                printf("║        功能菜单              ║\n");
                printf("╠══════════════════════════════╣\n");
                printf("║  1. 私聊                     ║\n");
                printf("║  2. 群聊                     ║\n");
                printf("║  3. 创建群组                 ║\n");
                printf("║  4. 加入群组                 ║\n");
                printf("║  5. 添加好友                 ║\n");
                printf("║  6. 好友回复                 ║\n");
                printf("║  7. 撤回消息                 ║\n");
                printf("║  8. 登出                     ║\n");
                printf("╚══════════════════════════════╝\n");

                std::string choice = readLine("请选择: ");

                if (choice == "1")
                {
                    std::string to = readLine("发送给谁: ");
                    std::string content = readLine("消息内容: ");
                    if (to.empty() || content.empty())
                    {
                        printf("用户ID和内容不能为空\n");
                        continue;
                    }
                    client.sendPrivateMsg(to, content);
                }
                else if (choice == "2")
                {
                    std::string room = readLine("群组名称: ");
                    std::string content = readLine("消息内容: ");
                    if (room.empty() || content.empty())
                    {
                        printf("群组名和内容不能为空\n");
                        continue;
                    }
                    client.sendRoomMsg(room, content);
                }
                else if (choice == "3")
                {
                    std::string name = readLine("群组名称: ");
                    if (name.empty())
                    {
                        printf("群组名不能为空\n");
                        continue;
                    }
                    client.sendCreateRoom(name);
                }
                else if (choice == "4")
                {
                    std::string room = readLine("群组名称: ");
                    std::string uid = readLine("你的ID: ");
                    if (room.empty() || uid.empty())
                    {
                        printf("群组名和ID不能为空\n");
                        continue;
                    }
                    client.sendJoinRoom(room, uid);
                }
                else if (choice == "5")
                {
                    std::string to = readLine("好友ID: ");
                    std::string msg = readLine("验证消息: ");
                    if (to.empty())
                    {
                        printf("好友ID不能为空\n");
                        continue;
                    }
                    client.sendFriendReq(to, msg);
                }
                else if (choice == "6")
                {
                    std::string from = readLine("对方ID (请求方的ID): ");
                    std::string accept = readLine("同意? (1=是/0=否): ");
                    if (from.empty())
                    {
                        printf("ID不能为空\n");
                        continue;
                    }
                    client.sendFriendResp(from, accept == "1");
                }
                else if (choice == "7")
                {
                    std::string idStr = readLine("消息ID: ");
                    std::string to = readLine("对方ID (私聊) 或留空: ");
                    std::string room = readLine("群组名 (群聊) 或留空: ");
                    if (idStr.empty())
                    {
                        printf("消息ID不能为空\n");
                        continue;
                    }
                    client.sendRecall(strtoull(idStr.c_str(), NULL, 10), to, room);
                }
                else if (choice == "8")
                {
                    client.sendLogout();
                    printf("已登出\n");
                }
                else
                {
                    printf("无效选择，请重试\n");
                }
            }
        }
    });

    loop.loop();
    stdinThread.join();
    return 0;
}
