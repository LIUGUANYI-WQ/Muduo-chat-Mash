# Muduo-Chat-Mash 完整设计方案

## 1. 系统架构总览

```
                               公网用户 (TCP / WebSocket)
                                      │
                                      │ chat.example.com:443
                                ┌─────▼──────┐
                                │   Nginx     │  SSL / LB / WS Upgrade
                                └─┬────────┬─┘
                                  │        │
                          ┌───────▼──┐ ┌───▼───────┐
                          │ chat-1   │ │ chat-2     │  ...
                          └────┬─────┘ └─────┬─────┘
                               │              │
          ┌────────────────────▼──────────────▼────────────────────┐
          │                     Redis                              │
          │    Pub/Sub (实时广播) | Stream (持久化流) | Hash (路由) │
          └────────────────────────┬───────────────────────────────┘
                                   │
          ┌────────────────────────▼───────────────────────────────┐
│               MySQL (数据库)                             │
│    users / rooms / room_members / messages / offline   │
│    sessions / friendships                              │
          └────────────────────────┬───────────────────────────────┘
                                   │
          ┌────────────────────────▼───────────────────────────────┐
          │               OpenLDAP                                 │
          │               (统一认证源)                              │
          └────────────────────────┬───────────────────────────────┘
                                   │
          ┌────────────────────────▼───────────────────────────────┐
          │     Prometheus ──► Grafana (大屏)                      │
          └────────────────────────────────────────────────────────┘
```

---

## 2. 组件职责

| 组件 | 角色 | 关键职责 |
|------|------|---------|
| **Nginx** | 入口网关 | SSL termination、TCP 四层负载均衡、WebSocket 升级转发、Grafana 反向代理 |
| **Chat-Node** | 业务核心 | 连接管理、房间管理、消息路由、LDAP 认证、Prometheus 指标 |
| **Redis** | 实时层 | Pub/Sub 跨节点广播、Stream 消息流持久化、路由表 / 在线状态 |
| **MySQL** | 持久层 | 用户信息、好友关系、房间元数据、消息记录、离线消息、撤回记录、会话管理 |
| **OpenLDAP** | 认证源 | 统一用户目录、密码认证、分组管理 |
| **Prometheus** | 监控采集 | 从各节点拉取指标、告警规则 |
| **Grafana** | 可视化 | 实时大屏、历史趋势、节点状态 |

---

## 3. 数据存储设计

### 3.1 Redis 数据结构

| 用途 | 类型 | Key | Value | 说明 |
|------|------|-----|-------|------|
| 在线状态 | String | `online:{uid}` | `nodeId` | 用户连到哪个节点 |
| 路由表 | Hash | `user_route` | `{uid → nodeId}` | 全部在线用户分布 |
| 房间成员 | Set | `room:{roomId}` | `[uid1, uid2...]` | 房间在线成员 |
| 消息中继 | Stream | `chat:messages` | `{from, to, room, content}` | 持久化流供异步落库 |
| 节点心跳 | Hash | `node:heartbeat` | `{nodeId → timestamp}` | 健康检测 |
| 会话 Token | String | `session:{token}` | `{uid, nodeId, expire}` | 登录态，TTL 自动过期 |

### 3.2 MySQL 表结构

