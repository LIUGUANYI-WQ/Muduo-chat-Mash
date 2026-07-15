CREATE TABLE IF NOT EXISTS sessions (
    id BIGINT AUTO_INCREMENT PRIMARY KEY,
    uid VARCHAR(64) NOT NULL,
    token VARCHAR(128) NOT NULL UNIQUE,
    node_id VARCHAR(64) NOT NULL,
    ip_address VARCHAR(45),
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    expired_at DATETIME NOT NULL,
    revoked TINYINT DEFAULT 0,
    FOREIGN KEY (uid) REFERENCES users(uid)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
CREATE INDEX idx_sessions_uid ON sessions(uid, revoked);
CREATE INDEX idx_sessions_expire ON sessions(expired_at);
