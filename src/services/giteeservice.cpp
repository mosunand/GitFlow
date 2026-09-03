#include "giteeservice.h"
#include <QUrlQuery>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>

namespace {
const char *kBase = "https://gitee.com/api/v5";
} // namespace

GiteeService::GiteeService(const QString &token, QObject *parent)
    : RestService(token, kBase, parent) {}

void GiteeService::verifyUser(const Callback &cb) { get("/user", cb); }

void GiteeService::listMyRepos(const Callback &cb) {
    QUrlQuery q;
    q.addQueryItem("per_page", "100");
    get("/user/repos", cb, q);
}

void GiteeService::listUserRepos(const QString &username, const Callback &cb) {
    QUrlQuery q;
    q.addQueryItem("per_page", "100");
    get(QStringLiteral("/users/%1/repos").arg(username), cb, q);
}

void GiteeService::getRepo(const QString &owner, const QString &repo, const Callback &cb) {
    get(QStringLiteral("/repos/%1/%2").arg(owner, repo), cb);
}

void GiteeService::forkRepo(const QString &owner, const QString &repo, const Callback &cb) {
    post(QStringLiteral("/repos/%1/%2/forks").arg(owner, repo), {}, cb);
}

void GiteeService::createRepo(const QString &name, const QString &desc, bool privateRepo,
                              bool autoInit, const Callback &cb) {
    QJsonObject body;
    body.insert("name", name);
    body.insert("description", desc);
    body.insert("private", privateRepo);
    body.insert("auto_init", autoInit);
    RestService::post("/user/repos", body, cb);
}

void GiteeService::createRelease(const QString &owner, const QString &repo, const QString &tag,
                                 const QString &name, const QString &body_, bool prerelease,
                                 const Callback &cb) {
    QJsonObject body;
    body.insert("tag_name", tag);
    body.insert("name", name);
    body.insert("body", body_);
    body.insert("prerelease", prerelease);
    RestService::post(QStringLiteral("/repos/%1/%2/releases").arg(owner, repo), body, cb);
}

void GiteeService::uploadAsset(const QString &owner, const QString &repo, qint64 releaseId,
                               const QString &filePath, const Callback &cb) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        cb(false, QJsonArray{}, QJsonObject{}, QStringLiteral("cannot open %1").arg(filePath));
        return;
    }
    const QString name = QFileInfo(filePath).fileName();
    QUrl url(QStringLiteral("%1/repos/%2/%3/releases/%4/attachment")
                 .arg(m_baseUrl).arg(owner, repo).arg(releaseId));
    QUrlQuery q;
    q.addQueryItem("name", name);
    url.setQuery(q);
    postRaw(url, f.readAll(), "application/octet-stream", cb);
}
