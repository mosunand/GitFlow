#include "gitservice.h"
#include "proxy.h"
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QFileInfo>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QSet>
#include <QTimer>
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

// 写 askpass 批处理并返回路径（Windows）
QString writeAskpass(const QString &token, const QString &user) {
    const QString dir = QDir::tempPath() + "/gitflow_auth";
    QDir().mkpath(dir);
    const QString batPath = dir + "/askpass.cmd";
    QFile f(batPath);
    if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << "@echo off\r\n";
        ts << "echo %~1 | findstr /I \"Username\" >nul\r\n";
        ts << "if not errorlevel 1 (echo " << (user.isEmpty() ? "oauth2" : user) << ")"
           << " else (echo " << token << ")\r\n";
    }
    return batPath;
}
} // namespace

GitService::GitService(QObject *parent) : QObject(parent) {
    m_gitPath = QStringLiteral("git");
}

QString GitService::run(const QStringList &args, const QString &cwd, bool check) {
    QProcess proc;
    proc.setProcessEnvironment(gitEnv());
    if (!cwd.isEmpty()) proc.setWorkingDirectory(cwd);
    proc.start(m_gitPath, args);
    if (!proc.waitForStarted(5000))
        throw std::runtime_error("Cannot start git process");
    if (!proc.waitForFinished(-1))
        throw std::runtime_error("Git operation timed out");
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
        if (code == '?') continue;   // 未跟踪条目已由 -unormal 按目录给出
        GitFileItem item;
        item.path = path;
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
            const QString key = rel + '|' + QString::number(fi.size());
            if (!seen.contains(key)) {
                seen.insert(key);
                out.append({ rel, QStringLiteral("current"), fi.size(),
                             fi.size() / 1024.0 / 1024.0 });
            }
        }
    }
    // 一次 rev-list 拿全部历史对象（id + 路径），再交给单进程 cat-file --batch-check
    // 批量取类型/大小（每个对象起一个 git 进程的方式会卡死大仓库）
    const QString raw = run({ "rev-list", "--objects", "--all" }, repo, false);
    QHash<QString, QString> objPath;
    QStringList ids;
    ids.reserve(4096);
    for (const QString &line : raw.split('\n', Qt::SkipEmptyParts)) {
        const int sp = line.indexOf(' ');
        const QString id = sp < 0 ? line : line.left(sp);
        ids.append(id);
        if (sp > 0) objPath.insert(id, line.mid(sp + 1));
    }
    if (!ids.isEmpty()) {
        QProcess proc;
        proc.setProcessEnvironment(gitEnv());
        proc.setWorkingDirectory(repo);
        proc.start(m_gitPath, { "cat-file", "--batch-check" });
        if (proc.waitForStarted(5000)) {
            proc.write(ids.join('\n').toUtf8());
            proc.write("\n");
            proc.closeWriteChannel();
            // 边跑边读，避免输出超过管道缓冲时死锁
            QByteArray batch;
            while (proc.state() == QProcess::Running || proc.bytesAvailable() > 0) {
                if (proc.waitForReadyRead(300))
                    batch += proc.readAll();
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
                const QString key = path + '|' + QString::number(size);
                if (!seen.contains(key)) {
                    seen.insert(key);
                    out.append({ path, QStringLiteral("history"), size, size / 1024.0 / 1024.0 });
                }
            }
        }
    }
    std::sort(out.begin(), out.end(), [](auto &a, auto &b) { return a.size > b.size; });
    return out;
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

void GitService::init(const QString &path) { run({ "init" }, path); }

QString GitService::currentBranch(const QString &repo) {
    const QString b = run({ "branch", "--show-current" }, repo, false);
    return b.isEmpty() ? QStringLiteral("HEAD") : b;
}

