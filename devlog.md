# Dev Log

> 记录开发过程中遇到的问题、排查思路与结论。新条目加在顶部。

---

## BUG #1：客户端连接超时（Connection timeout）

### 现象
`chat_client` 连 `chat_server` 报 `Connection timeout`，服务端日志为空。

### 根因
客户端 `main` 里 `TcpClient::connect()` 之后只有 `while(!connected()) usleep` 空转，
**从未调用 `EventLoop::loop()`**。muduo 的 connector 依赖所在 EventLoop 线程跑 `poll()`
才能把排队的函数分发执行，loop 不跑则 SYN 永远发不出去，服务端自然看不到连接。

### 排查方法

| 手段 | 命令 | 结论 |
|------|------|------|
| **strace** | `strace -f ./chat_client 127.0.0.1 8000 2>&1 \| grep connect` | 看不到 `connect()` 系统调用 → connector 未执行 |
| **gdb** | `b muduo::net::EventLoop::loop` / `b muduo::net::Connector::startInLoop` | 断点不命中 → loop 从未运行 |

### 修复
`client/chat_client.cc` 把 `EventLoop` 放到独立线程 `ioThread([&loop]{ loop.loop(); })` 跑起来，
stdin 在主线程读；`quit` 时 `loop.quit()` 并 `ioThread.join()`。

### 验证
修复后，客户端连接正常，服务端日志也正常。

![alt text](image.png)

---

## BUG #2：EventLoop 跨线程调用导致 abort 崩溃

### 现象
客户端线程抛出 abort 异常，服务端日志正常。

![alt text](image-1.png)

### 根因
`EventLoop loop` 在 main 线程创建，但 `loop.loop()` 被放到 `ioThread`（另一个线程）调用。
muduo 的 `loop()` 入口第一件事就是断言 `isInLoopThread()`，检测到线程 ID 不匹配 → `LOG_FATAL` → `abort()`。

### GDB 排查过程

#### 1. 在 abort 上设断点，抓 crash 现场
```bash
gdb ./chat_client
(gdb) b abort
(gdb) r 127.0.0.1 8000
```

![alt text](image-2.png)

> 上图：线程调用不属于它的事件循环，导致异常抛出。

#### 2. 打印调用栈，定位触发点
```bash
(gdb) bt
```

![alt text](image-3.png)

关键帧：
```
#5  EventLoop::loop()           → 入口断言检查
#4  EventLoop::assertInLoopThread()  → 检测线程不匹配
#3  EventLoop::abortNotInLoopThread() → LOG_FATAL
#0  __GI_abort()                → 进程被杀
```

#### 3. 查看线程 ID，确认不一致
```bash
(gdb) frame 5
(gdb) p this->threadId_         # EventLoop 创建时记录的主线程 ID
(gdb) p muduo::CurrentThread::t_cachedTid   # 当前 ioThread 的 ID
```

![alt text](image-4.png)

> 如我们所看，ioThread 线程 ID 与主线程 ID 不一致，导致 abort。

### 修复
**正确做法：main 线程创建 EventLoop，也必须在 main 线程调 `loop()`。**

```cpp
// main 线程：创建 + 调用（同线程）
EventLoop loop;
loop.loop();    // ✓

// stdin 线程：通过 sendEnvelope → runInLoop 安全投递
std::thread stdinThread([&]() {
    client.sendEnvelope(env);  // 内部用 runInLoop，安全
});
```
客户端再登出是,返回一条日志后续登录无法成功
![alt text](image-5.png)

---
handleLogout 里调了 conn->shutdown()——服务端关闭连接后，客户端的 onConnection 触发 loop_->quit()，但 stdinThread 还在跑，显示了菜单但连接已断，再登录就失败了。

## 知识总结

### 一、什么是 abort 崩溃（信号 SIGABRT）

程序打印 `Aborted (core dumped)` 代表进程收到 `SIGABRT` 信号，和段错误（`SIGSEGV`）本质完全不同：

| | SIGSEGV（段错误） | SIGABRT（abort 退出） |
|---|---|---|
| 性质 | 被动崩溃 | 主动自杀 |
| 触发者 | 操作系统 / MMU | 程序代码 / 标准库 |
| 原因 | 非法内存访问（野指针、越界、空指针） | 主动调用 `abort()` |
| 目的 | 保护系统 | 阻止数据损坏、死锁、内存错乱 |

