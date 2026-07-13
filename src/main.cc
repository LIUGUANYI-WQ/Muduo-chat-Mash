#include "src/chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"

#include <stdio.h>
#include <unistd.h>

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 2)
    {
        muduo::net::EventLoop loop;
        muduo::net::InetAddress listenAddr(argv[1], static_cast<uint16_t>(atoi(argv[2])));
        ChatServer server(&loop, listenAddr);
        server.setThreadNum(4);  // 4个I/O线程（1 main + 3 sub）
        server.start();
        loop.loop();
    }
    else
    {
        printf("Usage: %s ip port\n", argv[0]);
    }
    return 0;
}