```sql
-- 用户表（与 LDAP 同步）
CREATE TABLE users (
    uid VARCHAR(64) PRIMARY KEY,
    nickname VARCHAR(128) NOT NULL DEFAULT '',
    email VARCHAR(256) NOT NULL DEFAULT '',
    avatar_url TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_login DATETIME
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 房间表
CREATE TABLE rooms (
    room_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL UNIQUE,
    creator_uid VARCHAR(64) NOT NULL,
    type TINYINT DEFAULT 0,   -- 0:公开 1:私有
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (creator_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 房间成员
CREATE TABLE room_members (
    room_id BIGINT NOT NULL,
    uid VARCHAR(64) NOT NULL,
    role TINYINT DEFAULT 0,   -- 0:成员 1:管理员 2:群主
    joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (room_id, uid),
    FOREIGN KEY (room_id) REFERENCES rooms(room_id),
    FOREIGN KEY (uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 好友关系
CREATE TABLE friendships (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    requester_uid VARCHAR(64) NOT NULL,
    target_uid VARCHAR(64) NOT NULL,
    status TINYINT DEFAULT 0,  -- 0:pending 1:accepted 2:rejected 3:blocked
    message VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_friendship (requester_uid, target_uid),
    FOREIGN KEY (requester_uid) REFERENCES users(uid),
    FOREIGN KEY (target_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_friendships_uid ON friendships(requester_uid, status);
CREATE INDEX idx_friendships_target ON friendships(target_uid, status);

-- 消息记录
CREATE TABLE messages (
    msg_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    seq BIGINT NOT NULL,                 -- 房间内序号
    room_id BIGINT,                      -- NULL=私聊
    from_uid VARCHAR(64) NOT NULL,
    to_uid VARCHAR(64),                  -- NULL=房间消息, 非NULL=私聊
    content TEXT NOT NULL,
    msg_type TINYINT DEFAULT 0,          -- 0:text 1:image 2:file
    recalled TINYINT DEFAULT 0,          -- 0:正常 1:已撤回
    recall_time DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (from_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_messages_room_seq ON messages(room_id, seq);
CREATE INDEX idx_messages_private ON messages(from_uid, to_uid);

-- 离线消息队列
CREATE TABLE offline_messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    target_uid VARCHAR(64) NOT NULL,
    msg_id BIGINT NOT NULL,
    delivered TINYINT DEFAULT 0,          -- 0:未投递 1:已投递
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (target_uid) REFERENCES users(uid),
    FOREIGN KEY (msg_id) REFERENCES messages(msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_offline_undelivered ON offline_messages(target_uid, delivered);

-- 会话记录（登录态持久化，用于 Token 吊销校验）
CREATE TABLE sessions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    uid VARCHAR(64) NOT NULL,
    token VARCHAR(128) NOT NULL UNIQUE,
    node_id VARCHAR(64) NOT NULL,        -- 登录时所在的节点
    ip_address VARCHAR(45),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    expired_at DATETIME NOT NULL,         -- Token 过期时间
    revoked TINYINT DEFAULT 0,            -- 0:有效 1:已吊销
    FOREIGN KEY (uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_sessions_uid ON sessions(uid, revoked);
CREATE INDEX idx_sessions_expire ON sessions(expired_at);
```

### 3.3 数据流

#### 发送一条消息

```
Client A ──send(token + msg)──► Chat Node N1
                     │
                     ├──► 鉴权: Redis GET session:{token}
                     │       └── 失败 → 拒绝请求
                     │
                     ├──► Redis Stream: chat:messages (持久化 + 中继)
                     │       │
                     │       ├──► 异步消费者 ──► MySQL (消息落库)
                     │       │
                     │       └──► Redis Pub/Sub ──► 其他节点 ──► Client B
                     │
                     ├──► 同节点：直推目标客户端
                     ├──► 异节点：查路由表 → 跨节点转发
                     │
                     └──► 目标离线：写入 offline_messages 表
                            待用户上线后补推
```

#### 撤回一条消息

```
Sender ──recall(token + msg_id)──► Chat Node
                     │
                     ├──► 鉴权: Redis GET session:{token}
                     │       └── 失败 → 拒绝请求
                     │
                     ├──► 校验：是否消息发送者本人
                     ├──► 校验：是否在撤回时间窗口内（默认 2 分钟）
                     │
                     ├──► MySQL: UPDATE messages SET recalled=1, recall_time=NOW()
                     │
                     ├──► Redis Pub/Sub: recall 事件广播给所有节点
                     │       │
                     │       ├──► 房间消息：广播给房间所有在线成员
                     │       └──► 私聊消息：推送给接收方
                     │
                     └──► 目标离线：写入离线撤回通知
```

---

## 4. 登录态与会话管理

### 4.1 Token 认证流程

```
Client                    Chat Server                    Redis / MySQL
  │                            │                            │
  │── LoginRequest ──────────► │                            │
  │    {uid, passwd}           │                            │
  │                            ├──► LDAP bind 验证凭证       │
  │                            │       │                    │
  │                            │◄────── OK                  │
  │                            │                            │
  │                            ├──► 生成 Token (uuid/SHA256) │
  │                            ├──► Redis SET session:{token}
  │                            │       {uid,nodeId,ttl=7d} │
  │                            ├──► MySQL INSERT sessions   │
  │                            │                            │
  │◄── LoginResponse ────────┤                            │
  │    {ok=true, token}       │                            │
```

### 4.2 Token 数据结构

| 存储 | Key | Value/Fields | TTL |
|------|-----|-------------|-----|
| Redis | `session:{token}` | `{uid, nodeId, loginTime}` | 7 天（自动过期） |
| MySQL | `sessions` 表 | 完整记录（含吊销标记） | 持久化 |

### 4.3 鉴权链路

```
每次请求（发消息/建房间等）:

Client ──request + token──► Chat Node
                              │
                              ├──► Redis GET session:{token}
                              │       │
                              │       ├── 存在 → 取出 uid，放行
                              │       └── 不存在/过期 → 拒绝（需重登录）
                              │
                              └── 请求处理完毕
```

