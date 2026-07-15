#ifndef CHAT_SERVICE_H
#define CHAT_SERVICE_H

#include "../repository/message_repository.h"
#include "../repository/session_repository.h"
#include "../repository/redis_pubsub.h"

#include <string>
#include <optional>
#include <vector>

struct SendMessageResult {
    bool success = false;
    int64_t msg_id = 0;
    std::string reason;
};

struct RecallResult {
    bool success = false;
    std::string reason;
};

class ChatService {
public:
    ChatService(MessageRepository& msgRepo, SessionRepository& sessionRepo,
                RedisPubSub* pubsub = nullptr)
        : msgRepo_(msgRepo), sessionRepo_(sessionRepo), pubsub_(pubsub) {}

    SendMessageResult sendMessage(const std::string& from, const std::string& to,
                                  const std::string& room, const std::string& content);

    std::optional<MessageInfo> getMessage(int64_t msg_id);

    RecallResult recallMessage(int64_t msg_id, const std::string& uid,
                               const std::string& to = "", const std::string& room = "");

    bool markMessagesDelivered(const std::string& uid);

    void publishMessage(const std::string& channel, const std::string& message);

private:
    MessageRepository& msgRepo_;
    SessionRepository& sessionRepo_;
    RedisPubSub* pubsub_;
};

#endif
