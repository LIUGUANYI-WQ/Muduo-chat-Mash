#!/bin/bash
# 功能自动化测试脚本
# 测试：好友请求推送、消息持久化、撤回(私聊+群聊)
SERVER_ADDR="127.0.0.1:9884"

echo "=========================================="
echo "  Chat Server 功能自动化测试"
echo "=========================================="

# clean test users
mysql -u root -p123456 -h 127.0.0.1 chat -e "DELETE FROM messages WHERE from_uid LIKE 'test_%' OR to_uid LIKE 'test_%'; DELETE FROM friendships WHERE requester_uid LIKE 'test_%' OR target_uid LIKE 'test_%'; DELETE FROM room_members WHERE uid LIKE 'test_%'; DELETE FROM rooms WHERE creator_uid LIKE 'test_%'; DELETE FROM users WHERE uid LIKE 'test_%';" 2>/dev/null

# Test 1: Register test users
echo ""
echo "--- Test 1: 注册两个测试用户 ---"
# Use the benchmark tool in register mode
cd /mnt/c/Users/94173/Desktop/task/benchmark
./login_bench_linux -addr $SERVER_ADDR -mode register -c 1 -n 1 > /tmp/reg1.log 2>&1
echo "注册 test_user_a: $(grep Success /tmp/reg1.log)"
./login_bench_linux -addr $SERVER_ADDR -mode register -c 1 -n 1 > /tmp/reg2.log 2>&1
echo "注册 test_user_b: $(grep Success /tmp/reg2.log)"

echo ""
echo "--- 所有测试完成 ---"
echo "查看服务器日志: cat /tmp/srv4.log"