### 4.4 断线重连

```
Client TCP 断线
    │
    ├── Redis 不删 token（token 维持 7 天有效）
    ├── Redis 更新 online 状态: 清空 nodeId 或标记离线
    │
Client 重新连接
    │
    ├── 发送 LoginRequest {token}（无需密码直接 token 登录）
    ├── Server 校验 Redis session 有效
    ├── 恢复在线状态 + 绑定新连接
    ├── 补推离线期间的消息
    └── 通知好友上线
```

### 4.5 主动退出 / Token 吊销

```
Client 发送 Logout
    │
    ├── Redis DEL session:{token}
    ├── MySQL UPDATE sessions SET revoked=1
    ├── 清理在线状态
    └── 通知好友下线

管理员强制踢人:
    ├── MySQL UPDATE sessions SET revoked=1 WHERE uid=?
    └── 广播 kick 通知 → 节点断开连接
```

### 4.6 安全策略

| 措施 | 说明 |
|------|------|
| Token 长度 | 64 字节随机 HEX（SHA256 或 uuid v4） |
| TTL | Redis 7 天自动过期，MySQL 持久化留痕 |
| 吊销 | 每次请求可选择性校验 MySQL revoked 状态（安全敏感操作） |
| 单点登录 | 同一 uid 新登录 → 旧 token 自动失效（可选策略） |
| 密码 | 不落盘，仅 LDAP bind 验证 |

---

## 5. 消息协议（Protobuf）

### 5.1 消息类型

```
// chat.proto

message LoginRequest {
    string uid = 1;
    string passwd = 2;     // 首次登录或 LDAP 认证时必填
    string token = 3;      // 重连时携带 token 免密登录
}

message LoginResponse {
    bool ok = 1;
    string token = 2;      // 服务端下发的会话 token
    string reason = 3;
    uint64 server_time = 4;
}

message LogoutRequest {
    string uid = 1;
    string token = 2;
}

message CreateRoom {
    string name = 1;
    string creator = 2;
}

message JoinRoom {
    string room_name = 1;
    string uid = 2;
}

message ChatMessage {
    string from = 1;
    string to = 2;          // 空=房间消息, 非空=私聊
    string room = 3;
    string content = 4;
    uint64 msg_id = 5;      // 服务器分配的消息 ID
    uint64 seq = 6;         // 房间内序号
    uint64 timestamp = 7;
    bool   recalled = 8;    // 客户端展示时检查
}

// ─── 好友系统 ───

message FriendRequest {
    string from_uid = 1;
    string to_uid = 2;
    string message = 3;     // 验证信息
}

message FriendResponse {
    string from_uid = 1;    // 对方 uid
    bool   accepted = 2;    // true=同意 false=拒绝
}

message FriendList {
    message Friend {
        string uid = 1;
        string nickname = 2;
        bool   online = 3;
        string node = 4;
    }
    repeated Friend friends = 1;
}

message FriendRemove {
    string target_uid = 1;
}

// ─── 撤回系统 ───

message RecallMessage {
    uint64 msg_id = 1;          // 要撤回的消息 ID
    string from_uid = 2;        // 发送者（校验用）
    string room = 3;            // 房间名（房间消息必填）
    string to_uid = 4;          // 接收方 uid（私聊消息必填）
}

message RecallNotify {
    uint64 msg_id = 1;
    string from_uid = 2;
    string room = 3;
    string to_uid = 4;
    uint64 recall_time = 5;
}

// ─── 通用 ───

message Heartbeat {
    uint64 timestamp = 1;
}

message ErrorResponse {
    uint32 code = 1;
    string reason = 2;
}
```

### 5.2 传输格式

```
[4字节网络序长度(int32)] [Protobuf 二进制数据]
```

复用 muduo `LengthHeaderCodec` 模式。

### 5.3 消息信封（所有请求携带 token）

```
message Envelope {
    string token = 1;          // 登录态 token（首次 LoginRequest 可为空）
    oneof payload {
        LoginRequest login_req = 10;
        LogoutRequest logout_req = 11;
        ChatMessage chat_msg = 20;
        CreateRoom create_room = 21;
        JoinRoom join_room = 22;
        FriendRequest friend_req = 30;
        FriendResponse friend_resp = 31;
        FriendRemove friend_remove = 32;
        RecallMessage recall_msg = 40;
        Heartbeat heartbeat = 50;
    }
}

message ServerMessage {
    oneof payload {
        LoginResponse login_resp = 10;
        ChatMessage chat_msg = 20;
        RecallNotify recall_notify = 40;
        FriendRequest friend_req = 30;
        FriendResponse friend_resp = 31;
        FriendList friend_list = 33;
        FriendRemove friend_remove = 32;
        ErrorResponse error = 99;
    }
}
```

