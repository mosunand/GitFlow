#include "gitservice.h"
#include "proxy.h"
#include "i18n.h"
#include <QProcess>
#include <QElapsedTimer>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimer>
#include <QMutex>
#include <algorithm>
#include <stdexcept>

namespace {
QStringList proxyEnvList() {
    const QString proxy = proxy::detectSystemProxy();
    if (proxy.isEmpty()) return {};
    return { QStringLiteral("HTTPS_PROXY=%1").arg(proxy),
             QStringLiteral("HTTP_PROXY=%1").arg(proxy) };
}

// 所有 git 子进程的统一环境：禁交互提示 + 继承代理
QProcessEnvironment gitEnv() {
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert("GIT_TERMINAL_PROMPT", "0");
    pe.insert("GCM_INTERACTIVE", "Never");
    for (const QString &kv : proxyEnvList()) {
        const int eq = kv.indexOf('=');
        pe.insert(kv.left(eq), kv.mid(eq + 1));
    }
    return pe;
}

QString unquotePath(const QString &p) {
    if (p.startsWith('"') && p.endsWith('"') && p.size() >= 2) {
        QString b = p.mid(1, p.size() - 2);
        QRegularExpression re("\\\\([0-7]{3})");
        QString out; qsizetype last = 0;
        auto it = re.globalMatch(b);
        while (it.hasNext()) {
            auto m = it.next();
            out += b.mid(last, m.capturedStart() - last);
            out += QChar(char(m.captured(1).toInt(nullptr, 8)));
            last = m.capturedEnd();
        }
        out += b.mid(last);
        return out;
    }
    return p;
}

void appendProxyEnv(QProcessEnvironment &pe) {
    for (const QString &kv : proxyEnvList()) {
        const int eq = kv.indexOf('=');
        pe.insert(kv.left(eq), kv.mid(eq + 1));
    }
}

// 写 askpass 批处理并返回路径（Windows）。
// Token 走 GF_TOKEN 环境变量（只对本 git 子进程可见），不写进脚本文件明文；
// 用户名/Token 含 & | ^ < > 等 cmd 特殊字符时也不会破坏批处理语法。
// 注意不能带 QIODevice::Text：Windows 下 Qt 会把 \n 再翻成 \r\n，
// 而这里已经显式写了 \r\n，结果就是每行都变成 \r\r\n
QString writeAskpass(const QString &token, const QString &user) {
    const QString dir = QDir::tempPath() + "/gitflow_auth";
    QDir().mkpath(dir);
    const QString batPath = dir + "/askpass.cmd";
    QFile f(batPath);
    if (f.open(QIODevice::WriteOnly)) {
        QTextStream ts(&f);
        ts << "@echo off\r\n";
        ts << "echo %~1 | findstr /I \"Username\" >nul\r\n";
        ts << "if not errorlevel 1 (echo %GF_USER%) else (echo %GF_TOKEN%)\r\n";
    }
    return batPath;
}

// 带一次性 Token 环境的 git 启动封装：所有远程认证统一走这里
QProcessEnvironment buildAskpassEnv(const QString &token, const QString &user) {
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert("GIT_TERMINAL_PROMPT", "0");
    pe.insert("GCM_INTERACTIVE", "Never");
    pe.insert("GCM_GUI_PROMPT", "Never");
    pe.insert("GF_TOKEN", token);
    pe.insert("GF_USER", user.isEmpty() ? QStringLiteral("oauth2") : user);
    pe.insert("GIT_ASKPASS", writeAskpass(token, user));
    appendProxyEnv(pe);
    return pe;
}
} // namespace

QProcessEnvironment GitService::askpassEnv(const QString &token, const QString &user) {
    return buildAskpassEnv(token, user);
}

GitService::GitService(QObject *parent) : QObject(parent) {
    m_gitPath = QStringLiteral("git");
}

