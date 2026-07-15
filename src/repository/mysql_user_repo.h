#ifndef MYSQL_USER_REPO_H
#define MYSQL_USER_REPO_H

#include "user_repository.h"
#include "../db.h"

class MySQLUserRepo : public UserRepository {
public:
    MySQLUserRepo(MySQLPool& pool) : pool_(pool) {}

    bool registerUser(const std::string& uid, const std::string& passwd,
                      const std::string& nickname = "",
                      const std::string& email = "") override;

    bool verifyUser(const std::string& uid, const std::string& passwd) override;

    bool userExists(const std::string& uid) override;

    std::optional<UserInfo> getUserInfo(const std::string& uid) override;

    bool updateLastLogin(const std::string& uid) override;

private:
    MySQLPool& pool_;
};

#endif
