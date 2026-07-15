#ifndef USER_REPOSITORY_H
#define USER_REPOSITORY_H

#include "../db.h"

#include <optional>

class UserRepository {
public:
    virtual ~UserRepository() = default;

    virtual bool registerUser(const std::string& uid, const std::string& passwd,
                              const std::string& nickname = "",
                              const std::string& email = "") = 0;

    virtual bool verifyUser(const std::string& uid, const std::string& passwd) = 0;

    virtual bool userExists(const std::string& uid) = 0;

    virtual std::optional<UserInfo> getUserInfo(const std::string& uid) = 0;

    virtual bool updateLastLogin(const std::string& uid) = 0;
};

#endif
