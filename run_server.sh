#!/bin/bash

PORT="${1:-8000}"
PROJECT=/mnt/c/Users/94173/Desktop/task
SERVER="$PROJECT/build_chat/src/chat_server"

if [ ! -x "$SERVER" ]; then
    echo "服务端未编译，请先跑 ./build.sh"
    exit 1
fi

echo "==> 启动服务端，监听端口 $PORT"
exec "$SERVER" "$PORT"
