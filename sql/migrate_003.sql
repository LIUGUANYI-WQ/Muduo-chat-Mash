-- Migration 003: 房间、消息、离线消息表
-- 按 DESIGN.md 第 3.2 节

CREATE TABLE IF NOT EXISTS rooms (
    room_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    name VARCHAR(128) NOT NULL UNIQUE,
    creator_uid VARCHAR(64) NOT NULL,
    type TINYINT DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (creator_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS room_members (
    room_id BIGINT NOT NULL,
    uid VARCHAR(64) NOT NULL,
    role TINYINT DEFAULT 0,
    joined_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    PRIMARY KEY (room_id, uid),
    FOREIGN KEY (room_id) REFERENCES rooms(room_id),
    FOREIGN KEY (uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

CREATE TABLE IF NOT EXISTS messages (
    msg_id BIGINT AUTO_INCREMENT PRIMARY KEY,
    seq BIGINT NOT NULL,
    room_id BIGINT,
    from_uid VARCHAR(64) NOT NULL,
    to_uid VARCHAR(64),
    content TEXT NOT NULL,
    msg_type TINYINT DEFAULT 0,
    recalled TINYINT DEFAULT 0,
    recall_time DATETIME,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (from_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_messages_room_seq ON messages(room_id, seq);
CREATE INDEX idx_messages_private ON messages(from_uid, to_uid);

CREATE TABLE IF NOT EXISTS offline_messages (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    target_uid VARCHAR(64) NOT NULL,
    msg_id BIGINT NOT NULL,
    delivered TINYINT DEFAULT 0,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    FOREIGN KEY (target_uid) REFERENCES users(uid),
    FOREIGN KEY (msg_id) REFERENCES messages(msg_id)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_offline_undelivered ON offline_messages(target_uid, delivered);
