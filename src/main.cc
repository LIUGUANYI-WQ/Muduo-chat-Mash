#include "src/chat_server.h"
#include "muduo/base/Logging.h"
#include "muduo/net/EventLoop.h"
#include "src/db.h"
#include "src/redis_pool.h"
#include "src/repository/mysql_user_repo.h"
#include "src/repository/mysql_friend_repo.h"
#include "src/repository/mysql_message_repo.h"
#include "src/repository/mysql_room_repo.h"
#include "src/repository/mysql_session_repo.h"
#include "src/repository/redis_session_repo.h"
#include "src/repository/redis_pubsub.h"
#include "src/service/login_service.h"
#include "src/service/chat_service.h"
#include "src/service/friend_service.h"
#include "src/service/room_service.h"

#include <stdio.h>
#include <unistd.h>
#include <memory>

int main(int argc, char* argv[])
{
    LOG_INFO << "pid = " << getpid();
    if (argc > 2)
    {
        MySQLPool db;
        db.init("localhost", "root", "123456", "chat", 3306, 16);

        RedisPool redisPool;
        redisPool.init("localhost", 6379, "", 0);

        RedisPubSub pubsub;
        pubsub.init("localhost", 6379, "", 0);
        pubsub.start();

        MySQLUserRepo userRepo(db);
        MySQLFriendRepo friendRepo(db);
        MySQLMessageRepo msgRepo(db);
        MySQLRoomRepo roomRepo(db);
        MySQLSessionRepo sqlSessionRepo(db);
        RedisSessionRepo redisSessionRepo(redisPool);

        LoginService loginService(userRepo, redisSessionRepo, friendRepo, msgRepo);
        ChatService chatService(msgRepo, redisSessionRepo, &pubsub);
        FriendService friendService(friendRepo, userRepo, &pubsub);
        RoomService roomService(roomRepo, userRepo, &pubsub);

        muduo::net::EventLoop loop;
        muduo::net::InetAddress listenAddr(argv[1], static_cast<uint16_t>(atoi(argv[2])));
        ChatServer server(&loop, listenAddr, loginService, chatService, friendService, roomService, "node-1");
        server.setThreadNum(4);
        server.start();
        loop.loop();

        pubsub.stop();
    }
    else
    {
        printf("Usage: %s ip port\n", argv[0]);
    }
    return 0;
}
