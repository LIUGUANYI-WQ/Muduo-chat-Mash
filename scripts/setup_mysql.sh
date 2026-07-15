#!/bin/bash

echo "=== Setting up MySQL for Chat Server ==="

sudo mysql -u root <<EOF
CREATE DATABASE IF NOT EXISTS chat_db CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci;
CREATE USER IF NOT EXISTS 'chat_user'@'localhost' IDENTIFIED BY 'chat_password';
GRANT ALL PRIVILEGES ON chat_db.* TO 'chat_user'@'localhost';
FLUSH PRIVILEGES;
EOF

echo "=== Verifying setup ==="
mysql -u chat_user -pchat_password -e "USE chat_db; SHOW TABLES;" 2>&1 || echo "Tables may not exist yet (normal for first run)"

echo "=== Setup complete ==="
