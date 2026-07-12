#!/bin/bash
set -e

PROJECT=/mnt/c/Users/94173/Desktop/task
MUDUO_SRC=$PROJECT/muduo
MUDUO_BUILD=$HOME/muduo_build/release-cpp11
BUILD=$PROJECT/build_chat

# 1. muduo 库（放到 WSL 原生 fs 编译，避开 DrvFs；不存在才编）
if [ ! -d "$MUDUO_BUILD/lib" ]; then
    echo "==> 编译 muduo 库（原生 fs: $MUDUO_BUILD）"
    mkdir -p "$MUDUO_BUILD"
    cd "$MUDUO_BUILD"
    cmake -DCMAKE_BUILD_TYPE=release "$MUDUO_SRC"
    make -j"$(nproc)"
fi

# 2. 生成 protobuf（DrvFs 缓存坑：每次重跑最稳）
echo "==> 生成 protobuf"
cd "$PROJECT/proto"
protoc --cpp_out=. chat.proto

# 3. 编译我们的项目
echo "==> 编译 chat 项目"
mkdir -p "$BUILD"
cd "$BUILD"
cmake .. -DMUDUO_DIR="$MUDUO_BUILD" >/dev/null
make -j"$(nproc)"

echo "==> 编译完成"
echo "    服务端: $BUILD/src/chat_server"
echo "    客户端: $BUILD/client/chat_client"
