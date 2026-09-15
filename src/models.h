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
    QString path, source;   // source: "current"（工作区现存）/"history"（已进历史）/"pending"（即将进入本次提交）
    qint64 size = 0;
    double sizeMb = 0;
    QString firstCommit;    // 历史文件首次进入的提交（空=工作区文件）；软回退方案的锚点
    QString blobId;         // 历史文件的 blob 哈希：清理时按 blob 定位，避免改名/多路径漏删
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