---

## 6. 客户端连接方式

### 6.1 双协议支持

| 方式 | Nginx 端口 | Chat 端口 | 适用场景 |
|------|-----------|----------|---------|
| **原生 TCP** | `stream 443 → 9000` | 9000 | 高性能 Native 客户端 |
| **WebSocket** | `http 443 → upgrade → 9001` | 9001 | Web 浏览器客户端 |

### 6.2 连接生命周期

```
TCP/WS 建立
    │
    ├── 未认证状态（仅接收 LoginRequest）
    ├── LoginRequest 通过 → 分配 token → 转入已认证
    │
    ├── 已认证状态
    │   ├── 正常收发消息
    │   ├── 心跳保活（每 30s）
    │   └── 断线 → 保留 token，等待重连
    │
    └── Logout / Token 过期 / 连接关闭 → 清理状态

---

## 7. 公网部署方案

### 7.1 服务器

| 项目 | 配置 |
|------|------|
| 云厂商 | 阿里云 ECS |
| 规格 | 2C4G（初期） |
| 带宽 | 5Mbps |
| 系统 | Ubuntu 22.04 LTS |

### 7.2 网络拓扑

```
用户 ──► chat.example.com:443
            │
       ┌────▼────┐
       │ 安全组    │  开放：443 / 3000(Grafana) / 9090(Prom)
       └────┬────┘    屏蔽：6379(Redis) / 389(LDAP) / 3306(MySQL)
            │
       ┌────▼────┐
       │ Nginx   │  Let's Encrypt SSL 自动续签
       └─────────┘
```

### 7.3 域名与 SSL

1. 注册域名（NameSilo / Cloudflare）
2. A 记录指向 ECS 公网 IP
3. Nginx 配置 Let's Encrypt（certbot）自动续签

### 7.4 安全措施

| 措施 | 说明 |
|------|------|
| SSL/TLS | Nginx 443 + certbot |
| 安全组 | 最小开放原则 |
| Grafana | Nginx 反向代理 + HTTP Basic Auth |
| Redis | requirepass + bind 内网 |
| MySQL | 只允许 chat 节点访问 |
| LDAP | 只允许 chat 节点访问 |

---

## 8. Docker Compose 服务清单

```yaml
services:
  nginx:            # 负载均衡 + SSL + WS Upgrade
                    # 端口: 443 (公网), 80 (certbot)
  chat-node-1:      # Chat 服务器节点 1
  chat-node-2:      # Chat 服务器节点 2
  chat-node-3:      # Chat 服务器节点 3
  redis:            # 消息中继 + 路由表 + 在线状态
                    # 端口: 6379 (内网)
  mysql:            # 持久化数据库
                    # 端口: 3306 (内网)
  openldap:         # 用户认证目录服务
                    # 端口: 389 (内网)
  phpldapadmin:     # LDAP Web 管理界面 (内网访问)
                    # 端口: 8080 (内网)
  prometheus:       # 指标采集
                    # 端口: 9090 (内网)
  grafana:          # 可视化大屏
                    # 端口: 3000 (Nginx 反向代理)
```

---

## 9. 监控与可观测性

### 9.1 Prometheus 指标

| 指标 | 类型 | Labels | 说明 |
|------|------|--------|------|
| `chat_connections` | Gauge | node | 当前连接数 |
| `chat_messages_total` | Counter | node, type(room/private) | 累计消息数 |
| `chat_messages_per_second` | Gauge | node | 每秒消息速率 |
| `chat_rooms` | Gauge | node | 房间数 |
| `chat_auth_duration_ms` | Histogram | node, status | 认证延迟分位 |
| `chat_online_users` | Gauge | node | 在线用户数 |
| `chat_friendships_total` | Gauge | - | 好友关系总数 |
| `chat_recall_total` | Counter | node, type(room/private) | 累计撤回次数 |
| `chat_recall_success_rate` | Gauge | node | 撤回成功率 |

### 9.2 Grafana 大屏面板

1. **实时在线人数** — 多节点折线图
2. **消息吞吐** — 每秒消息数折线图
3. **房间热度 TOP10** — 柱状图
4. **节点 CPU / 内存** — 仪表盘
5. **认证成功率** — 饼图
6. **延迟 P50 / P90 / P99** — 热力图

---

## 10. OpenLDAP 目录结构

```
dc=chat,dc=example,dc=com
├── ou=users
│   ├── uid=alice
│   │   ├── cn: Alice
│   │   ├── sn: Wang
│   │   ├── mail: alice@example.com
│   │   └── userPassword: {SSHA}encrypted
│   └── uid=bob
├── ou=groups
│   └── cn=admins
│       ├── memberUid: alice
└── ou=rooms
    └── cn=general