QString GitService::run(const QStringList &args, const QString &cwd, bool check, int timeoutMs) {
    QProcess proc;
    proc.setProcessEnvironment(gitEnv());
    if (!cwd.isEmpty()) proc.setWorkingDirectory(cwd);
    proc.start(m_gitPath, args);
    if (!proc.waitForStarted(5000))
        throw std::runtime_error("Cannot start git process");
    // 超时兜底：git 挂死时不能让 UI 的加载遮罩永远转下去
    if (!proc.waitForFinished(timeoutMs)) {
        proc.kill();
        throw std::runtime_error("Git operation timed out");
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    const QString err = QString::fromUtf8(proc.readAllStandardError());
    if (check && (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)) {
        const QString msg = err.isEmpty() ? out : err;
        throw std::runtime_error(QStringLiteral("Git: %1").arg(msg).toStdString());
    }
    return out.trimmed();
}

bool GitService::isRepository(const QString &path) const {
    return QFileInfo::exists(path + "/.git");
}

GitRepoStatus GitService::status(const QString &repo) {
    GitRepoStatus st;
    // 一次调用取全：分支 + 变更 + 未跟踪（-unormal 按目录显示，
    // 避免大目录（几千个文件的依赖目录）逐文件枚举拖慢 status）
    const QString raw = run({ "-c", "core.quotepath=false", "status", "--porcelain=v1",
                              "-b", "--untracked-files=normal" }, repo, false);
    const QStringList lines = raw.split('\n');
    if (!lines.isEmpty() && lines[0].startsWith("## ")) {
        const QString head = lines[0].mid(3);
        st.branch = head.section("...", 0, 0).section(" [", 0, 0).trimmed();
        if (st.branch == QLatin1String("HEAD (no branch)"))
            st.branch = QStringLiteral("(detached)");
        // 全新仓库（尚无任何提交）输出 "## No commits yet on master"：
        // 直接显示会把整句话当成分支名，填进分支下拉框和状态栏
        const QString unborn = QStringLiteral("No commits yet on ");
        if (st.branch.startsWith(unborn))
            st.branch = st.branch.mid(unborn.size());
    }
    if (!lines.isEmpty() && lines[0].contains('[')) {
        const QString track = lines[0].section('[', 1).section(']', 0, 0);
        for (const QString &part : track.split(',')) {
            const QString p = part.trimmed();
            if (p.startsWith("ahead")) st.ahead = p.section(' ', 1).toInt();
            else if (p.startsWith("behind")) st.behind = p.section(' ', 1).toInt();
        }
    }
    for (int i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        if (line.size() < 3) continue;
        const QChar idx = line[0], wt = line[1];
        QString path = unquotePath(line.mid(3));
        const int arrow = path.indexOf(" -> ");
        if (arrow >= 0) path = path.mid(arrow + 4);
        const QChar code = wt != ' ' ? wt : idx;
        GitFileItem item;
        item.path = path;
        if (code == '?') {
            // 未跟踪：-unormal 只给到目录粒度（大目录不会逐文件枚举），
            // 但绝不能丢——此前直接 continue，导致界面上"未追踪文件"分组永远是空的
            item.status = GitFileStatus::Untracked;
            item.staged = false;
            st.files.append(item);
            continue;
        }
        item.staged = idx != ' ' && idx != '?';
        switch (code.unicode()) {
        case 'M': item.status = GitFileStatus::Modified; break;
        case 'A': item.status = GitFileStatus::Added; break;
        case 'D': item.status = GitFileStatus::Deleted; break;
        case 'R': item.status = GitFileStatus::Renamed; break;
        case 'U': item.status = GitFileStatus::Conflict; break;
        default:  item.status = GitFileStatus::Modified; break;
        }
        st.files.append(item);
    }
    return st;
}

QStringList GitService::lsFiles(const QString &repo) {
    const QString raw = run({ "-c", "core.quotepath=false", "ls-files" }, repo, false);
    QStringList out;
    for (const QString &p : raw.split('\n', Qt::SkipEmptyParts))
        out.append(unquotePath(p.trimmed()));
    return out;
}

QList<OversizedFile> GitService::findOversizedFiles(const QString &repo, qint64 limitBytes) {
    QList<OversizedFile> out;
    QSet<QString> seen;
    const QDir rd(repo);
    for (const QString &rel : lsFiles(repo)) {
        const QFileInfo fi(rd.filePath(rel));
        if (fi.exists() && fi.isFile() && fi.size() > limitBytes) {
            // 去重键必须带来源前缀：同一个文件"工作区里是大文件"且"已经提交进历史"时，
            // 两个循环算出的键完全一样（路径|大小），历史条目会被这里吞掉，
            // 于是只提示"① 工作区现存 → 删除文件并提交即可"——而这条建议对已入库的
            // blob 根本无效（删文件再提交，旧提交里的大 blob 依然会被推送），
            // 用户照做后再次推送仍被拒。分开记录才能同时给出真正有效的选项
            const QString key = QStringLiteral("c|") + rel + '|' + QString::number(fi.size());
            if (!seen.contains(key)) {
                seen.insert(key);
                out.append({ rel, QStringLiteral("current"), fi.size(),
                             fi.size() / 1024.0 / 1024.0 });
            }
        }
    }
    // 一次 rev-list 拿"本次推送真正会传输的对象"（id + 路径），再交给单进程
    // cat-file --batch-check 批量取类型/大小（每个对象起一个 git 进程的方式会卡死大仓库）。
    // 范围限定 HEAD --not --remotes：推送只上传这部分对象，已在远程的对象不会重传、
    // 平台也不会再检查其大小。用 --all 会把其他分支/旧标签里的大文件也算进来——
    // 那些既不阻塞本次推送，又会把用户引向"重写全部历史"这类破坏性操作
    const QString raw = run({ "rev-list", "--objects", "HEAD", "--not", "--remotes" }, repo, false);
    // 只保留"带路径的对象"：rev-list 对 commit 对象不输出路径，而 commit 也不可能是
    // blob。省掉一份与 objPath 完全重复的 id 列表，大仓库上少几十 MB 的临时内存
    QHash<QString, QString> objPath;
    objPath.reserve(4096);
    for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
        const int sp = line.indexOf(' ');
        if (sp > 0) objPath.insert(line.left(sp), line.mid(sp + 1));
    }
    if (!objPath.isEmpty()) {
        QProcess proc;
        proc.setProcessEnvironment(gitEnv());
        proc.setWorkingDirectory(repo);
        proc.start(m_gitPath, { "cat-file", "--batch-check" });
        if (proc.waitForStarted(5000)) {
            proc.write(objPath.keys().join('\n').toUtf8());
            proc.write("\n");
            proc.closeWriteChannel();
            // 边跑边读，避免输出超过管道缓冲时死锁
            QByteArray batch;
            QElapsedTimer batchTimer;
            batchTimer.start();
            while (proc.state() == QProcess::Running || proc.bytesAvailable() > 0) {
                if (proc.waitForReadyRead(300))
                    batch += proc.readAll();
                if (batchTimer.elapsed() > 120000) {   // 兜底：cat-file 卡住不能拖住整个推送
                    proc.kill();
                    break;
                }
            }
            batch += proc.readAll();
            for (const QString &line : QString::fromUtf8(batch).split('\n', Qt::SkipEmptyParts)) {
                const QStringList parts = line.split(' ');
                if (parts.size() < 3 || parts.at(1) != QLatin1String("blob")) continue;
                bool ok = false;
                const qint64 size = parts.at(2).toLongLong(&ok);
                if (!ok || size <= limitBytes) continue;
                const QString id = parts.at(0);
                const QString path = objPath.value(id, id.left(12));
                const QString key = QStringLiteral("h|") + path + '|' + QString::number(size);
                if (!seen.contains(key)) {
                    seen.insert(key);
                    OversizedFile f;
                    f.path = path;
                    f.source = QStringLiteral("history");
                    f.size = size;
                    f.sizeMb = size / 1024.0 / 1024.0;
                    f.blobId = id;
                    out.append(f);
                }
            }
        }
    }
    // 历史大文件：定位首次引入的提交（软回退方案的锚点）。
    // 只查当前分支 HEAD（不带 --all）：锚点必须在 HEAD 祖先链上才能安全 reset，
    // 否则可能拿到其他分支的提交、软回退会切到错误的历史
    for (auto &f : out) {
        if (f.source != QLatin1String("history")) continue;
        const QString first = run({ "-c", "core.quotepath=false", "log", "HEAD",
                                    "--reverse", "--format=%H", "--diff-filter=A", "--", f.path },
                                  repo, false);
        f.firstCommit = first.section('\n', 0, 0).trimmed();
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.size > b.size; });
    return out;
}

