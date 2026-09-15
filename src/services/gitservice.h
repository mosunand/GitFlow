#pragma once
#include "models.h"
#include <QObject>
#include <QStringList>
#include <QProcessEnvironment>
#include <functional>

// 统一封装 git CLI（QProcess），所有仓库操作走这里。
class GitService : public QObject {
    Q_OBJECT
public:
    explicit GitService(QObject *parent = nullptr);

    void setGitPath(const QString &p);
    QString gitPath() const { return m_gitPath; }

    // 同步执行（本地快速操作）。timeoutMs 兜底 git 挂死（默认 120s，大批量 add 这类可放宽）
    QString run(const QStringList &args, const QString &cwd = {}, bool check = true,
                int timeoutMs = 120000);
    bool isRepository(const QString &path) const;

    // 状态
    GitRepoStatus status(const QString &repo);
    QStringList lsFiles(const QString &repo);
    // 推送前扫描：只扫"本次推送会传输的对象"（HEAD --not --remotes）里的超大 blob
    QList<OversizedFile> findOversizedFiles(const QString &repo, qint64 limitBytes = 100LL * 1024 * 1024);
    // 提交前扫描：只算"这次提交会新写入仓库的内容"（新增/已改/已暂存），
    // 已入库且未改动的历史大文件不算——否则连一个 .gitignore 都提交不了
    QList<OversizedFile> findOversizedInCommit(const QString &repo, qint64 limitBytes = 100LL * 1024 * 1024);
    void enableStatusCache(const QString &repo);   // 每仓库一次性启用 untrackedCache + fsmonitor
    // 重写历史移除指定文件（不可逆！提交哈希全变），用于清理历史中的超大文件。
    // 按 blob 定位（改名/同内容多路径也能剥净），只重写当前分支，完成后复核
    QString purgeFilesFromHistory(const QString &repo, const QList<OversizedFile> &files);
    // 软回退方案：回退到锚点提交的父提交，之后的改动重新打包提交（排除大文件）。
    // 仅适合大文件在最近未推送提交中的场景；秒级完成、不影响更早历史
    QString softResetPurge(const QString &repo, const QList<OversizedFile> &files, const QString &anchorCommit);

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
    // 结果里带冲突/无可暂存标记：git 对这两类情况不报错或只写提示，
    // 只看退出码会把"弹出遇到冲突"也当成成功
    struct StashResult { bool ok = false; bool conflict = false; bool nothing = false; QString message; };
    StashResult stashSave(const QString &repo, const QString &msg);
    StashResult stashPop(const QString &repo);
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
    // a 是否为 b 的祖先（或相等）。用于判断提交是否已被远程包含
    bool isAncestorOrEqual(const QString &repo, const QString &a, const QString &b);

    // 网络诊断
    static QString diagnoseNetwork();

    // 远程认证环境：一次性 askpass（Token 走环境变量）+ 禁交互 + 代理。
    // 需要直接起 git 进程的地方（如主窗口的推送三阶段）统一用它，避免各写一份
    static QProcessEnvironment askpassEnv(const QString &token, const QString &user);

private:
    QString m_gitPath;
    // 按 blob 哈希收齐这些大文件在历史里出现过的全部路径（改名 / 同内容多路径）
    QStringList pathsOfOversized(const QString &repo, const QList<OversizedFile> &files);
};