**`abort()` 底层行为：**
- 向自身发送 `SIGABRT` 信号
- 默认行为：生成 core 转储文件、终止整个进程
- 无法被 `try-catch` 捕获（属于进程信号，不是 C++ 异常）

### 二、触发 abort() 的三大核心场景

#### 1. C 内存管理错误（libc 主动 abort）
libc 的内存分配器检测到堆损坏时直接调用 `abort()`：
- 重复释放内存：`free(p)` 执行两次
- 释放非法指针：栈变量、全局变量直接 `free`
- 内存越界写破坏堆元数据（堆溢出）

#### 2. C++ 未捕获异常（标准强制行为）
C++ 标准规定：线程抛出异常且无 `try-catch` 捕获时，自动调用 `std::terminate()`，默认实现就是 `abort()`。

**多线程最大坑：** 裸 `std::thread` 运行的函数抛异常，无法在主线程 `try-catch`，直接触发 abort。

#### 3. 标准库断言 assert()
调试代码中 `assert(条件)`，条件不成立时调用 `abort()`。

### 三、muduo 的 One Loop Per Thread 设计

#### 为什么一个线程一个事件循环？

**核心原因：避免锁竞争**，这是传统多线程网络库最大的性能瓶颈。

**传统做法（多线程共享一个 EventLoop）：**
```
Thread A: epoll_wait → 回调 → 操作 sharedState → 加锁
Thread B: epoll_wait → 回调 → 操作 sharedState → 加锁
                                        ↓
                              锁竞争 → 上下文切换 → 性能下降
```

**muduo 的做法（one loop per thread）：**
```
Thread A: EventLoop A → 处理连接 1,2,3    （独立，不锁）
Thread B: EventLoop B → 处理连接 4,5,6    （独立，不锁）
Thread C: EventLoop C → 处理连接 7,8,9    （独立，不锁）
                                        ↓
                              无锁 → 线性扩展 → 连接数翻倍性能也翻倍
```

#### 跨线程怎么办？

用 `runInLoop` 把任务投递到目标线程执行，而不是直接操作别人的数据：
```
Thread A 想发消息给 Thread B 管的连接：
    loop_B->runInLoop([msg]{
        // 这段代码在 Thread B 执行，不竞争
        conn->send(msg);
    });
```

#### 对比总结

| | 共享 EventLoop | One Loop Per Thread |
|---|---|---|
| 锁竞争 | 高（每操作都锁） | 无（各自独立） |
| 扩展性 | 差（线程多反而慢） | 好（线性扩展） |
| 代码复杂度 | 高（到处加锁） | 低（单线程逻辑清晰） |
| 适用场景 | 连接数少 | 海量连接（C10K/C100K） |

> muduo 能用少量线程处理几万并发连接——每个线程专注自己的 EventLoop，零锁竞争，CPU 缓存命中率高，性能接近单线程但又能利用多核。

跨线程事件通知使用 `eventfd` 和同步队列来实现，保证线程之间的安全通信。

---

## BUG #4：ThreadPool runInLoop 投递到错误 EventLoop

### 现象

ThreadPool 异步任务完成后，响应需要 ~30ms 才能返回客户端（从 WSL2 内部压测发现 recv_avg=29ms）。
且线程池版本的 QPS 比纯同步版本还低（5115 vs 6201），不符合预期。

### 根因

所有 `runInLoop` 调用都写死为 `loop_`（主 EventLoop），但 muduo `TcpServer` 启用多 Reactor 后，
连接被分配到 sub EventLoop（Round-Robin）。投递到主 EventLoop 的 functor 需要经过：

```
线程池完成 → runInLoop(main EventLoop) → eventfd 唤醒主线
            → main EventLoop 处理 functor → conn->send()
            → conn 属于 sub EventLoop → 内部 runInLoop(sub EventLoop)
            → eventfd 再次唤醒 sub 线程 → 真正发送
```

**多了两次 eventfd 唤醒 + 两次线程上下文切换**，延迟从 ~0.7ms 膨胀到 ~30ms。

### 修复

用 `conn->getLoop()` 获取连接所属的正确 EventLoop，直接投递：

```cpp
// 错误：投递到主 EventLoop
loop_->runInLoop([this, conn]() { ... });

// 正确：投递到连接所属的 EventLoop
auto* ioLoop = conn->getLoop();
ioLoop->runInLoop([this, conn]() { ... });
```

### 验证

WSL2 内部压测（ThreadPool + 4 I/O 线程 + Redis Token）：

