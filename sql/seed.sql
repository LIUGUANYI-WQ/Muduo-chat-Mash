-- Chat 数据库完整建表
-- 按 DESIGN.md 第 3.2 节

CREATE DATABASE IF NOT EXISTS chat DEFAULT CHARSET utf8mb4;
USE chat;

-- 用户表
CREATE TABLE IF NOT EXISTS users (
    uid VARCHAR(64) PRIMARY KEY,
    nickname VARCHAR(128) NOT NULL DEFAULT '',
    email VARCHAR(256) NOT NULL DEFAULT '',
    passwd VARCHAR(128) NOT NULL,
    avatar_url TEXT,
    created_at DATETIME DEFAULT CURRENT_TIMESTAMP,
    last_login DATETIME
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;

-- 如果之前已有 users 表（旧结构），补全新列
-- （这些 ALTER 在新表上无害，因为有 IF NOT EXISTS / IF EXISTS 语义，
--   但 MySQL 不支持 ALTER TABLE ADD COLUMN IF NOT EXISTS，需要先检查）
-- 手动迁移参考：
--   ALTER TABLE users ADD COLUMN nickname VARCHAR(128) NOT NULL DEFAULT '' AFTER uid;
--   ALTER TABLE users ADD COLUMN email VARCHAR(256) NOT NULL DEFAULT '' AFTER nickname;
--   ALTER TABLE users ADD COLUMN avatar_url TEXT AFTER email;
--   ALTER TABLE users ADD COLUMN last_login DATETIME AFTER created_at;
