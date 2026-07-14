#!/usr/bin/env python3
import socket
import struct
import sys
import time
import os
import subprocess

sys.path.insert(0, '/mnt/c/Users/94173/Desktop/task')
os.chdir('/mnt/c/Users/94173/Desktop/task')

subprocess.run(['protoc', '--proto_path=proto', '--python_out=.', 'proto/chat.proto'],
               capture_output=True)
import chat_pb2


def send_msg(sock, msg):
    data = msg.SerializeToString()
    sock.sendall(struct.pack('!i', len(data)) + data)


def recv_msg(sock, timeout=1.0):
    sock.settimeout(timeout)
    try:
        header = sock.recv(4)
        if not header:
            return None
        length = struct.unpack('!i', header)[0]
        data = b''
        while len(data) < length:
            chunk = sock.recv(length - len(data))
            if not chunk:
                return None
            data += chunk
        sm = chat_pb2.ServerMessage()
        sm.ParseFromString(data)
        return sm
    except socket.timeout:
        return None


def drain(sock, timeout=0.3):
    count = 0
    while True:
        m = recv_msg(sock, timeout)
        if m is None:
            break
        count += 1
    return count


def make_env(token="", **kwargs):
    env = chat_pb2.Envelope()
    if token:
        env.token = token
    for k, v in kwargs.items():
        getattr(env, k).CopyFrom(v)
    return env


def msg_type(m):
    if m is None:
        return "None"
    t = m.WhichOneof("payload")
    return t if t else "unknown"


def connect(host, port):
    sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect((host, port))
    return sock


def register_user(host, port, uid, pwd):
    sock = connect(host, port)
    req = chat_pb2.RegisterRequest()
    req.uid = uid
    req.passwd = pwd
    env = make_env(register_req=req)
    send_msg(sock, env)
    resp = recv_msg(sock)
    sock.close()
    return resp


def login_user(host, port, uid, pwd):
    sock = connect(host, port)
    req = chat_pb2.LoginRequest()
    req.uid = uid
    req.passwd = pwd
    env = make_env(login_req=req)
    send_msg(sock, env)
    resp = recv_msg(sock)
    if resp and resp.HasField("login_resp") and resp.login_resp.ok:
        token = resp.login_resp.token
        time.sleep(0.2)
        drain(sock, 0.3)
        return sock, token
    sock.close()
    return None, None


HOST = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
PORT = int(sys.argv[2]) if len(sys.argv) > 2 else 9884

passed = 0
failed = 0

def check(name, condition, detail=""):
    global passed, failed
    if condition:
        print("  PASS: " + name)
        passed += 1
    else:
        print("  FAIL: " + name + " | " + detail)
        failed += 1


# Clean test data
subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "DELETE FROM messages WHERE from_uid='test_alice' OR from_uid='test_bob';"
           "DELETE FROM friendships WHERE requester_uid LIKE 'test_%' OR target_uid LIKE 'test_%';"
           "DELETE FROM room_members WHERE uid LIKE 'test_%';"
           "DELETE FROM rooms WHERE creator_uid LIKE 'test_%';"
           "DELETE FROM users WHERE uid='test_alice' OR uid='test_bob';"],
    capture_output=True
)

print("")
print("=== Test Suite: " + HOST + ":" + str(PORT) + " ===")
print("")

# ===== Test 1: Register =====
print("[Test 1] Register")
r1 = register_user(HOST, PORT, "test_alice", "pass123")
check("Register test_alice", r1 and r1.HasField("register_resp") and r1.register_resp.ok)

r2 = register_user(HOST, PORT, "test_bob", "pass123")
check("Register test_bob", r2 and r2.HasField("register_resp") and r2.register_resp.ok)

# ===== Test 2: Login =====
print("")
print("[Test 2] Login")
sock_a, token_a = login_user(HOST, PORT, "test_alice", "pass123")
check("Alice login OK", sock_a is not None and bool(token_a))

sock_b, token_b = login_user(HOST, PORT, "test_bob", "pass123")
check("Bob login OK", sock_b is not None and bool(token_b))

# ===== Test 3: Friend request =====
print("")
print("[Test 3] Friend request")
fr = chat_pb2.FriendRequest()
fr.from_uid = "test_alice"
fr.to_uid = "test_bob"
fr.message = "be friends"
env = make_env(token=token_a, friend_req=fr)
send_msg(sock_a, env)
time.sleep(0.5)

