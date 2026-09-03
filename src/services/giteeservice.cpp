#include "giteeservice.h"
#include <QUrlQuery>
#include <QHttpMultiPart>
#include <QHttpPart>
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
                                 const Callback &cb, const QString &targetCommitish) {
    // Gitee v5 实测规则（官方 Swagger 为表单参数，不解析 JSON body）：
    // ① body 必填且非空（空串报"发行版的描述不能为空"）
    // ② target_commitish 必传（目标分支，tag 不存在时在其上创建）
    // 缺任一都会 400 且 messages 数组只写"xxx is missing"
    QUrlQuery form;
    form.addQueryItem("tag_name", tag);
    form.addQueryItem("target_commitish",
                      targetCommitish.isEmpty() ? QStringLiteral("master") : targetCommitish);
    form.addQueryItem("name", name);
    form.addQueryItem("body", body_.trimmed().isEmpty() ? QStringLiteral("-") : body_);
    form.addQueryItem("prerelease", prerelease ? QStringLiteral("true") : QStringLiteral("false"));
    RestService::postForm(QStringLiteral("/repos/%1/%2/releases").arg(owner, repo), form, cb);
}

void GiteeService::uploadAsset(const QString &owner, const QString &repo, qint64 releaseId,
                               const QString &filePath, const Callback &cb) {
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly)) {
        cb(false, QJsonArray{}, QJsonObject{}, QStringLiteral("cannot open %1").arg(filePath));
        return;
    }
    const QString name = QFileInfo(filePath).fileName();
    // Gitee 附件上传为 multipart/form-data，字段名固定为 files（attach_files 端点）
    QUrl url(QStringLiteral("%1/repos/%2/%3/releases/%4/attach_files")
                 .arg(m_baseUrl).arg(owner, repo).arg(releaseId));
    auto *multi = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    QHttpPart filePart;
    // 实测字段名为 file（单数），files 会被拒"file is missing"
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QVariant(QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(name)));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader,
                       QVariant(QStringLiteral("application/octet-stream")));
    auto *payload = new QFile(filePath);
    payload->open(QIODevice::ReadOnly);
    filePart.setBodyDevice(payload);
    payload->setParent(multi);   // 随 multiPart 一起释放
    multi->append(filePart);
    RestService::postMultipart(url, multi, cb);
}
