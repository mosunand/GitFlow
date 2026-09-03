#pragma once
#include <QObject>
#include "models.h"

class GitHubService;
class GiteeService;

// 账户服务：加密存储于 data/accounts/<platform>_<user>.json，当前账户存 current.txt
class AccountService : public QObject {
    Q_OBJECT
public:
    explicit AccountService(QObject *parent = nullptr);

    bool hasAccounts() const;
    QStringList listUsernames() const;
    bool hasAccount(const QString &username, const QString &platform) const;

    Account loadAccount(const QString &username, const QString &platform) const;
    void saveAccount(const Account &acct);
    void removeAccount(const QString &username, const QString &platform);

    Account currentAccount() const;
    void setCurrent(const QString &username, const QString &platform);

    // 创建平台专用 API 服务（自动带当前账户 token）
    GitHubService *github() const;
    GiteeService *gitee() const;

private:
    QString accountFile(const QString &username, const QString &platform) const;
};