// 提交前拦截：算出"这次提交会新写入仓库的内容"里有没有超限文件。
// 候选 = 工作区已改 ∪ 已暂存 ∪ 未跟踪的新文件（未跟踪目录展开到文件，
// 否则拿不到目录里文件的真实大小）—— 这些 git status 一次就能给全，
// 原来拆成 diff / diff --cached / ls-files 三次调用，每次提交都要多起两三个进程。
// 刻意不含"已入库且未改动"的文件：它们不是"这次要进入提交的内容"，
// 若一并拦下，仓库里存在历史大文件时用户连一个 .gitignore 都提交不了
QList<OversizedFile> GitService::findOversizedInCommit(const QString &repo, qint64 limitBytes) {
    const QString raw = run({ "-c", "core.quotepath=false", "status", "--porcelain=v1",
                              "--untracked-files=all" }, repo, false);
    QSet<QString> cand;
    for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
        if (line.size() < 3) continue;
        if (line.at(0) == QLatin1Char('!')) continue;   // 被忽略项（未请求 --ignored，防御）
        QString path = unquotePath(line.mid(3));
        const int arrow = path.indexOf(" -> ");         // 重命名取新名（即将入库的那个）
        if (arrow >= 0) path = path.mid(arrow + 4);
        cand.insert(path);
    }

    QList<OversizedFile> found;
    for (const QString &rel : cand) {
        const QFileInfo fi(QDir(repo).filePath(rel));
        // 已删除的文件没有内容要写入，跳过（它不入库也就不会触发平台限制）；
        // 这一步同时顺手过滤掉了 status 里"只有 D"的条目
        if (!fi.exists() || !fi.isFile() || fi.size() <= limitBytes) continue;
        OversizedFile f;
        f.path = rel;
        f.source = QStringLiteral("pending");
        f.size = fi.size();
        f.sizeMb = fi.size() / 1024.0 / 1024.0;
        found.append(f);
    }
    std::sort(found.begin(), found.end(),
              [](const OversizedFile &a, const OversizedFile &b) { return a.size > b.size; });
    return found;
}

