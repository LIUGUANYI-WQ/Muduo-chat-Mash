#!/usr/bin/env python3
import hashlib

lines = []
for i in range(1000):
    uid = f"bench{i}"
    pwd_hash = hashlib.sha256(b"bench123").hexdigest()
    lines.append(f"('{uid}', '{pwd_hash}')")

sql = f"""INSERT IGNORE INTO users (uid, passwd) VALUES
{','.join(lines)};

SELECT COUNT(*) AS total FROM users WHERE uid LIKE 'bench%';
"""

with open("/mnt/c/Users/94173/Desktop/task/benchmark/seed.sql", "w") as f:
    f.write(sql)

print("seed.sql generated")