| 配置 | 单连接延迟 | c=50 QPS |
|------|-----------|---------|
| 修复前（loop_） | ~30ms | — |
| 修复后（conn->getLoop()） | ~1.5ms | 8186 |

### 教训

1. **muduo 多 EventLoop 下必须用 `conn->getLoop()` 确定连接所属线程**，不能写死
2. `conn->send()` 是线程安全的（内部跨线程投递），但会增加延迟
3. 通过响应延迟是否 ~1ms 还是 ~30ms，可以快速判断 runInLoop 是否投递错了 EventLoop

---

## BUG #3：Redis bool 缓存导致鉴权脏数据

### 现象

用 Redis 缓存用户鉴权结果（`auth:uid = 1/0`，TTL 300s），压测 QPS 没有提升。
进一步调试发现：`SETEX` 返回 OK (type=5)，但紧接的 `GET` 返回 NIL (type=4)。

### 根因分析

#### 1. hiredis 多线程竞态

chat_server 有 4 个 EventLoop 线程（1 main + 3 sub），共用一个 `redisContext*`。
虽然加了 `std::mutex`，但 Redis 协议是请求-响应式的：

```
线程A: SETEX auth:bench0 300 1  →  发送命令
线程B: GET auth:bench0          →  发送命令（线程A的响应还没读）
线程A: 读响应                   →  读到的是线程B的GET响应
线程B: 读响应                   →  读到的是线程A的SETEX响应
```

协议流交叉导致类型错乱。hiredis 文档明确说明：**一个 redisContext 只能在一个线程中使用**，
多线程必须每个线程一个连接，或用 pipeline + 队列分发。

#### 2. Bool 缓存的脏数据问题

即使 hiredis 竞态修复，bool 缓存仍有逻辑漏洞：

```
时刻1: 用户A密码登录 → Redis SET auth:A=1 (TTL 300s)
时刻2: 用户A修改密码 → MySQL 更新密码，但 Redis 仍缓存 auth:A=1
时刻3: 攻击者用旧密码登录 → Redis 命中 auth:A=1 → 跳过MySQL → 登录成功！
```

### 解决方案：Token 鉴权

抛弃 bool 缓存，改为 token 鉴权：

```
登录流程：
1. MySQL 验证密码（SHA256，~1ms）
2. 生成随机 token（32位 hex）
3. Redis: SETEX token:uid {token} 3600
4. 返回 token 给客户端

后续鉴权：
1. 客户端携带 uid + token
2. Redis: GET token:uid → 比对 token
3. 匹配则鉴权通过（跳过 MySQL）

改密码时：
1. MySQL 更新密码
2. Redis: DEL token:uid（立即失效）
```

#### Token 方案的优势

| 对比项 | Bool 缓存 (auth:uid) | Token 鉴权 (token:uid) |
|--------|----------------------|------------------------|
| 脏数据 | 改密码后旧缓存仍有效 | 改密码 DEL token 立即失效 |
| 竞态 | 频繁 GET/SET 同一 key | 每次登录写入唯一 token |
| 安全性 | 缓存的是"是否通过" | 缓存的是随机 token 字符串 |
| 跨节点 | 需要同步 bool 状态 | Redis 天然共享 token |

#### 数据结构选择

用 Redis **String**（`SET/GET`）而非 Hash：

```
SET token:bench0 "a1b2c3d4..." EX 3600
GET token:bench0
```

原因：token 鉴权只需要一个值（token 字符串），String 最简单高效。
Hash 适合多字段场景（如 `HSET user:bench0 token "abc" last_login "123"`），此处不需要。

### 压测验证

Token 模式压测结果（预注册用户，先密码登录获取 token，再用 token 重连）：

| 测试 | 并发 | QPS | P99 | 说明 |
|------|------|-----|-----|------|
| T-1  | 50   | 1128.2 | 48.7ms | token 重连路径生效 |
| T-2  | 200  | 4277.6 | 53.7ms | 与密码模式持平 |
| T-3  | 500  | 5504.1 | 55.1ms | 53%成功率 |

QPS 未提升的原因：压测 worker 共享 bench0-bench999，token 互相覆盖，大部分请求仍走 MySQL。
真实场景（每用户独占连接）下 token 路径会稳定生效。

### 关键教训

1. **hiredis 不是线程安全的**：多线程必须每线程一个连接，或用 `redisAsyncContext`
2. **缓存鉴权结果（bool）不如缓存凭证（token）**：bool 无法及时失效
3. **压测工具设计要考虑隔离性**：共享 UID 导致 token 覆盖，掩盖了真实优化效果