// rev-list --objects 对每个对象只给"一个"路径。文件改过名、或同一份内容存在于
// 多个路径时，只按这一个路径去删会漏（--ignore-unmatch 静默跳过），结果是
// "提示清理成功、大 blob 仍在历史里、推送照样被拒"。这里按 blob 把历史路径收齐
QStringList GitService::pathsOfOversized(const QString &repo, const QList<OversizedFile> &files) {
    QStringList paths;
    for (const auto &f : files) {
        if (!f.path.isEmpty() && !paths.contains(f.path)) paths << f.path;
        if (f.blobId.isEmpty()) continue;
        const QString raw = run({ "-c", "core.quotepath=false", "log", "--all", "--name-only",
                                  "--format=", QStringLiteral("--find-object=%1").arg(f.blobId) },
                                repo, false);
        for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
            const QString p = unquotePath(line.trimmed());
            if (!p.isEmpty() && !paths.contains(p)) paths << p;
        }
    }
    return paths;
}

void GitService::enableStatusCache(const QString &repo) {
    // 每仓库一次性启用 git 自带状态缓存；只在未配置时才写，避免反复覆盖用户配置
    for (const QString &key : { QStringLiteral("core.untrackedCache"),
                                QStringLiteral("core.fsmonitor") }) {
        if (run({ "config", "--get", key }, repo, false).isEmpty())
            run({ "config", key, "true" }, repo, false);
    }
    if (!run({ "fsmonitor--daemon", "status" }, repo, false)
             .contains(QLatin1String("is running"), Qt::CaseInsensitive))
        run({ "fsmonitor--daemon", "start" }, repo, false);
}

// 历史大文件清理：用 filter-branch 重写提交把指定路径从全部历史剥除。
// 不可逆（提交哈希全变），调用方必须先向用户强确认。返回错误信息，空=成功。
QString GitService::purgeFilesFromHistory(const QString &repo, const QList<OversizedFile> &files) {
    if (files.isEmpty()) return {};
    // 按 blob 收齐历史路径：只看 rev-list 给的单个路径，改名/多路径的副本会漏删，
    // 于是"提示清理完成"但大 blob 仍在历史里、推送依旧被拒
    const QStringList paths = pathsOfOversized(repo, files);
    if (paths.isEmpty()) return {};
    // --index-filter 对每个提交从索引中删除指定路径（绕过检出，速度可接受）。
    // 命令经 shell 解释，路径必须逐个加引号（含空格/中文的路径才不会被拆碎）
    QStringList quoted;
    for (const QString &p : paths) quoted << QStringLiteral("'%1'").arg(p);
    const QString rm = QStringLiteral("git rm -r --cached --ignore-unmatch %1")
                           .arg(quoted.join(' '));
    // 只重写当前分支：扫描范围已是 HEAD --not --remotes，别分支/旧标签里的大文件
    // 既不阻塞本次推送，也不该被顺手改写掉（那会让别人的副本分叉）
    QString branch = run({ "rev-parse", "--abbrev-ref", "HEAD" }, repo, false);
    if (branch.isEmpty() || branch == QLatin1String("HEAD")) branch = QStringLiteral("HEAD");
    const QStringList args { "filter-branch", "-f", "--index-filter", rm,
                             "--prune-empty", "--", branch };
    QProcess proc;
    proc.setProcessEnvironment(gitEnv());
    proc.setWorkingDirectory(repo);
    proc.start(m_gitPath, args);
    if (!proc.waitForStarted(5000)) return QStringLiteral("cannot start git");
    if (!proc.waitForFinished(300000)) {   // 5 分钟上限
        proc.kill();
        return QStringLiteral("filter-branch timeout");
    }
    if (proc.exitCode() != 0)
        return QString::fromUtf8(proc.readAllStandardError()).left(2000);
    // 剥除后回收悬空对象；顺序关键（实测验证）：先删 filter-branch 的原始引用备份，
    // 否则 refs/original/ 仍引用旧历史，gc --prune 不会回收大 blob
    const QString origRefs = run({ "for-each-ref", "--format=%(refname)", "refs/original/" }, repo, false);
    for (const QString &ref : origRefs.split('\n', Qt::SkipEmptyParts))
        run({ "update-ref", "-d", ref }, repo, false);
    run({ "reflog", "expire", "--expire=now", "--all" }, repo, false);
    run({ "gc", "--prune=now", "--aggressive" }, repo, false);
    // 复核：filter-branch 的退出码只说明命令跑完了，路径没匹配上/漏删都照样返回 0。
    // 不复核就报成功，用户会在"清理成功→推送仍被拒"之间来回循环
    QSet<QString> want;
    for (const auto &f : files)
        if (!f.blobId.isEmpty()) want.insert(f.blobId);
    if (!want.isEmpty()) {
        const QString remain = run({ "rev-list", "--objects", "HEAD", "--not", "--remotes" }, repo, false);
        for (const QString &line : remain.split('\n', Qt::SkipEmptyParts)) {
            const int sp = line.indexOf(' ');
            const QString id = sp < 0 ? line : line.left(sp);
            if (want.contains(id))
                return i18n::t("oversized_purge_incomplete");
        }
    }
    return {};
}

