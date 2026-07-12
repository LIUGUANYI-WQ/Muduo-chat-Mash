#!/bin/bash

HOST="${1:-127.0.0.1}"
PORT="${2:-8000}"
PROJECT=/mnt/c/Users/94173/Desktop/task
CLIENT="$PROJECT/build_chat/client/chat_client"

if [ ! -x "$CLIENT" ]; then
    echo "客户端未编译，请先跑 ./build.sh"
    exit 1
fi

echo "==> 启动客户端，连接 $HOST:$PORT"
exec "$CLIENT" "$HOST" "$PORT"
