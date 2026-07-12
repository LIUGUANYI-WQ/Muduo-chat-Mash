#ifndef MUDUO_CHAT_DB_H
#define MUDUO_CHAT_DB_H

#include <string>
#include <mysql/mysql.h>

class MySQL {
public:
    MySQL();
    ~MySQL();

    bool init(const std::string& host, const std::string& user,
              const std::string& password, const std::string& database);
    bool registerUser(const std::string& uid, const std::string& passwd);
    bool verifyUser(const std::string& uid, const std::string& passwd);
    bool userExists(const std::string& uid);

private:
    bool ensureTable();
    MYSQL* conn_;
};

#endif
