CREATE TABLE IF NOT EXISTS friendships (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    requester_uid VARCHAR(64) NOT NULL,
    target_uid VARCHAR(64) NOT NULL,
    status TINYINT DEFAULT 0,
    message VARCHAR(256) DEFAULT '',
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    updated_at DATETIME DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
    UNIQUE KEY uk_friendship (requester_uid, target_uid),
    FOREIGN KEY (requester_uid) REFERENCES users(uid),
    FOREIGN KEY (target_uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_friendships_uid ON friendships(requester_uid, status);
CREATE INDEX idx_friendships_target ON friendships(target_uid, status);
