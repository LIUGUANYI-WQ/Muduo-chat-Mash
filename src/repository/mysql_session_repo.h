#ifndef MYSQL_SESSION_REPO_H
#define MYSQL_SESSION_REPO_H

#include "session_repository.h"
#include "../db.h"

class MySQLSessionRepo : public SessionRepository {
public:
    MySQLSessionRepo(MySQLPool& pool) : pool_(pool) {}

    bool createSession(const std::string& uid, const std::string& token,
                       const std::string& node_id, int ttlSeconds) override;

    std::optional<SessionInfo> getSessionByToken(const std::string& token) override;

    bool revokeSession(const std::string& token) override;

    bool revokeAllSessions(const std::string& uid) override;

    bool isValidSession(const std::string& token) override;

    bool updateSessionNode(const std::string& token, const std::string& node_id) override;

private:
    MySQLPool& pool_;
};

#endif
