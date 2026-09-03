#pragma once
#include <QString>
#include <QList>

// ── Git 文件状态 ──
enum class GitFileStatus { Modified, Added, Deleted, Renamed, Untracked, Conflict };

struct GitFileItem {
    QString path;
    GitFileStatus status = GitFileStatus::Modified;
    bool staged = false;
};

struct GitRepoStatus {
    QString branch;
    int ahead = 0, behind = 0;
    QList<GitFileItem> files;
};

struct CommitInfo {
    QString hash, shortHash, subject, author, date;
};

struct OversizedFile {
    QString path, source;
    qint64 size = 0;
    double sizeMb = 0;
};

// ── 账户（Token 加密存储）──
struct Account {
    QString username;
    QString token;          // 明文（仅内存中）
    QString platform = "github";   // github / gitee
    QString expiresAt;      // Token 过期日期（ISO 格式，空=未设置）
    QString keyB64, nonceB64;

    QString host() const { return platform == "gitee" ? "gitee.com" : "github.com"; }
};
