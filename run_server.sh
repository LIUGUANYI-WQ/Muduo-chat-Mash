#!/bin/bash

PORT="${1:-8000}"
BIND_IP="${2:-0.0.0.0}"  # 默认 0.0.0.0，Windows 可用 WSL2 IP 直连绕过 localhost 转发
PROJECT=/mnt/c/Users/94173/Desktop/task
SERVER="$PROJECT/build/src/chat_server"

if [ ! -x "$SERVER" ]; then
    SERVER="$PROJECT/build_chat/src/chat_server"
    if [ ! -x "$SERVER" ]; then
        echo "服务端未编译，请先跑 ./build.sh"
        exit 1
    fi
fi

export MYSQL_HOST="${MYSQL_HOST:-127.0.0.1}"
export MYSQL_USER="${MYSQL_USER:-root}"
export MYSQL_PASSWORD="${MYSQL_PASSWORD:-123456}"
export MYSQL_DATABASE="${MYSQL_DATABASE:-chat}"
export MYSQL_POOL_SIZE="${MYSQL_POOL_SIZE:-8}"
export REDIS_HOST="${REDIS_HOST:-127.0.0.1}"
export REDIS_PORT="${REDIS_PORT:-6379}"

# 如果绑定到 0.0.0.0，输出 WSL2 IP 供 Windows 直连
WSL2_IP=$(hostname -I 2>/dev/null | awk '{print $1}')
if [ -n "$WSL2_IP" ]; then
    echo "==> Windows 直连地址: $WSL2_IP:$PORT (比 127.0.0.1 快 ~40ms，绕过 WSL2 端口转发)"
fi

echo "==> 启动服务端，监听 $BIND_IP:$PORT (MySQL: $MYSQL_USER@$MYSQL_HOST/$MYSQL_DATABASE, pool=$MYSQL_POOL_SIZE, Redis: $REDIS_HOST:$REDIS_PORT)"
exec "$SERVER" "$BIND_IP" "$PORT"