// 软回退清理：回退到"大文件进入前"的提交，之后的全部改动重新提交为一个
// （大文件路径被排除在外）。适合大文件只存在于最近未推送提交的场景：
// 秒级完成、不改更早历史、不产生 force push。返回错误信息，空=成功。
QString GitService::softResetPurge(const QString &repo, const QList<OversizedFile> &files,
                                   const QString &anchorCommit) {
    if (anchorCommit.isEmpty()) return QStringLiteral("no anchor commit");
    try {
        // 锚点必须是当前分支的历史提交（防止别分支的提交被当锚点，
        // reset 过去等于切换历史，属于数据丢失级错误）
        const QString head = run({ "rev-parse", "HEAD" }, repo, false);
        if (!isAncestorOrEqual(repo, anchorCommit, head))
            return QStringLiteral("anchor commit is not on the current branch");
        // 锚点为根提交时无法软回退到"它之前"（没有父提交），
        // 唯一安全出路是 filter-branch，这里明确拒绝
        const QString parent = run({ "rev-parse", "--verify", "--quiet",
                                     anchorCommit + "^" }, repo, false);
        if (parent.isEmpty())
            return QStringLiteral("big file is in the root commit; use 'Rewrite history' instead");
        // 1) 软回退到锚点的父提交：工作区与暂存区保持不变，仅移动分支指针
        run({ "reset", "--soft", parent }, repo);
        // 2) 从暂存区剔除大文件（工作区文件不动，用户可自行删除）；路径逐个传参，天然支持空格。
        //    用收齐后的历史路径：同一 blob 若在 HEAD 上还有别的名字，只删一个仍会留在新提交里
        const QStringList paths = pathsOfOversized(repo, files);
        for (const QString &p : paths)
            run({ "rm", "-r", "--cached", "--ignore-unmatch", "--", p }, repo, false);
        // 3) 之后的改动合并为一个新提交；若暂存区为空（改动只有大文件）则跳过提交。
        // 必须排除未跟踪文件：工作区里那个大文件此刻正是未跟踪状态，
        // 用默认 status 判断会"看起来有改动"→ commit 报 nothing to commit，
        // 而 reset 已经执行，用户只会看到一句莫名其妙的失败
        const QString pending = run({ "status", "--porcelain", "--untracked-files=no" }, repo, false);
        if (!pending.isEmpty())
            run({ "commit", "-m", "cleanup: remove oversized files from recent commits" }, repo);
        return {};
    } catch (const std::exception &e) {
        return QString::fromUtf8(e.what());
    }
}

void GitService::init(const QString &path) { run({ "init" }, path); }

QString GitService::currentBranch(const QString &repo) {
    const QString b = run({ "branch", "--show-current" }, repo, false);
    return b.isEmpty() ? QStringLiteral("HEAD") : b;
}

void GitService::setGitPath(const QString &p) {
    m_gitPath = p.isEmpty() ? QStringLiteral("git") : p;
    // 相对名（如 "git"）解析为绝对路径，QProcess 启动时不再做 PATH 搜索。
    // 解析不到时退回 "git" 而不是留空串：空串会让每次调用都报
    // "Cannot start git process"，连"PATH 里其实有 git"这种正常情况也一起废掉
    if (!QFileInfo::exists(m_gitPath) && !m_gitPath.contains('/') && !m_gitPath.contains('\\')) {
        const QString found = QStandardPaths::findExecutable(m_gitPath);
        if (!found.isEmpty()) m_gitPath = found;
    }
}

QString GitService::blame(const QString &repo, const QString &path) {
    return run({ "blame", "--", path }, repo, false);
}

QStringList GitService::branches(const QString &repo) {
    return run({ "for-each-ref", "--format=%(refname:short)", "refs/heads" }, repo)
        .split('\n', Qt::SkipEmptyParts);
}

QString GitService::diff(const QString &repo, const QString &path, bool staged) {
    QStringList args { "diff" };
    if (staged) args << "--cached";
    if (!path.isEmpty()) args << "--" << path;
    return run(args, repo, false);
}