```

认证流程：Chat 节点收到登录请求 → LDAP bind 验证凭证 → 成功后拉取用户属性 → 写入本地缓存。

---

## 11. 实施步骤（逐个功能串行）

```
Step 1  ─ 环境准备
           WSL 编译 muduo → 安装 Docker → 跑通示例

Step 2  ─ 单节点聊天服务器（内存版）
           Protobuf 协议定义 → 基础聊天逻辑 → CLI 测试客户端

Step 3  ─ MySQL 集成
           建表 → Chat 节点连接池 → 用户注册登录 → 消息落库

Step 4  ─ 好友系统
           好友请求/同意/拒绝/删除 → 好友列表加载 → 在线状态推送

Step 5  ─ 消息撤回
           发送消息分配 msg_id → 撤回校验（发送者 + 时间窗口）→
           房间广播 RecallNotify / 私聊推送 RecallNotify

Step 6  ─ Redis 集成
           Stream 消息中继 → Pub/Sub 跨节点广播 → 路由表

Step 7  ─ 集群 + Nginx 负载均衡
           多节点部署 → ip_hash 粘性 → 健康检查

Step 8  ─ OpenLDAP 集成
           Docker 部署 → LDIF 初始化 → libldap 认证 → 用户同步

Step 9  ─ TCP + WebSocket 双协议
           Chat 节点双端口 → Nginx 协议分流

Step 10 ─ 公网部署
           阿里云 ECS → 域名 → SSL → 安全组

Step 11 ─ Prometheus + Grafana
           指标埋点 → 采集 → 大屏

Step 12 ─ Go 压测 + 调优
           并发连接 / 消息吞吐 / 延迟 / 节点故障 / 伸缩 / 撤回流程
```

---

## 11. Go 压测方案

### 12.1 工具结构

```
loadtester/
├── main.go           # 入口：协程池管理
├── client.go         # TCP / WS 客户端实现
├── scenario.go       # 场景定义（登录 → 发消息 → 退出）
├── metrics.go        # 本地统计（延迟 / 吞吐 / 错误率）
└── report.go         # 结果输出（表格 / JSON）
```

### 12.2 压测场景

| 场景 | 并发量 | 目标 |
|------|--------|------|
| 单节点连接上限 | 1k / 5k / 10k | 最大在线连接数 |
| 房间消息广播 | 1000 人在线 + 持续发消息 | 每秒广播量、延迟 P99 |
| 私聊吞吐 | 500 对并发私聊 | 私聊延迟 P99 |
| 节点故障容灾 | 持续连接中停掉一个节点 | 重连时间、丢消息率 |
| 集群伸缩 | 3 节点 → 5 节点 → 缩回 3 | 吞吐线性增长比 |

---

## 13. 项目目录规划

```
Muduo-chat-Mash/
├── DESIGN.md                  # 本设计文档
├── CMakeLists.txt             # 顶层 CMake
├── cmake/                     # CMake 模块
├── proto/                     # Protobuf 定义
│   └── chat.proto
├── src/                       # Chat Server 源代码
│   ├── chat_server.h/cc       # 服务器入口
│   ├── connection_manager.h/cc# 连接管理
│   ├── room_manager.h/cc      # 房间管理
│   ├── friend_manager.h/cc    # 好友关系管理
│   ├── recall_manager.h/cc    # 消息撤回逻辑
│   ├── redis_client.h/cc      # Redis Stream + Pub/Sub
│   ├── db_client.h/cc         # MySQL 客户端
│   ├── ldap_auth.h/cc         # LDAP 认证
│   ├── ws_server.h/cc         # WebSocket 支持
│   ├── prometheus_exporter.h/cc# /metrics 端点
│   └── main.cc                # main()
├── client/                    # 测试客户端
│   └── chat_client.cc
├── deploy/                    # 部署相关
│   ├── Dockerfile             # Chat Server 镜像
│   ├── docker-compose.yml     # 全套编排
│   ├── nginx/
│   │   └── nginx.conf
│   └── ldap/
│       └── init.ldif
├── monitoring/                # 监控配置
│   ├── prometheus.yml
│   └── dashboards/
│       └── chat-dashboard.json
├── loadtester/                # Go 压测工具
│   └── main.go
└── scripts/                   # 辅助脚本
    ├── build.sh
    └── deploy.sh
```
