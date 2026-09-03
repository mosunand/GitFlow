#pragma once
#include "models.h"
#include <QObject>
#include <QStringList>
#include <functional>

// 统一封装 git CLI（QProcess），所有仓库操作走这里。
class GitService : public QObject {
    Q_OBJECT
public:
    explicit GitService(QObject *parent = nullptr);

    void setGitPath(const QString &p);
    QString gitPath() const { return m_gitPath; }

    // 同步执行（本地快速操作）
    QString run(const QStringList &args, const QString &cwd = {}, bool check = true);
    bool isRepository(const QString &path) const;

    // 状态
    GitRepoStatus status(const QString &repo);
    QStringList lsFiles(const QString &repo);
    QList<OversizedFile> findOversizedFiles(const QString &repo, qint64 limitBytes = 100LL * 1024 * 1024);
    void enableStatusCache(const QString &repo);   // 每仓库一次性启用 untrackedCache + fsmonitor

    // 基本操作
    void init(const QString &path);
    QString currentBranch(const QString &repo);
    QStringList branches(const QString &repo);
    QString diff(const QString &repo, const QString &path = {}, bool staged = false);
    QString blame(const QString &repo, const QString &path);
    void add(const QString &repo, const QStringList &paths);
    QString commit(const QString &repo, const QString &msg, const QStringList &paths);
    void restore(const QString &repo, const QString &path);
    void deleteFile(const QString &repo, const QString &path);
    QString revert(const QString &repo, const QString &hash);
    QString resetTo(const QString &repo, const QString &hash, bool hard);
    void switchBranch(const QString &repo, const QString &name);
    void createBranch(const QString &repo, const QString &name, const QString &start = {});
    void deleteBranch(const QString &repo, const QString &name, bool force = false);

    // 历史
    QList<CommitInfo> history(const QString &repo, int limit = 50);

    // 远程（自动继承系统代理 + token 认证）
    // onLine 回调在后台线程逐行收到 git 输出（进度用）
    using LineFn = std::function<void(const QString &)>;
    void push(const QString &repo, const QString &token, const QString &user,
              const LineFn &onLine = {}, const LineFn &onDone = {}, bool *ok = nullptr, QString *err = nullptr);
    void pull(const QString &repo, const QString &token, const QString &user);
    QString clone(const QString &url, const QString &targetDir,
                  const QString &token = {}, const QString &user = {},
                  const QString &into = {});

    // Stash
    QString stashSave(const QString &repo, const QString &msg);
    QString stashPop(const QString &repo);
    struct StashEntry { QString ref, subject; };
    QList<StashEntry> stashList(const QString &repo);
    void stashDrop(const QString &repo, const QString &ref);

    // Tag
    QStringList tags(const QString &repo);
    void createTag(const QString &repo, const QString &name, const QString &msg = {});
    void deleteTag(const QString &repo, const QString &name);

    // 作者身份
    QPair<QString, QString> identity(const QString &repo);
    void setIdentity(const QString &name, const QString &email, bool globalScope = true);

    // 网络诊断
    static QString diagnoseNetwork();

private:
    QString m_gitPath;
};
