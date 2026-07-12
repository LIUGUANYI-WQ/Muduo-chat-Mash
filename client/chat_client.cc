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
    bool connected() const { return conn_ && conn_->connected(); }

    // 所有连接访问都在 EventLoop 线程，线程安全
    void sendEnvelope(const chat::Envelope& env)
    {
        loop_->runInLoop([this, env]() {
            if (conn_ && conn_->connected())
                ::sendEnvelope(conn_, env);
        });
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
                        printf("[login] ok=%d token=%s reason=%s\n",
                               msg.login_resp().ok(),
                               msg.login_resp().token().c_str(),
                               msg.login_resp().reason().c_str());
                        break;
                    case chat::ServerMessage::kChatMsg:
                        if (!msg.chat_msg().room().empty())
                            printf("[room:%s] %s: %s\n",
                                   msg.chat_msg().room().c_str(),
                                   msg.chat_msg().from().c_str(),
                                   msg.chat_msg().content().c_str());
                        else
                            printf("[private] %s -> me: %s\n",
                                   msg.chat_msg().from().c_str(),
                                   msg.chat_msg().content().c_str());
                        break;
                    case chat::ServerMessage::kFriendReq:
                        printf("[friend] request from: %s msg: %s\n",
                               msg.friend_req().from_uid().c_str(),
                               msg.friend_req().message().c_str());
                        break;
                    case chat::ServerMessage::kFriendResp:
                        printf("[friend] response from: %s accepted: %d\n",
                               msg.friend_resp().from_uid().c_str(),
                               msg.friend_resp().accepted());
                        break;
                    case chat::ServerMessage::kRecallNotify:
                        printf("[recall] msg_id=%lu from=%s\n",
                               msg.recall_notify().msg_id(),
                               msg.recall_notify().from_uid().c_str());
                        break;
                    case chat::ServerMessage::kError:
                        printf("[error] code=%d reason=%s\n",
                               msg.error().code(),
                               msg.error().reason().c_str());
                        break;
                    default:
                        printf("[unknown server message]\n");
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
};

static void printHelp()
{
    printf("Commands:\n");
    printf("  login <uid> [passwd]       login\n");
    printf("  msg <to> <content>         private message\n");
    printf("  room <name> <content>      room message\n");
    printf("  createroom <name>          create room\n");
    printf("  joinroom <room> <uid>      join room\n");
    printf("  friendreq <uid> [msg]     friend request\n");
    printf("  friendresp <uid> <to> <0|1>  friend response\n");
    printf("  recall <msg_id>            recall message\n");
    printf("  quit                       exit\n");
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

    // EventLoop 必须在独立线程跑起来，TcpClient 的连接/收发都依赖它
    std::thread ioThread([&loop]() { loop.loop(); });

    printf("Connecting...\n");
    Timestamp start = Timestamp::now();
    while (!client.connected())
    {
        Timestamp now = Timestamp::now();
        if (now.microSecondsSinceEpoch() - start.microSecondsSinceEpoch() > 5 * 1000000)
            break;
        usleep(10000);
    }
    if (!client.connected())
    {
        printf("Connection timeout\n");
        loop.quit();
        ioThread.join();
        return 1;
    }
    printf("Connected.\n");
    printHelp();

    char line[1024];
    while (fgets(line, sizeof line, stdin))
    {
        char cmd[256] = {0};
        char rest[1024] = {0};
        if (sscanf(line, "%255s %1023[^\n]", cmd, rest) < 1)
            continue;

        chat::Envelope env;

        if (strcmp(cmd, "login") == 0)
        {
            char uid[256] = {0};
            char pwd[256] = {0};
            sscanf(rest, "%255s %255s", uid, pwd);
            auto req = env.mutable_login_req();
            req->set_uid(uid);
            req->set_passwd(pwd);
        }
        else if (strcmp(cmd, "msg") == 0)
        {
            char to[256] = {0};
            char content[1024] = {0};
            sscanf(rest, "%255s %1023[^\n]", to, content);
            auto msg = env.mutable_chat_msg();
            msg->set_to(to);
            msg->set_content(content);
        }
        else if (strcmp(cmd, "room") == 0)
        {
            char name[256] = {0};
            char content[1024] = {0};
            sscanf(rest, "%255s %1023[^\n]", name, content);
            auto msg = env.mutable_chat_msg();
            msg->set_room(name);
            msg->set_content(content);
        }
        else if (strcmp(cmd, "createroom") == 0)
        {
            char name[256] = {0};
            sscanf(rest, "%255s", name);
            auto req = env.mutable_create_room();
            req->set_name(name);
        }
        else if (strcmp(cmd, "joinroom") == 0)
        {
            char room[256] = {0};
            char uid[256] = {0};
            sscanf(rest, "%255s %255s", room, uid);
            auto req = env.mutable_join_room();
            req->set_room_name(room);
            req->set_uid(uid);
        }
        else if (strcmp(cmd, "friendreq") == 0)
        {
            char to[256] = {0};
            char msg[1024] = {0};
            sscanf(rest, "%255s %1023[^\n]", to, msg);
            auto req = env.mutable_friend_req();
            req->set_to_uid(to);
            req->set_message(msg);
        }
        else if (strcmp(cmd, "friendresp") == 0)
        {
            char from[256] = {0};
            char to[256] = {0};
            int acc = 0;
            sscanf(rest, "%255s %255s %d", from, to, &acc);
            auto resp = env.mutable_friend_resp();
            resp->set_from_uid(from);
            resp->set_to_uid(to);
            resp->set_accepted(acc != 0);
        }
        else if (strcmp(cmd, "recall") == 0)
        {
            char id[256] = {0};
            sscanf(rest, "%255s", id);
            auto req = env.mutable_recall_msg();
            req->set_msg_id(strtoull(id, NULL, 10));
        }
        else if (strcmp(cmd, "quit") == 0)
        {
            loop.quit();
            break;
        }
        else
        {
            printHelp();
            continue;
        }

        client.sendEnvelope(env);
    }

    if (ioThread.joinable())
        ioThread.join();
}