void GitService::setGitPath(const QString &p) {
    m_gitPath = p.isEmpty() ? QStringLiteral("git") : p;
    // 相对名（如 "git"）解析为绝对路径，QProcess 启动时不再做 PATH 搜索
    if (!QFileInfo::exists(m_gitPath) && !m_gitPath.contains('/') && !m_gitPath.contains('\\'))
        m_gitPath = QStandardPaths::findExecutable(m_gitPath);
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
    if (!paths.isEmpty()) run(QStringList{ "add", "--" } + paths, repo);
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
    QFile f(QDir(repo).filePath(path));
    if (f.exists()) f.remove();
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
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert("GIT_TERMINAL_PROMPT", "0");
    pe.insert("GCM_INTERACTIVE", "Never");
    pe.insert("GIT_ASKPASS", writeAskpass(token, user));
    appendProxyEnv(pe);
    proc.setProcessEnvironment(pe);
    proc.setWorkingDirectory(repo);
    proc.start(m_gitPath, { "-c", "http.version=HTTP/1.1", "push", "--progress" });
    if (!proc.waitForStarted(5000)) {
        if (okOut) *okOut = false;
        if (errOut) *errOut = QStringLiteral("cannot start git");
        return;
    }
    QString all;
    while (proc.state() != QProcess::NotRunning) {
        if (proc.waitForReadyRead(300)) {
            const QByteArray chunk = proc.readAll();
            all += QString::fromUtf8(chunk);
            if (onLine)
                for (const QString &line : QString::fromUtf8(chunk).split('\n', Qt::SkipEmptyParts))
                    onLine(line.trimmed());
        }
    }
    const bool success = proc.exitStatus() == QProcess::NormalExit && proc.exitCode() == 0;
    if (okOut) *okOut = success;
    if (!success && errOut) *errOut = all;
    if (success && onDone) onDone(all);
}

void GitService::pull(const QString &repo, const QString &token, const QString &user) {
    if (!token.isEmpty()) {
        QProcess proc;
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        pe.insert("GIT_TERMINAL_PROMPT", "0");
        pe.insert("GIT_ASKPASS", writeAskpass(token, user));
        appendProxyEnv(pe);
        proc.setProcessEnvironment(pe);
        proc.setWorkingDirectory(repo);
        proc.start(m_gitPath, { "pull", "--ff-only" });
        proc.waitForFinished(-1);
        if (proc.exitCode() != 0)
            throw std::runtime_error(QString::fromUtf8(proc.readAllStandardError()).toStdString());
        return;
    }
    run({ "pull", "--ff-only" }, repo);
}

QString GitService::clone(const QString &url, const QString &targetDir,
                          const QString &token, const QString &user, const QString &into) {
    QDir().mkpath(targetDir);
    QStringList args { "clone", url };
    if (!into.isEmpty()) args << into;
    if (!token.isEmpty()) {
        QProcess proc;
        QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
        pe.insert("GIT_TERMINAL_PROMPT", "0");
        pe.insert("GIT_ASKPASS", writeAskpass(token, user));
        appendProxyEnv(pe);
        proc.setProcessEnvironment(pe);
        proc.setWorkingDirectory(targetDir);
        proc.start(m_gitPath, args);
        proc.waitForFinished(-1);
        if (proc.exitCode() != 0)
            throw std::runtime_error(QString::fromUtf8(proc.readAllStandardError()).toStdString());
    } else {
        run(args, targetDir);
    }
    if (!into.isEmpty()) return into;
    QString name = url.trimmed().chopped(1);
    name = name.section('/', -1);
    if (name.endsWith(".git")) name.chop(4);
    return QDir(targetDir).filePath(name);
}

QString GitService::stashSave(const QString &repo, const QString &msg) {
    QStringList args { "stash", "push" };
    if (!msg.isEmpty()) args << "-m" << msg;
    return run(args, repo, false);
}

QString GitService::stashPop(const QString &repo) {
    return run({ "stash", "pop" }, repo, false);
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
} // namespace

QPair<QString, QString> GitService::identity(const QString &repo) {
    // 全局作者信息缓存：设置页与启动预热共用，避免重复 spawn git
    if (repo.isEmpty() && g_identityValid) return g_identity;
    // 一条命令同时取回生效的 user.name / user.email（本地优先，自动回退全局）
    const QString raw = run({ "config", "--get-regexp", "^user\\.(name|email)$" }, repo, false);
    QString name, email;
    for (const QString &l : raw.split('\n', Qt::SkipEmptyParts)) {
        const int sp = l.indexOf(' ');
        if (sp <= 0) continue;
        const QString k = l.left(sp), v = l.mid(sp + 1).trimmed();
        if (k == QLatin1String("user.name")) name = v;
        else if (k == QLatin1String("user.email")) email = v;
    }
    if (repo.isEmpty()) { g_identity = { name, email }; g_identityValid = true; }
    return { name, email };
}

void GitService::setIdentity(const QString &name, const QString &email, bool globalScope) {
    g_identityValid = false;
    const QStringList scope = globalScope ? QStringList{ "--global" } : QStringList{ "--local" };
    run(QStringList{ "config" } + scope + QStringList{ "user.name", name });
    run(QStringList{ "config" } + scope + QStringList{ "user.email", email });
}

QString GitService::diagnoseNetwork() {
    return QStringLiteral("[Network diagnostics]");
}
