#!/bin/bash

PORT="${1:-8000}"
PROJECT=/mnt/c/Users/94173/Desktop/task
SERVER="$PROJECT/build_chat/src/chat_server"

if [ ! -x "$SERVER" ]; then
    echo "服务端未编译，请先跑 ./build.sh"
    exit 1
fi

export MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
export MYSQL_USER="${MYSQL_USER:-root}"
export MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
export MYSQL_DATABASE="${MYSQL_DATABASE:-chat}"

echo "==> 启动服务端，监听端口 $PORT (MySQL: $MYSQL_USER@$MYSQL_HOST/$MYSQL_DATABASE)"
exec "$SERVER" "$PORT"