void GitService::add(const QString &repo, const QStringList &paths) {
    if (paths.isEmpty()) return;
    // 分批暂存：所有路径塞进一条命令行会撞上 Windows 约 32KB 的命令行上限，
    // QProcess 直接起不来（大批量拖入文件夹时就是这种"像崩了一样"的失败）。
    // 预算给到 24KB / 512 条，既留足 exe 路径与参数的余量，又尽量少起进程
    QStringList batch;
    int batchChars = 0;
    auto flush = [&] {
        if (batch.isEmpty()) return;
        // 大批量入库时 git 要逐个哈希，120s 不一定够，放宽到 10 分钟
        run(QStringList{ "add", "--" } + batch, repo, true, 600000);
        batch.clear();
        batchChars = 0;
    };
    for (const QString &p : paths) {
        if (!batch.isEmpty() && (batch.size() >= 512 || batchChars + p.size() > 24000)) flush();
        batch << p;
        batchChars += p.size() + 1;
    }
    flush();
}

QString GitService::commit(const QString &repo, const QString &msg, const QStringList &paths) {
    if (paths.isEmpty())
        run({ "add", "-A" }, repo);          // 未指定文件时全量暂存（含未跟踪文件）
    else
        add(repo, paths);
    return run({ "commit", "-m", msg }, repo);
}

void GitService::restore(const QString &repo, const QString &path) {
    run({ "restore", "--", path }, repo);
}

void GitService::deleteFile(const QString &repo, const QString &path) {
    // 变更列表里的未跟踪项可能是目录（-unormal 只给到目录粒度），
    // 只按文件删会静默失败；同时拦住 "." 之类的路径，避免误删整个仓库
    if (path.isEmpty() || path == QLatin1String(".") || path == QLatin1String("..")) return;
    const QString abs = QDir(repo).filePath(path);
    const QFileInfo fi(abs);
    if (fi.isDir()) {
        QDir(abs).removeRecursively();
        return;
    }
    if (fi.exists()) QFile::remove(abs);
}

QString GitService::revert(const QString &repo, const QString &hash) {
    return run({ "revert", hash, "--no-edit" }, repo);
}

QString GitService::resetTo(const QString &repo, const QString &hash, bool hard) {
    return run({ "reset", hard ? "--hard" : "--mixed", hash }, repo);
}

void GitService::switchBranch(const QString &repo, const QString &name) {
    run({ "switch", name }, repo);
}

void GitService::createBranch(const QString &repo, const QString &name, const QString &start) {
    QStringList args { "branch", name };
    if (!start.isEmpty()) args << start;
    run(args, repo);
}

void GitService::deleteBranch(const QString &repo, const QString &name, bool force) {
    run({ "branch", force ? "-D" : "-d", name }, repo);
}

QList<CommitInfo> GitService::history(const QString &repo, int limit) {
    QList<CommitInfo> out;
    const QString raw = run({ "log", QString("-%1").arg(limit),
                              "--format=%H%x1f%h%x1f%s%x1f%an%x1f%ad", "--date=short" }, repo, false);
    for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
        const QStringList parts = line.split(QChar(0x1f));
        if (parts.size() == 5)
            out.append({ parts[0], parts[1], parts[2], parts[3], parts[4] });
    }
    return out;
}

// ---- 远程操作（token askpass + 代理 + 逐行进度）----
void GitService::push(const QString &repo, const QString &token, const QString &user,
                      const LineFn &onLine, const LineFn &onDone, bool *okOut, QString *errOut) {
    QProcess proc;
    proc.setProcessEnvironment(askpassEnv(token, user));
    proc.setWorkingDirectory(repo);
    // credential.helper 置空：强制走 askpass（Token），不让 GCM 弹登录窗
    proc.start(m_gitPath, { "-c", "http.version=HTTP/1.1", "-c", "credential.helper=", "push", "--progress" });
    if (!proc.waitForStarted(5000)) {
        if (okOut) *okOut = false;
        if (errOut) *errOut = QStringLiteral("cannot start git");
        return;
    }
    QString all;
    // 有总超时：git 挂死（如网络黑洞）不能永远占用进度弹窗
    QElapsedTimer elapsed;
    elapsed.start();
    while (proc.state() != QProcess::NotRunning) {
        if (proc.waitForReadyRead(300)) {
            const QByteArray chunk = proc.readAll();
            all += QString::fromUtf8(chunk);
            if (onLine)
                for (const QString &line : QString::fromUtf8(chunk).split('\n', Qt::SkipEmptyParts))
                    onLine(line.trimmed());
        }
        if (elapsed.elapsed() > 1800000) {   // 30 分钟总闸
            proc.kill();
            if (errOut) *errOut = all + "\n[timeout: push aborted after 30min]";
            if (okOut) *okOut = false;
            return;
        }
    }
    // 进程退出后缓冲区里通常还压着最后几行——推送失败的真正原因基本都在尾部，
    // 不读干净就会出现"失败但错误信息是空的"
    const QByteArray tailOut = proc.readAllStandardOutput();
    const QByteArray tailErr = proc.readAllStandardError();
    if (onLine) {
        for (const QByteArray &chunk : { tailOut, tailErr })
            for (const QString &line : QString::fromUtf8(chunk).split('\n', Qt::SkipEmptyParts))
                onLine(line.trimmed());
    }
    all += QString::fromUtf8(tailOut) + QString::fromUtf8(tailErr);
    const bool success = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (okOut) *okOut = success;
    if (!success && errOut) *errOut = all;
    if (success && onDone) onDone(all);
}

