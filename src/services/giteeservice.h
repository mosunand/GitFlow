#pragma once
#include "githubservice.h"

// Gitee API v5（结构同 GitHub，基址不同；附件端点为 /attachment）
class GiteeService : public RestService {
    Q_OBJECT
public:
    explicit GiteeService(const QString &token = {}, QObject *parent = nullptr);

    void verifyUser(const Callback &cb);
    void listMyRepos(const Callback &cb);
    void listUserRepos(const QString &username, const Callback &cb);
    void getRepo(const QString &owner, const QString &repo, const Callback &cb);
    void forkRepo(const QString &owner, const QString &repo, const Callback &cb);
    void createRepo(const QString &name, const QString &desc, bool privateRepo,
                    bool autoInit, const Callback &cb);
    void createRelease(const QString &owner, const QString &repo, const QString &tag,
                       const QString &name, const QString &body, bool prerelease,
                       const Callback &cb, const QString &targetCommitish = {});
    void uploadAsset(const QString &owner, const QString &repo, qint64 releaseId,
                     const QString &filePath, const Callback &cb);
};
