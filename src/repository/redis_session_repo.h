#ifndef REDIS_SESSION_REPO_H
#define REDIS_SESSION_REPO_H

#include "session_repository.h"
#include "../redis_pool.h"

class RedisSessionRepo : public SessionRepository {
public:
    RedisSessionRepo(RedisPool& pool) : pool_(pool) {}

    bool createSession(const std::string& uid, const std::string& token,
                       const std::string& node_id, int ttlSeconds) override;

    std::optional<SessionInfo> getSessionByToken(const std::string& token) override;

    bool revokeSession(const std::string& token) override;

    bool revokeAllSessions(const std::string& uid) override;

    bool isValidSession(const std::string& token) override;

    bool updateSessionNode(const std::string& token, const std::string& node_id) override;

private:
    RedisPool& pool_;
};

#endif