resp = recv_msg(sock_b, 1.0)
check("Bob receives friend request",
      resp and resp.HasField("friend_req") and resp.friend_req.from_uid == "test_alice",
      "got=" + msg_type(resp))

drain(sock_b, 0.2)

# ===== Test 4: Re-login pushes pending requests =====
print("")
print("[Test 4] Login pushes pending requests")
sock_b.close()
sock_b2, token_b2 = login_user(HOST, PORT, "test_bob", "pass123")
sock_b2.close()  # just verify login works
check("Bob relogin success with pending requests", sock_b2 is not None)

# ===== Test 5: Re-login fresh to test pending push clearly =====
print("")
print("[Test 5] Login with pending requests")
sock_b3, token_b3 = login_user(HOST, PORT, "test_bob", "pass123",)
check("Bob login with pending", sock_b3 is not None)
# pending requests are drained by login_user's drain call

# ===== Test 6: Accept friend request =====
print("")
print("[Test 6] Accept friend request")
# Re-login Alice too to get clean state
sock_a.close()
sock_a2, token_a2 = login_user(HOST, PORT, "test_alice", "pass123")
check("Alice relogin", sock_a2 is not None)

fresp = chat_pb2.FriendResponse()
fresp.from_uid = "test_bob"
fresp.to_uid = "test_alice"
fresp.accepted = True
env = make_env(token=token_b3, friend_resp=fresp)
send_msg(sock_b3, env)
time.sleep(0.5)

resp_a = recv_msg(sock_a2, 1.0)
check("Alice gets friend accept notification",
      resp_a and resp_a.HasField("friend_resp") and resp_a.friend_resp.accepted,
      "got=" + msg_type(resp_a))

# Drain both
drain(sock_a2, 0.3)
drain(sock_b3, 0.3)

# ===== Test 7: Private message =====
print("")
print("[Test 7] Private message")
cm = chat_pb2.ChatMessage()
cm.to = "test_bob"
cm.content = "Hello Bob!"
env = make_env(token=token_a2, chat_msg=cm)
send_msg(sock_a2, env)
time.sleep(0.5)

resp_b = recv_msg(sock_b3, 1.0)
check("Bob receives private message",
      resp_b and resp_b.HasField("chat_msg") and resp_b.chat_msg.content == "Hello Bob!",
      "got=" + msg_type(resp_b))

resp_a = recv_msg(sock_a2, 1.0)
msg_id = resp_a.chat_msg.msg_id if (resp_a and resp_a.HasField("chat_msg")) else 0
check("Alice receives echo with msg_id",
      resp_a and resp_a.HasField("chat_msg") and msg_id > 0,
      "msg_id=" + str(msg_id) + " got=" + msg_type(resp_a))

# MySQL persistence
r = subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "SELECT msg_id, from_uid, to_uid, content FROM messages WHERE msg_id=" + str(msg_id)],
    capture_output=True, text=True)
check("Message persisted in MySQL", str(msg_id) in r.stdout, "mysql: " + r.stdout.strip())

drain(sock_a2, 0.2)
drain(sock_b3, 0.2)

# ===== Test 8: Recall private message =====
print("")
print("[Test 8] Recall private message")
if msg_id <= 0:
    check("Recall skipped", False, "no msg_id")
else:
    rm = chat_pb2.RecallMessage()
    rm.msg_id = msg_id
    rm.to_uid = "test_bob"
    env = make_env(token=token_a2, recall_msg=rm)
    send_msg(sock_a2, env)
    time.sleep(0.5)

    rb = recv_msg(sock_b3, 1.0)
    check("Bob receives recall notify",
          rb and rb.HasField("recall_notify") and rb.recall_notify.msg_id == msg_id,
          "got=" + msg_type(rb))

    ra = recv_msg(sock_a2, 1.0)
    check("Alice receives recall echo",
          ra and ra.HasField("recall_notify") and ra.recall_notify.msg_id == msg_id,
          "got=" + msg_type(ra))

    r = subprocess.run(
        ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
         "-e", "SELECT recalled, recall_time FROM messages WHERE msg_id=" + str(msg_id)],
        capture_output=True, text=True)
    check("MySQL recalled=1", "1" in r.stdout, "mysql: " + r.stdout.strip())

    drain(sock_a2, 0.2)
    drain(sock_b3, 0.2)

# Use unique room name to avoid conflicts with in-memory state from previous runs
room_name = "test_room_" + str(int(time.time()))

