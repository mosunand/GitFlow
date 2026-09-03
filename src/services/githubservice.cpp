#include "githubservice.h"
#include "proxy.h"
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QNetworkProxy>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QFile>

namespace {
const char *kBase = "https://api.github.com";
const char *kUploadBase = "https://uploads.github.com";

void applyAuth(QNetworkRequest &req, const QString &token, int timeoutMs = 30000) {
    req.setRawHeader("User-Agent", "GitFlow-Pro");
    req.setRawHeader("Accept", "application/json");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    req.setTransferTimeout(timeoutMs);
    if (!token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + token.toUtf8());
}

QString apiError(QNetworkReply *reply, const QByteArray &data) {
    if (reply->error() == QNetworkReply::NoError) return {};
    const QJsonObject o = QJsonDocument::fromJson(data).object();
    // GitHub 用 {"message": "..."}，Gitee 校验失败用 {"messages": ["...", ...]}
    const QString msg = o.value("message").toString();
    if (!msg.isEmpty()) return msg;
    const QJsonArray msgs = o.value("messages").toArray();
    if (!msgs.isEmpty()) {
        QStringList list;
        for (const auto &m : msgs) list << m.toString();
        return list.join("; ");
    }
    return reply->errorString();
}

void finishReply(QNetworkReply *reply, const RestService::Callback &cb) {
    const QByteArray data = reply->readAll();
    const QString err = apiError(reply, data);
    reply->deleteLater();
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    cb(err.isEmpty(), doc.array(), doc.object(), err);
}
} // namespace

RestService::RestService(const QString &token, const QString &baseUrl, QObject *parent)
    : QObject(parent), m_token(token), m_baseUrl(baseUrl) {
    m_nam = new QNetworkAccessManager(this);
    const QString proxy = proxy::detectSystemProxy();
    if (!proxy.isEmpty()) {
        const QUrl u(proxy);
        QNetworkProxy p(QNetworkProxy::HttpProxy, u.host(), u.port(80));
        if (!u.userInfo().isEmpty()) {
            p.setUser(u.userInfo().section(':', 0, 0));
            p.setPassword(u.userInfo().section(':', 1));
        }
        m_nam->setProxy(p);
    } else {
        m_nam->setProxy(QNetworkProxy::NoProxy);
    }
}

RestService::~RestService() {
    if (!m_nam) return;
    const auto replies = m_nam->findChildren<QNetworkReply *>();
    for (auto *r : replies)
        r->abort();
}

void RestService::get(const QString &path, const Callback &cb, const QUrlQuery &query) {
    QUrl url(m_baseUrl + path);
    if (!query.isEmpty()) url.setQuery(query);
    QNetworkRequest req(url);
    applyAuth(req, m_token);
    QNetworkReply *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] { finishReply(reply, cb); });
}

void RestService::post(const QString &path, const QJsonObject &body, const Callback &cb) {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    applyAuth(req, m_token);
    QNetworkReply *reply = m_nam->post(req, QJsonDocument(body).toJson());
    connect(reply, &QNetworkReply::finished, this, [reply, cb] { finishReply(reply, cb); });
}

void RestService::postForm(const QString &path, const QUrlQuery &form, const Callback &cb) {
    QNetworkRequest req(QUrl(m_baseUrl + path));
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");
    applyAuth(req, m_token);
    QNetworkReply *reply = m_nam->post(req, form.toString(QUrl::FullyEncoded).toUtf8());
    connect(reply, &QNetworkReply::finished, this, [reply, cb] { finishReply(reply, cb); });
}

void RestService::postMultipart(const QUrl &url, QHttpMultiPart *multi, const Callback &cb) {
    QNetworkRequest req(url);
    applyAuth(req, m_token, 300000);   // 上传给更长超时
    QNetworkReply *reply = m_nam->post(req, multi);
    multi->setParent(reply);           // 随 reply 一起释放
    connect(reply, &QNetworkReply::finished, this, [reply, cb] { finishReply(reply, cb); });
}

void RestService::postRaw(const QUrl &url, const QByteArray &data,
                          const QString &contentType, const Callback &cb) {
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    applyAuth(req, m_token, 300000);   // 上传给更长超时
    QNetworkReply *reply = m_nam->post(req, data);
    connect(reply, &QNetworkReply::finished, this, [reply, cb] { finishReply(reply, cb); });
}

// ── GitHub ──
GitHubService::GitHubService(const QString &token, QObject *parent)
    : RestService(token, kBase, parent) {}

void GitHubService::verifyUser(const Callback &cb) { get("/user", cb); }

void GitHubService::listMyRepos(const Callback &cb) {
    QUrlQuery q;
    q.addQueryItem("per_page", "100");
    q.addQueryItem("sort", "updated");
    get("/user/repos", cb, q);
}

void GitHubService::listUserRepos(const QString &username, const Callback &cb) {
    QUrlQuery q;
    q.addQueryItem("per_page", "100");
    get(QStringLiteral("/users/%1/repos").arg(username), cb, q);
}

void GitHubService::getRepo(const QString &owner, const QString &repo, const Callback &cb) {
    get(QStringLiteral("/repos/%1/%2").arg(owner, repo), cb);
}

void GitHubService::forkRepo(const QString &owner, const QString &repo, const Callback &cb) {
    post(QStringLiteral("/repos/%1/%2/forks").arg(owner, repo), {}, cb);
}

void GitHubService::createRepo(const QString &name, const QString &desc, bool privateRepo,
                               bool autoInit, const QString &gitignoreTpl,
                               const QString &licenseTpl, const Callback &cb) {
    QJsonObject body { {"name", name}, {"description", desc}, {"private", privateRepo} };
    if (autoInit) body.insert("auto_init", true);
    if (!gitignoreTpl.isEmpty()) body.insert("gitignore_template", gitignoreTpl);
    if (!licenseTpl.isEmpty()) body.insert("license_template", licenseTpl);
    post("/user/repos", body, cb);
}

void GitHubService::createRelease(const QString &owner, const QString &repo, const QString &tag,
                                  const QString &name, const QString &body, bool prerelease,
                                  const Callback &cb) {
    post(QStringLiteral("/repos/%1/%2/releases").arg(owner, repo),
         { {"tag_name", tag}, {"name", name}, {"body", body}, {"prerelease", prerelease} }, cb);
}

void GitHubService::uploadAsset(const QString &owner, const QString &repo, qint64 releaseId,
                                const QString &filePath, const Callback &cb) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        cb(false, QJsonArray{}, QJsonObject{}, QStringLiteral("cannot open %1").arg(filePath));
        return;
    }
    const QString name = QFileInfo(filePath).fileName();
    QUrl url(QStringLiteral("%1/repos/%2/%3/releases/%4/assets")
                 .arg(kUploadBase).arg(owner, repo).arg(releaseId));
    QUrlQuery q;
    q.addQueryItem("name", name);
    url.setQuery(q);
    postRaw(url, f.readAll(), "application/octet-stream", cb);
}