void GitService::pull(const QString &repo, const QString &token, const QString &user) {
    if (!token.isEmpty()) {
        QProcess proc;
        proc.setProcessEnvironment(askpassEnv(token, user));
        proc.setWorkingDirectory(repo);
        proc.start(m_gitPath, { "-c", "credential.helper=", "pull", "--ff-only" });
        if (!proc.waitForStarted(5000))
            throw std::runtime_error("Cannot start git process");
        // 不能无限等：网络黑洞时工作线程会永远挂在"拉取中..."，且没有任何取消手段。
        // 给 15 分钟上限（大仓库慢网络也够），超时杀掉并报错
        if (!proc.waitForFinished(900000)) {
            proc.kill();
            throw std::runtime_error("Git pull timed out (15min)");
        }
        // 必须同时看 exitStatus：进程根本没起来时 exitCode() 也是 0，
        // 只判 exitCode 会把"启动失败"当成"拉取成功"
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
            throw std::runtime_error(QString::fromUtf8(proc.readAllStandardError()).toStdString());
        return;
    }
    run({ "pull", "--ff-only" }, repo, true, 900000);
}

QString GitService::clone(const QString &url, const QString &targetDir,
                          const QString &token, const QString &user, const QString &into) {
    QDir().mkpath(targetDir);
    if (!token.isEmpty()) {
        // credential.helper 置空：强制走 askpass（Token），不让 GCM 弹登录窗
        QStringList args { "-c", "credential.helper=", "clone", url };
        if (!into.isEmpty()) args << into;
        QProcess proc;
        proc.setProcessEnvironment(askpassEnv(token, user));
        proc.setWorkingDirectory(targetDir);
        proc.start(m_gitPath, args);
        if (!proc.waitForStarted(5000))
            throw std::runtime_error("Cannot start git process");
        proc.waitForFinished(-1);
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
            throw std::runtime_error(QString::fromUtf8(proc.readAllStandardError()).toStdString());
    } else {
        // 无 Token 的公开仓库克隆：大仓库耗时长，走无超时路径（由 UI 的 WaitCursor 兜底）
        QProcess proc;
        proc.setProcessEnvironment(gitEnv());
        proc.setWorkingDirectory(targetDir);
        proc.start(m_gitPath, { "clone", url });
        if (!proc.waitForStarted(5000))
            throw std::runtime_error("Cannot start git process");
        proc.waitForFinished(-1);
        if (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)
            throw std::runtime_error(QString::fromUtf8(proc.readAllStandardError()).toStdString());
    }
    if (!into.isEmpty()) return into;
    // 由 URL 推目录名：先去掉末尾斜杠，再取最后一段并剥掉 .git。
    // 原实现无条件 chopped(1) 先砍掉一个字符，".git" 判断永远不成立，
    // 得到的是 "repo.gi" 这种残名
    QString name = url.trimmed();
    while (name.endsWith('/')) name.chop(1);
    name = name.section('/', -1);
    if (name.endsWith(".git")) name.chop(4);
    return QDir(targetDir).filePath(name);
}

namespace {
// stash 的三种结局都要能区分：成功 / 遇到冲突（退出码非 0，改动带标记进工作区，
// stash 不会被删）/ 没有可暂存的改动（退出码 0 但什么都没做）。
// 只看退出码或只看 check=false 的 run() 会把后两种全当成成功
GitService::StashResult runStash(const QString &gitPath, const QString &repo, const QStringList &args) {
    QProcess proc;
    proc.setProcessEnvironment(gitEnv());
    proc.setWorkingDirectory(repo);
    proc.start(gitPath, args);
    GitService::StashResult r;
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(120000)) {
        if (proc.state() == QProcess::Running) proc.kill();
        r.message = QStringLiteral("git stash failed");
        return r;
    }
    const QString out = QString::fromUtf8(proc.readAllStandardOutput());
    const QString err = QString::fromUtf8(proc.readAllStandardError());
    r.message = (out + "\n" + err).trimmed();
    if (proc.exitCode() == 0) {
        r.ok = true;
        r.nothing = out.contains(QLatin1String("No local changes to save"));
        return r;
    }
    r.conflict = out.contains(QLatin1String("conflict"), Qt::CaseInsensitive)
                  || err.contains(QLatin1String("conflict"), Qt::CaseInsensitive);
    return r;
}
} // namespace

