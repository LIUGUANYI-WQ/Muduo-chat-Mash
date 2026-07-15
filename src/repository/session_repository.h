#ifndef SESSION_REPOSITORY_H
#define SESSION_REPOSITORY_H

#include <string>
#include <optional>

struct SessionInfo {
    std::string uid;
    std::string token;
    std::string node_id;
    std::string ip_address;
    std::string created_at;
    std::string expired_at;
    int revoked = 0;
    bool exists = false;
};

class SessionRepository {
public:
    virtual ~SessionRepository() = default;

    virtual bool createSession(const std::string& uid, const std::string& token,
                               const std::string& node_id, int ttlSeconds) = 0;

    virtual std::optional<SessionInfo> getSessionByToken(const std::string& token) = 0;

    virtual bool revokeSession(const std::string& token) = 0;

    virtual bool revokeAllSessions(const std::string& uid) = 0;

    virtual bool isValidSession(const std::string& token) = 0;

    virtual bool updateSessionNode(const std::string& token, const std::string& node_id) = 0;
};

#endif
