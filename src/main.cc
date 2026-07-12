#include "src/chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 1)
    {
        muduo::net::EventLoop loop;
        uint16_t port = static_cast<uint16_t>(atoi(argv[1]));
        muduo::net::InetAddress listenAddr(port);
        ChatServer server(&loop, listenAddr);
        server.setThreadNum(4);  // 4个I/O线程（1 main + 3 sub）
        server.start();
        loop.loop();
    }
    else
    {
        printf("Usage: %s port\n", argv[0]);
    }
    return 0;
}