GitService::StashResult GitService::stashSave(const QString &repo, const QString &msg) {
    QStringList args { "stash", "push" };
    if (!msg.isEmpty()) args << "-m" << msg;
    return runStash(m_gitPath, repo, args);
}

GitService::StashResult GitService::stashPop(const QString &repo) {
    return runStash(m_gitPath, repo, { "stash", "pop" });
}

QList<GitService::StashEntry> GitService::stashList(const QString &repo) {
    QList<StashEntry> out;
    const QString raw = run({ "stash", "list", "--format=%gd %s" }, repo, false);
    for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
        const int sp = line.indexOf(' ');
        if (sp > 0) out.append({ line.left(sp), line.mid(sp + 1) });
    }
    return out;
}

void GitService::stashDrop(const QString &repo, const QString &ref) {
    run({ "stash", "drop", ref }, repo);
}

QStringList GitService::tags(const QString &repo) {
    return run({ "tag" }, repo, false).split('\n', Qt::SkipEmptyParts);
}

void GitService::createTag(const QString &repo, const QString &name, const QString &msg) {
    if (msg.isEmpty()) run({ "tag", name }, repo);
    else run({ "tag", "-a", name, "-m", msg }, repo);
}

void GitService::deleteTag(const QString &repo, const QString &name) {
    run({ "tag", "-d", name }, repo);
}

namespace {
QPair<QString, QString> g_identity;
bool g_identityValid = false;
// 启动预热与设置页可能在不同线程同时调用（Qt 容器非线程安全，并发读写会踩坏引用计数）
QMutex g_identityMutex;
} // namespace

QPair<QString, QString> GitService::identity(const QString &repo) {
    // 全局作者信息缓存：设置页与启动预热共用，避免重复 spawn git
    if (repo.isEmpty()) {
        QMutexLocker lock(&g_identityMutex);
        if (g_identityValid) return g_identity;
    }
    // 一条命令同时取回生效的 user.name / user.email（本地优先，自动回退全局）
    // repo 为空时是"查本机全局身份"：必须显式 --global，否则进程当前目录若恰好
    // 在某个仓库里，会把那个仓库的局部配置当成全局身份显示出来
    QStringList args { "config" };
    if (repo.isEmpty()) args << "--global";
    args << "--get-regexp" << "^user\\.(name|email)$";
    const QString raw = run(args, repo, false);
    QString name, email;
    for (const QString &l : raw.split('\n', Qt::SkipEmptyParts)) {
        const int sp = l.indexOf(' ');
        if (sp <= 0) continue;
        const QString k = l.left(sp), v = l.mid(sp + 1).trimmed();
        if (k == QLatin1String("user.name")) name = v;
        else if (k == QLatin1String("user.email")) email = v;
    }
    if (repo.isEmpty()) {
        QMutexLocker lock(&g_identityMutex);
        g_identity = { name, email };
        g_identityValid = true;
    }
    return { name, email };
}

bool GitService::isAncestorOrEqual(const QString &repo, const QString &a, const QString &b) {
    if (a == b) return true;
    // merge-base --is-ancestor 靠退出码表达结果（0=是祖先），需独立跑进程拿退出码
    QProcess proc;
    proc.setProcessEnvironment(gitEnv());
    proc.setWorkingDirectory(repo);
    proc.start(m_gitPath, { "merge-base", "--is-ancestor", a, b });
    if (!proc.waitForStarted(5000) || !proc.waitForFinished(10000)) return false;
    return proc.exitCode() == 0;
}

void GitService::setIdentity(const QString &name, const QString &email, bool globalScope) {
    {
        QMutexLocker lock(&g_identityMutex);
        g_identityValid = false;
    }
    const QStringList scope = globalScope ? QStringList{ "--global" } : QStringList{ "--local" };
    run(QStringList{ "config" } + scope + QStringList{ "user.name", name });
    run(QStringList{ "config" } + scope + QStringList{ "user.email", email });
}

QString GitService::diagnoseNetwork() {
    // 真实诊断（原来只有一行 "[Network diagnostics]" 占位文案，等于没诊断）：
    // 代理是这类远程失败最常见的原因，至少要把"走没走代理、走的是哪个"讲清楚
    const QString proxy = proxy::detectSystemProxy();
    QString out = i18n::t("net_diag_title") + "\n";
    out += proxy.isEmpty() ? i18n::t("net_diag_direct") + "\n"
                           : i18n::t("net_diag_proxy").arg(proxy) + "\n";
    out += i18n::t("net_diag_hint");
    return out;
}
