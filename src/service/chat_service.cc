#include "chat_service.h"
#include <ctime>

SendMessageResult ChatService::sendMessage(const std::string& from, const std::string& to,
                                           const std::string& room, const std::string& content) {
    SendMessageResult result;

    int64_t msg_id = msgRepo_.storeMessage(from, to, room, content);
    if (msg_id <= 0) {
        result.success = false;
        result.reason = "store message failed";
        return result;
    }

    result.success = true;
    result.msg_id = msg_id;
    return result;
}

std::optional<MessageInfo> ChatService::getMessage(int64_t msg_id) {
    return msgRepo_.getMessage(msg_id);
}

RecallResult ChatService::recallMessage(int64_t msg_id, const std::string& uid,
                                        const std::string& to, const std::string& room) {
    RecallResult result;

    auto msg = msgRepo_.getMessage(msg_id);
    if (!msg) {
        result.success = false;
        result.reason = "message not found";
        return result;
    }

    if (msg->from_uid != uid) {
        result.success = false;
        result.reason = "can only recall your own messages";
        return result;
    }

    time_t now = time(nullptr);
    struct tm tm = {};
    sscanf(msg->created_at.c_str(), "%4d-%2d-%2d %2d:%2d:%2d",
           &tm.tm_year, &tm.tm_mon, &tm.tm_mday,
           &tm.tm_hour, &tm.tm_min, &tm.tm_sec);
    tm.tm_year -= 1900;
    tm.tm_mon -= 1;
    time_t msgTime = mktime(&tm);
    if (now - msgTime > 120) {
        result.success = false;
        result.reason = "recall window expired (2 min)";
        return result;
    }

    bool recalled = msgRepo_.recallMessage(msg_id, uid);
    if (!recalled) {
        result.success = false;
        result.reason = "recall failed";
        return result;
    }

    result.success = true;
    return result;
}

bool ChatService::markMessagesDelivered(const std::string& uid) {
    return msgRepo_.markMessagesDelivered(uid);
}

void ChatService::publishMessage(const std::string& channel, const std::string& message) {
    if (pubsub_) {
        pubsub_->publish(channel, message);
    }
}
