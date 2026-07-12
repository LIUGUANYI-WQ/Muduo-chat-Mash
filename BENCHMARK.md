# Login QPS 压测文档

## 1. 为什么用 Go 有栈协程而不是 C++ 多线程

### C++ muduo 多线程方案的问题

chat_client 使用 muduo TcpClient，一个连接绑定一个 EventLoop（一个线程）。
要模拟 100 个并发登录，需要 100 个线程：

| 问题 | 说明 |
|------|------|
| 线程创建开销 | pthread_create 每次约 10-30μs，100 线程 = 1-3ms 启动延迟 |
| 内存占用 | 每个线程默认 8MB 栈（Linux），100 线程 ≈ 800MB |
| 上下文切换 | 100 线程在 4 核机器上切换开销大，QPS 上不去 |
| EventLoop 模型 | 每个线程跑独立 loop，无法共享连接池，资源浪费 |

### Go 有栈协程方案的优势

Go runtime 使用 GMP 调度模型，goroutine 初始栈仅 2-8KB，可动态增长：

| 优势 | 说明 |
|------|------|
| 轻量 | 10000 goroutine 仅占约 80MB 内存（vs C++ 100 线程 800MB） |
| 快速创建 | goroutine 创建约 0.3μs（vs pthread_create 10-30μs） |
| 低切换开销 | 用户态调度，无系统调用，切换约 200ns（vs 线程切换 1-10μs） |
| 复用连接 | 多 goroutine 可共享一个 TCP 连接的写操作（通过 channel 同步） |
| 简单 | 不需要手写 EventLoop、Reactor、回调地狱 |

### 核心结论

C++ muduo 的 Reactor 模型适合**服务端**（少量长连接、高吞吐），
但做**压测客户端**需要大量并发连接时，Go 的 goroutine 模型更合适：
- 写起来简单（同步阻塞风格，不用回调）
- 资源消耗低（1 万 goroutine ≈ 100 线程的 1/10 内存）
- QPS 测量更准确（没有线程调度噪声）

---

## 2. 测试环境

| 项目 | 配置 |
|------|------|
| OS | Windows 11 + WSL2 Ubuntu 24.04 |
| CPU | （请填写） |
| 内存 | （请填写） |
| MySQL | 8.0, localhost:3306, chat 库 |
| 服务端 | chat_server（muduo + Protobuf） |
| 压测端 | login_bench.exe（Go 1.24 协程） |

---

## 3. 测试步骤

### Step 1: 启动服务器

```bash
# WSL 中
cd /mnt/c/Users/94173/Desktop/task
bash run_server.sh
```

确认看到 `MySQL connected` 和监听端口输出。

### Step 2: 运行压测

```powershell
# Windows PowerShell
cd C:\Users\94173\Desktop\task\benchmark
.\login_bench.exe -addr 127.0.0.1:8000 -c 50 -n 10000
```

### Step 3: 监控 CPU

```bash
# WSL 中另开终端，监控服务器 CPU
top -p $(pgrep chat_server)
```

---

## 4. 测试数据

### 4.1 单 Reactor 模式（1个线程，setThreadNum(0)）

| 测试编号 | 并发数(-c) | 总请求数(-n) | 成功数 | 失败数 | QPS | 平均延迟 | P50 | P90 | P95 | P99 | CPU使用率 |
|---------|-----------|-------------|-------|-------|-----|---------|-----|-----|-----|-----|----------|
| S-1     | 50        | 10000       | 10000 | 0     | 1126.1 | 44.1ms | 44.1ms | 46.0ms | 46.7ms | 48.7ms | （填写） |
| S-2     | 200       | 50000       | 50000 | 0     | 4380.6 | 45.1ms | 44.6ms | 47.8ms | 48.9ms | 51.5ms | （填写） |
| S-3     | 500       | 100000      | —     | 全部失败 | — | — | — | — | — | — | — |

> S-3 崩溃：单线程 Reactor + MySQL 单连接，500 并发直接崩。

截图：`bench_image/单reactor/`

### 4.2 多 Reactor 模式（server_.setThreadNum(4)，1 main + 3 sub）

修改方式：main.cc 中调用 `server.setThreadNum(4);`，重新编译。

| 测试编号 | 并发数(-c) | 总请求数(-n) | 成功数 | 失败数 | QPS | 平均延迟 | P50 | P90 | P95 | P99 | CPU使用率 |
|---------|-----------|-------------|-------|-------|-----|---------|-----|-----|-----|-----|----------|
| M-1     | 50        | 10000       | 10000 | 0     | 1129.5 | 44.1ms | 44.0ms | 45.7ms | 46.6ms | 48.7ms | （填写） |
| M-2     | 200       | 50000       | 50000 | 0     | 4272.1 | 46.2ms | 45.6ms | 49.8ms | 51.1ms | 53.9ms | （填写） |
| M-3     | 500       | 100000      | 48200 | 51800 | 5027.4 | 47.0ms | 46.5ms | 50.4ms | 51.8ms | 55.8ms | （填写） |

> M-3 部分连接被拒绝（51800失败），服务器处理能力达到极限。

截图：`bench_image/多reactor/`

### 4.3 测试结论

```
单 Reactor vs 多 Reactor 对比:

c=50:
  单 Reactor: QPS=1126.1, P99=48.7ms
  多 Reactor: QPS=1129.5, P99=48.7ms
  → 差异 <1%，瓶颈在 MySQL 查询，不在 I/O 线程数

c=200:
  单 Reactor: QPS=4380.6, P99=51.5ms
  多 Reactor: QPS=4272.1, P99=53.9ms
  → 差异 <3%，多 Reactor 反而略慢（线程切换开销）

c=500:
  单 Reactor: 崩溃（连接被拒绝）
  多 Reactor: QPS=5027.4, P99=55.8ms（30%连接失败）
  → 多 Reactor 能扛住更多连接，但 MySQL 单连接仍是瓶颈

核心发现:
- c≤200 时，单/多 Reactor 性能几乎相同
- 瓶颈不在 muduo 线程模型，而在 MySQL 单连接 + mutex 串行化
- 下一步优化方向：MySQL 连接池 + Redis 缓存
```

---

## 5. 下一步优化方向

| 优化项 | 预期效果 | 说明 |
|--------|---------|------|
| MySQL 连接池 | 减少连接建立开销，提升并发查询能力 | 当前单连接+mutex，多线程串行化 |
| Redis 缓存热点用户 | 减少 MySQL 查询次数 | 登录先查 Redis，未命中再查 MySQL |
| 多 Reactor 线程 | 提升 I/O 并发能力 | 当前已改代码，待测试 |
| SHA256 换更快哈希 | 降低 CPU 占用 | bcrypt cost 调低或换用 xxhash |

---

## 6. 注意事项

1. 每次测试间隔 5 秒，等服务器日志稳定
2. bench 用户是随机生成的，大部分不存在 → login 返回失败，这是正常的
3. P99 含义：99% 的请求延迟低于此值，反映长尾延迟
4. 如果 P99 异常高（>100ms），检查 MySQL 连接和线程锁竞争
