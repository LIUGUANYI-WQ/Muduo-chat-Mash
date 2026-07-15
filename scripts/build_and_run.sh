#!/bin/bash

set -e

echo "=== Building Chat Server ==="
cd /mnt/c/Users/94173/Desktop/task
rm -rf build
mkdir -p build
cd build
cmake ..
make -j$(nproc)

echo "=== Running Chat Server ==="
echo "Make sure MySQL is configured with:"
echo "  - Database: chat_db"
echo "  - User: chat_user"
echo "  - Password: chat_password"
echo ""
echo "If not configured, run:"
echo "  sudo mysql -u root"
echo "  CREATE DATABASE chat_db CHARACTER SET utf8mb4;"
echo "  CREATE USER 'chat_user'@'localhost' IDENTIFIED BY 'chat_password';"
echo "  GRANT ALL ON chat_db.* TO 'chat_user'@'localhost';"
echo "  FLUSH PRIVILEGES;"
echo ""

./src/chat_server 0.0.0.0 9000
