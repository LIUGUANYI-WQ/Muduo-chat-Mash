#ifndef MYSQL_MESSAGE_REPO_H
#define MYSQL_MESSAGE_REPO_H

#include "message_repository.h"
#include "../db.h"

class MySQLMessageRepo : public MessageRepository {
public:
    MySQLMessageRepo(MySQLPool& pool) : pool_(pool) {}

    int64_t storeMessage(const std::string& from, const std::string& to,
                         const std::string& room, const std::string& content) override;

    std::optional<MessageInfo> getMessage(int64_t msg_id) override;

    bool recallMessage(int64_t msg_id, const std::string& uid) override;

    bool addOfflineMessage(const std::string& target_uid, int64_t msg_id) override;

    std::vector<MessageInfo> getUndeliveredMessages(const std::string& uid) override;

    bool markMessagesDelivered(const std::string& uid) override;

private:
    MySQLPool& pool_;
};

#endif
