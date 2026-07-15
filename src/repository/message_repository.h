#ifndef MESSAGE_REPOSITORY_H
#define MESSAGE_REPOSITORY_H

#include "../db.h"

#include <optional>

class MessageRepository {
public:
    virtual ~MessageRepository() = default;

    virtual int64_t storeMessage(const std::string& from, const std::string& to,
                                  const std::string& room, const std::string& content) = 0;

    virtual std::optional<MessageInfo> getMessage(int64_t msg_id) = 0;

    virtual bool recallMessage(int64_t msg_id, const std::string& uid) = 0;

    virtual bool addOfflineMessage(const std::string& target_uid, int64_t msg_id) = 0;

    virtual std::vector<MessageInfo> getUndeliveredMessages(const std::string& uid) = 0;

    virtual bool markMessagesDelivered(const std::string& uid) = 0;
};

#endif
