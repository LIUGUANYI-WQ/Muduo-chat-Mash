# Muduo-chat-Mash

基于 muduo + Protobuf 的多人聊天服务器（Step 2：单节点）。

## 构建依赖
- [muduo](https://github.com/chenshuo/muduo) 网络库（放在仓库根目录 `muduo/`，编译产物位于 `build2/release-cpp11`）
- protobuf 3.21+（协议文件 `proto/chat.proto`，首次构建需 `protoc` 生成 `chat.pb.cc/.h`，已被 .gitignore 忽略）
- C++17 编译器

## 构建
```bash
mkdir -p build && cd build
cmake .. -DMUDUO_DIR=../build2/release-cpp11
make
```
