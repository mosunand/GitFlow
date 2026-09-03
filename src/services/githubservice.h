#pragma once
#include <QObject>
#include <QJsonArray>
#include <QUrlQuery>
#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QHttpMultiPart;

// GitHub / Gitee REST API 基类：自动继承系统代理
class RestService : public QObject {
    Q_OBJECT
public:
    explicit RestService(const QString &token = {}, const QString &baseUrl = {},
                         QObject *parent = nullptr);
    ~RestService() override;

    using Callback = std::function<void(bool ok, const QJsonArray &arr, const QJsonObject &obj, const QString &err)>;

    void get(const QString &path, const Callback &cb, const QUrlQuery &query = {});
    void post(const QString &path, const QJsonObject &body, const Callback &cb);
    // Gitee 等表单协议接口：application/x-www-form-urlencoded
    void postForm(const QString &path, const QUrlQuery &form, const Callback &cb);
    // 附件上传：multipart/form-data（Gitee attach_files 要求）
    void postMultipart(const QUrl &url, QHttpMultiPart *multi, const Callback &cb);
    void postRaw(const QUrl &url, const QByteArray &data, const QString &contentType, const Callback &cb);

protected:
    QNetworkAccessManager *m_nam = nullptr;
    QString m_token;
    QString m_baseUrl;
};

// ── GitHub ──
class GitHubService : public RestService {
    Q_OBJECT
public:
    explicit GitHubService(const QString &token = {}, QObject *parent = nullptr);

    void verifyUser(const Callback &cb);                       // GET /user
    void listMyRepos(const Callback &cb);
    void listUserRepos(const QString &username, const Callback &cb);
    void getRepo(const QString &owner, const QString &repo, const Callback &cb);
    void forkRepo(const QString &owner, const QString &repo, const Callback &cb);
    void createRepo(const QString &name, const QString &desc, bool privateRepo,
                    bool autoInit, const QString &gitignoreTpl, const QString &licenseTpl, const Callback &cb);
    void createRelease(const QString &owner, const QString &repo, const QString &tag,
                       const QString &name, const QString &body, bool prerelease, const Callback &cb);
    void uploadAsset(const QString &owner, const QString &repo, qint64 releaseId,
                     const QString &filePath, const Callback &cb);
};