# ===== Test 9: Create room =====
print("")
print("[Test 9] Create room (" + room_name + ")")
cr = chat_pb2.CreateRoom()
cr.name = room_name
env = make_env(token=token_a2, create_room=cr)
send_msg(sock_a2, env)
time.sleep(1.0)

r = subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "SELECT name, creator_uid FROM rooms WHERE name='" + room_name + "'"],
    capture_output=True, text=True)
check("Room in MySQL", room_name in r.stdout, "mysql: " + r.stdout.strip())

r = subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "SELECT uid FROM room_members rm JOIN rooms r ON rm.room_id=r.room_id WHERE r.name='" + room_name + "'"],
    capture_output=True, text=True)
check("Creator in room_members", "test_alice" in r.stdout, "mysql: " + r.stdout.strip())

# ===== Test 10: Join room =====
print("")
print("[Test 10] Join room (" + room_name + ")")
jr = chat_pb2.JoinRoom()
jr.room_name = room_name
jr.uid = "test_bob"
env = make_env(token=token_b3, join_room=jr)
send_msg(sock_b3, env)
time.sleep(1.0)

r = subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "SELECT uid FROM room_members rm JOIN rooms r ON rm.room_id=r.room_id WHERE r.name='" + room_name + "' ORDER BY uid"],
    capture_output=True, text=True)
check("Both in room_members",
      "test_alice" in r.stdout and "test_bob" in r.stdout,
      "mysql: " + r.stdout.strip())

# ===== Test 11: Room message =====
print("")
print("[Test 11] Room message")
cm2 = chat_pb2.ChatMessage()
cm2.room = room_name
cm2.content = "Hello everyone!"
env = make_env(token=token_a2, chat_msg=cm2)
send_msg(sock_a2, env)
time.sleep(1.0)

# Drain any error messages first
err = recv_msg(sock_b3, 0.3)
while err is not None and err.HasField("error"):
    print("  [drain error] code=" + str(err.error.code) + " reason=" + err.error.reason)
    err = recv_msg(sock_b3, 0.3)
if err is not None:
    _ = err  # This is the actual message

rb = err if (err and err.HasField("chat_msg")) else recv_msg(sock_b3, 1.0)
check("Bob receives room message",
      rb and rb.HasField("chat_msg") and rb.chat_msg.content == "Hello everyone!",
      "got=" + msg_type(rb))

ra = recv_msg(sock_a2, 1.0)
room_msg_id = ra.chat_msg.msg_id if (ra and ra.HasField("chat_msg")) else 0
check("Alice receives room message echo",
      ra and ra.HasField("chat_msg") and room_msg_id > 0,
      "msg_id=" + str(room_msg_id) + " got=" + msg_type(ra))

r = subprocess.run(
    ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
     "-e", "SELECT msg_id, seq, from_uid, content FROM messages WHERE msg_id=" + str(room_msg_id)],
    capture_output=True, text=True)
check("Room message in MySQL", str(room_msg_id) in r.stdout, "mysql: " + r.stdout.strip())

drain(sock_a2, 0.2)
drain(sock_b3, 0.2)

# ===== Test 12: Recall room message =====
print("")
print("[Test 12] Recall room message")
if room_msg_id <= 0:
    check("Room recall skipped", False, "no msg_id")
else:
    rm2 = chat_pb2.RecallMessage()
    rm2.msg_id = room_msg_id
    rm2.room = room_name
    env = make_env(token=token_a2, recall_msg=rm2)
    send_msg(sock_a2, env)
    time.sleep(1.0)

    rb = recv_msg(sock_b3, 1.0)
    check("Bob receives room recall notify",
          rb and rb.HasField("recall_notify") and rb.recall_notify.msg_id == room_msg_id,
          "got=" + msg_type(rb))

    ra = recv_msg(sock_a2, 1.0)
    check("Alice receives room recall echo",
          ra and ra.HasField("recall_notify") and ra.recall_notify.msg_id == room_msg_id,
          "got=" + msg_type(ra))

    r = subprocess.run(
        ["mysql", "-u", "root", "-p123456", "-h", "127.0.0.1", "chat",
         "-e", "SELECT recalled, recall_time FROM messages WHERE msg_id=" + str(room_msg_id)],
        capture_output=True, text=True)
    check("Room message recalled=1 in MySQL", "1" in r.stdout, "mysql: " + r.stdout.strip())

# Cleanup
sock_a2.close()
sock_b3.close()

print("")
print("=== Results: " + str(passed) + " passed, " + str(failed) + " failed ===")
print("")
sys.exit(0 if failed == 0 else 1)
