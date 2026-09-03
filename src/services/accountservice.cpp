#include "accountservice.h"
#include "githubservice.h"
#include "giteeservice.h"
#include "crypto.h"
#include "paths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>

namespace {
QString platformOf(const QString &platform) { return platform; } // "github"/"gitee"
}

AccountService::AccountService(QObject *parent) : QObject(parent) {}

QString AccountService::accountFile(const QString &username, const QString &platform) const {
    QString safe = username;
    safe.replace('/', '_').replace('\\', '_');
    // accountsDir() 本身已含 /accounts，模板里不能再拼一遍（会写进不存在的 accounts/accounts/）
    return QStringLiteral("%1/%2_%3.json")
        .arg(paths::accountsDir(), platform, safe);
}

bool AccountService::hasAccounts() const { return !listUsernames().isEmpty(); }

QStringList AccountService::listUsernames() const {
    QStringList out;
    const QDir dir(paths::accountsDir());
    for (const QFileInfo &fi : dir.entryInfoList({ "*.json" }, QDir::Files))
        out.append(fi.completeBaseName()); // "github_mosunand"
    return out;
}

bool AccountService::hasAccount(const QString &username, const QString &platform) const {
    return QFileInfo::exists(accountFile(username, platform));
}

Account AccountService::loadAccount(const QString &username, const QString &platform) const {
    QFile f(accountFile(username, platform));
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject crypto = o.value("crypto").toObject();
    Account a;
    a.username = o.value("username").toString();
    a.platform = o.value("platform").toString("github");
    a.expiresAt = o.value("token_expires_at").toString();
    a.keyB64 = crypto.value("key").toString();
    a.nonceB64 = crypto.value("nonce").toString();
    a.token = crypto::decrypt(o.value("credential").toObject().value("token").toString(),
                              QByteArray::fromBase64(a.keyB64.toUtf8()),
                              QByteArray::fromBase64(a.nonceB64.toUtf8()));
    return a;
}

void AccountService::saveAccount(const Account &acct) {
    const QByteArray key = crypto::randomBytes(32);
    QByteArray nonce;
    const QString cipher = crypto::encrypt(acct.token, key, &nonce);

    QJsonObject cryptoObj {
        {"algorithm", "AES-256-GCM"},
        {"key", QString::fromUtf8(key.toBase64())},
        {"nonce", QString::fromUtf8(nonce.toBase64())},
    };
    QJsonObject o {
        {"version", 1},
        {"username", acct.username},
        {"platform", acct.platform},
        {"token_expires_at", acct.expiresAt},
        {"crypto", cryptoObj},
        {"credential", QJsonObject { {"token", cipher} }},
    };
    QFile f(accountFile(acct.username, acct.platform));
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    setCurrent(acct.username, acct.platform);
}

void AccountService::removeAccount(const QString &username, const QString &platform) {
    QFile::remove(accountFile(username, platform));
}

Account AccountService::currentAccount() const {
    QFile f(paths::accountsDir() + "/current.txt");
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QStringList parts = QString::fromUtf8(f.readAll()).trimmed().split('|');
    if (parts.size() != 2) return {};
    return loadAccount(parts[0], parts[1]);
}

void AccountService::setCurrent(const QString &username, const QString &platform) {
    QFile f(paths::accountsDir() + "/current.txt");
    if (f.open(QIODevice::WriteOnly))
        f.write(QStringLiteral("%1|%2").arg(username, platform).toUtf8());
}

GitHubService *AccountService::github() const {
    const Account a = currentAccount();
    return new GitHubService(a.token, const_cast<AccountService *>(this));
}

GiteeService *AccountService::gitee() const {
    const Account a = currentAccount();
    return new GiteeService(a.token, const_cast<AccountService *>(this));
}
