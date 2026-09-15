#include "repopaneldialog.h"
#include "releasedialog.h"
#include "i18n.h"
#include "theme.h"
#include "settings.h"
#include "paths.h"
#include "services/accountservice.h"
#include "services/gitservice.h"
#include "services/githubservice.h"
#include "services/giteeservice.h"
#include <QListWidget>
#include <QLineEdit>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QDir>
#include <QFileInfo>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QApplication>
#include <QPointer>
#include <QRadioButton>
#include <QDateTime>
#include <QLocale>
#include <functional>

namespace {
AccountService *acct() { static AccountService s; return &s; }
GitService *gitSvc() { static GitService s; return &s; }

// 从 repo JSON 取统一字段
QString repoName(const QJsonObject &o) { return o.value("name").toString(); }
QString repoFull(const QJsonObject &o) {
    return o.contains("full_name") ? o.value("full_name").toString()
                                   : o.value("name").toString();
}
QString repoCloneUrl(const QJsonObject &o) {
    return o.value("clone_url").toString(o.value("html_url").toString());
}

// 本地克隆副本的状态：约定位置与 startClone 的落盘规则一致 ——
// 本人仓库 → <root>/<platform>/users/<用户名>/<仓库名>，他人仓库 → <root>/<platform>/downloads/<仓库名>
enum class LocalRepoState { Missing, Empty, HasFiles };

// 不计入"仓库里的文件"的条目：
//   .git       —— 仓库自身的元数据
//   README.md  —— 平台"初始化仓库"时自动生成的占位文件（GitHub/Gitee 的 auto_init），
//                 不视为仓库内容，否则新建的仓库必须先跑一趟文件树才能删
// 本地判定与远程判定必须共用这套规则，否则会出现"本地算空、远程却拦下"的矛盾
bool isIgnorableRepoEntry(const QString &name) {
    return name.compare(QLatin1String(".git"), Qt::CaseInsensitive) == 0
           || name.compare(QLatin1String("README.md"), Qt::CaseInsensitive) == 0;
}

LocalRepoState localRepoState(const QString &repoName, QString *pathOut) {
    const Account a = acct()->currentAccount();
    const QString root = settings::storageRoot();
    const QString platform = a.platform.isEmpty() ? QStringLiteral("github") : a.platform;
    const QStringList cands {
        QStringLiteral("%1/%2/users/%3/%4").arg(root, platform, a.username, repoName),
        QStringLiteral("%1/%2/downloads/%3").arg(root, platform, repoName),
    };
    // 只数"文件"：在文件树里把文件删完之后，目录本身可能还残留（git 不跟踪空目录，
    // 删除文件也不会自动删父目录），把空目录也算成"还有文件"会让用户永远删不掉仓库
    std::function<bool(const QString &)> hasFile = [&](const QString &dir) -> bool {
        const QFileInfoList entries =
            QDir(dir).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
        for (const QFileInfo &fi : entries) {
            if (isIgnorableRepoEntry(fi.fileName())) continue;
            if (fi.isDir()) {
                if (hasFile(fi.absoluteFilePath())) return true;
            } else {
                return true;
            }
        }
        return false;
    };
    for (const QString &c : cands) {
        if (!QFileInfo::exists(c + "/.git")) continue;
        if (pathOut) *pathOut = c;
        return hasFile(c) ? LocalRepoState::HasFiles : LocalRepoState::Empty;
    }
    return LocalRepoState::Missing;
}
// ── 仓库行：左侧仓库名，右侧"最后修改时间"──
// 相对时间（"3 天前"）和网页版一致；精确时间放在 tooltip 里，比网页版只能看相对值更有用
QString humanAge(const QDateTime &dt) {
    if (!dt.isValid()) return {};
    const qint64 secs = dt.secsTo(QDateTime::currentDateTime());
    if (secs < 60) return i18n::t("time_just_now");   // 含服务端时间略超前（secs<0）的情况
    if (secs < 3600) return i18n::t("time_minutes_ago").arg(secs / 60);
    if (secs < 86400) return i18n::t("time_hours_ago").arg(secs / 3600);
    if (secs < 86400LL * 30) return i18n::t("time_days_ago").arg(secs / 86400);
    if (secs < 86400LL * 365) return i18n::t("time_months_ago").arg(secs / (86400LL * 30));
    return i18n::t("time_years_ago").arg(secs / (86400LL * 365));
}

// 用自定义行控件而不是 item 文本：只有这样才能把时间右对齐到行尾（和网页版一样）。
// 必须让鼠标事件穿透，否则行控件会截走点击，列表就选不中、也双击不了仓库了
QWidget *makeRepoRow(const QString &name, const QDateTime &updated, bool gitee) {
    auto *row = new QWidget;
    row->setAttribute(Qt::WA_TransparentForMouseEvents);
    auto *lay = new QHBoxLayout(row);
    lay->setContentsMargins(2, 0, 8, 0);
    lay->setSpacing(8);
    auto *icon = new QLabel;
    icon->setPixmap(QIcon(QStringLiteral(":/icon/%1.png").arg(gitee ? "gitee" : "github"))
                        .pixmap(18, 18));
    lay->addWidget(icon);
    auto *nameLbl = new QLabel(name);
    nameLbl->setStyleSheet(QString("color:%1;").arg(theme::text()));
    lay->addWidget(nameLbl);
    lay->addStretch(1);
    auto *timeLbl = new QLabel(humanAge(updated));
    timeLbl->setStyleSheet(QString("color:%1;font-size:11px;").arg(theme::textMuted()));
    if (updated.isValid())
        timeLbl->setToolTip(i18n::t("repo_updated_at")
                                .arg(QLocale().toString(updated.toLocalTime(), QLocale::ShortFormat)));
    lay->addWidget(timeLbl);
    return row;
}
} // namespace

RepoPanelDialog::RepoPanelDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("repo_search_ph"));
    // 大尺寸 + 大按钮：英文长文案（Clone & Open / Fork to my account 等）不截断
    setMinimumSize(980, 680);
    resize(1020, 720);
    m_gh = acct()->github();
    m_gitee = acct()->gitee();
    m_gh->setParent(this);
    m_gitee->setParent(this);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(10);
    layout->setContentsMargins(20, 18, 20, 16);

    // google 图标 + 标题（居中）
    auto *google = new QLabel;
    google->setPixmap(QIcon(":/icon/google.png").pixmap(72, 72));
    google->setAlignment(Qt::AlignCenter);
    layout->addWidget(google);
    auto *title = new QLabel(i18n::t("repo_search_ph"));
    title->setAlignment(Qt::AlignCenter);
    title->setStyleSheet(QString("font-size:18px;font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(title);

    auto *tip = new QLabel(QStringLiteral(
        "https://github.com/owner/repo\n"
        "https://gitee.com/owner/repo\nowner/repo"));
    tip->setAlignment(Qt::AlignCenter);
    tip->setStyleSheet(QString("color:%1;font-size:12px;").arg(theme::textMuted()));
    layout->addWidget(tip);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_searchInput = new QLineEdit;
    m_searchInput->setPlaceholderText(i18n::t("repo_search_ph"));
    m_searchInput->setMinimumHeight(36);
    connect(m_searchInput, &QLineEdit::returnPressed, this, &RepoPanelDialog::doSearch);
    row->addWidget(m_searchInput, 1);
    auto *searchBtn = new QPushButton(i18n::t("search"));
    searchBtn->setMinimumHeight(36);
    searchBtn->setMinimumWidth(96);
    connect(searchBtn, &QPushButton::clicked, this, &RepoPanelDialog::doSearch);
    row->addWidget(searchBtn);
    layout->addLayout(row);

    m_repoList = new QListWidget;
    m_repoList->setAlternatingRowColors(true);
    m_repoList->setIconSize(QSize(22, 22));
    m_repoList->setSpacing(2);
    connect(m_repoList, &QListWidget::itemDoubleClicked, this, &RepoPanelDialog::cloneSelected);
    layout->addWidget(m_repoList, 1);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    layout->addWidget(m_status);

    // 底部按钮：加高加内边距，两行布局避免英文挤在一行被省略
    auto mkBtn = [](QPushButton *b) {
        b->setMinimumHeight(36);
        b->setMinimumWidth(120);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };
    m_cloneBtn = mkBtn(new QPushButton(i18n::t("clone_open")));
    connect(m_cloneBtn, &QPushButton::clicked, this, &RepoPanelDialog::cloneSelected);
    m_forkBtn = mkBtn(new QPushButton(i18n::t("fork_btn")));
    connect(m_forkBtn, &QPushButton::clicked, this, &RepoPanelDialog::forkSelected);
    m_openBtn = mkBtn(new QPushButton(i18n::t("open_browser")));
    connect(m_openBtn, &QPushButton::clicked, this, &RepoPanelDialog::openInBrowser);
    m_refreshBtn = mkBtn(new QPushButton(i18n::t("refresh")));
    connect(m_refreshBtn, &QPushButton::clicked, this, &RepoPanelDialog::refreshMyRepos);
    m_createBtn = mkBtn(new QPushButton("+ " + i18n::t("create_repo_btn")));
    connect(m_createBtn, &QPushButton::clicked, this, &RepoPanelDialog::createRepo);
    m_deleteBtn = mkBtn(new QPushButton(i18n::t("delete_repo_btn")));
    connect(m_deleteBtn, &QPushButton::clicked, this, &RepoPanelDialog::deleteRepo);
    m_releaseBtn = mkBtn(new QPushButton(i18n::t("create_release")));
    connect(m_releaseBtn, &QPushButton::clicked, this, &RepoPanelDialog::createRelease);
    auto *closeBtn = mkBtn(new QPushButton(i18n::t("close")));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnRow1 = new QHBoxLayout;   // 常用操作
    btnRow1->setSpacing(10);
    btnRow1->addWidget(m_cloneBtn);
    btnRow1->addWidget(m_createBtn);
    btnRow1->addWidget(m_deleteBtn);
    btnRow1->addWidget(m_forkBtn);
    btnRow1->addWidget(m_openBtn);
    btnRow1->addWidget(m_refreshBtn);
    btnRow1->addStretch(1);
    auto *btnRow2 = new QHBoxLayout;   // 发布 + 关闭
    btnRow2->setSpacing(10);
    btnRow2->addStretch(1);
    btnRow2->addWidget(m_releaseBtn);
    btnRow2->addWidget(closeBtn);
    layout->addLayout(btnRow1);
    layout->addLayout(btnRow2);

    refreshMyRepos();
}

void RepoPanelDialog::refreshMyRepos() {
    m_repoList->clear();
    const Account a = acct()->currentAccount();
    // 未连接账户时直接引导，不发空 Token 请求（会得到 401 Bad credentials）
    if (a.token.isEmpty()) {
        m_status->setText("❌ " + i18n::t("no_account_hint"));
        m_status->setStyleSheet(QString("color:#f85149;"));
        return;
    }
    m_status->setText(i18n::t("loading_repo"));
    QPointer<RepoPanelDialog> self(this);
    // 按当前账户平台路由 API，Gitee Token 打 GitHub 必然 401
    auto done = [self](bool ok, const QJsonArray &arr, const QJsonObject &, const QString &err) {
        if (!self) return;
        self->onSearchDone(ok, arr, err);
    };
    if (a.platform == QLatin1String("gitee"))
        m_gitee->listMyRepos(done);
    else
        m_gh->listMyRepos(done);
}

void RepoPanelDialog::doSearch() {
    QString q = m_searchInput->text().trimmed();
    while (q.endsWith('/')) q.chop(1);
    if (q.isEmpty()) return;
    // 主机名匹配要不区分大小写：粘贴进来的 URL 常是 GitHub.com/... 这种写法，
    // 用区分大小写的 contains 会漏掉，于是整串被当成 owner/repo 去查 API
    for (const char *h : {"github.com/", "gitee.com/"}) {
        const int at = q.indexOf(QLatin1String(h), 0, Qt::CaseInsensitive);
        if (at >= 0) {
            q = q.mid(at + int(qstrlen(h)));
            break;
        }
    }
    const QStringList parts = q.split('/', Qt::SkipEmptyParts);
    if (parts.isEmpty()) return;
    const Account a = acct()->currentAccount();
    const bool gitee = a.platform == QLatin1String("gitee");
    QPointer<RepoPanelDialog> self(this);
    auto done = [self](bool ok, const QJsonArray &arr, const QJsonObject &obj, const QString &err) {
        if (!self) return;
        self->onSearchDone(ok && !obj.isEmpty(), obj.isEmpty() ? arr : QJsonArray{ obj }, err);
    };
    auto doneList = [self](bool ok, const QJsonArray &arr, const QJsonObject &, const QString &err) {
        if (!self) return;
        self->onSearchDone(ok, arr, err);
    };
    if (parts.size() >= 2) {
        if (gitee) m_gitee->getRepo(parts[0], parts[1], done);
        else       m_gh->getRepo(parts[0], parts[1], done);
    } else {
        if (gitee) m_gitee->listUserRepos(parts[0], doneList);
        else       m_gh->listUserRepos(parts[0], doneList);
    }
    m_status->setText(i18n::t("searching"));
}

void RepoPanelDialog::onSearchDone(bool ok, const QJsonArray &arr, const QString &err) {
    m_repoList->clear();
    if (!ok) {
        // 刷新失败时丢掉待高亮项，避免它落到下一次成功渲染里选中一个不相干的仓库
        m_pendingSelect.clear();
        m_pendingMsg.clear();
        // 401/凭据无效 → 引导重新连接账户，而不是裸抛 API 错误
        if (err.contains(QLatin1String("Bad credentials"), Qt::CaseInsensitive)
            || err.contains(QLatin1String("Unauthorized"), Qt::CaseInsensitive)
            || err.contains(QLatin1String("401"))) {
            m_status->setText("❌ " + i18n::t("token_invalid_msg"));
            m_status->setStyleSheet(QString("color:#f85149;"));
            return;
        }
        m_status->setText("❌ " + i18n::t("search_failed") + ": " + err);
        m_status->setStyleSheet(QString("color:#f85149;"));
        return;
    }
    const Account a = acct()->currentAccount();
    const bool gitee = a.platform == QLatin1String("gitee");
    for (const auto &v : arr) {
        const QJsonObject o = v.toObject();
        const QString full = repoFull(o);
        // 行内容全部由自定义行控件绘制（仓库名 + 行尾时间），item 文本留空，
        // 否则空文本之上会再叠一层 item 自己的文字
        auto *item = new QListWidgetItem;
        item->setSizeHint(QSize(0, 28));   // 行高：文本为空时不会自动留出合适高度
        item->setData(Qt::UserRole, o);
        m_repoList->addItem(item);
        // 网页版用 updated_at（设置/Star 变动也会刷新它），与网页"Updated"一致；
        // 没有该字段时退回 pushed_at（最后一次推送时间）
        const QString stamp = o.value("updated_at").toString(o.value("pushed_at").toString());
        m_repoList->setItemWidget(item, makeRepoRow(full, QDateTime::fromString(stamp, Qt::ISODate), gitee));
    }
    m_status->setText(i18n::t("repos_found").arg(arr.size()));
    m_status->setStyleSheet(QString("color:%1;").arg(theme::accent()));
    // 创建/Fork 触发的刷新：选中刚操作的那一项，让"列表里多出来的它"成为成功反馈
    if (!m_pendingSelect.isEmpty()) {
        const QString suffix = QLatin1String("/") + m_pendingSelect;
        for (int i = 0; i < m_repoList->count(); ++i) {
            QListWidgetItem *it = m_repoList->item(i);
            const QString name = repoFull(it->data(Qt::UserRole).toJsonObject());
            if (name == m_pendingSelect || name.endsWith(suffix)) {
                m_repoList->setCurrentItem(it);
                m_repoList->scrollToItem(it);
                break;
            }
        }
        m_pendingSelect.clear();
    }
    if (!m_pendingMsg.isEmpty()) {
        m_status->setText(m_pendingMsg);
        m_pendingMsg.clear();
    }
}

void RepoPanelDialog::startClone(const QString &url, const QString &name) {
    const Account a = acct()->currentAccount();
    const QString base = settings::storageRoot();
    const QString platform = a.platform.isEmpty() ? "github" : a.platform;
    const QString user = a.username;
    // 自动归类：本人仓库 → users/<用户名>，他人 → downloads
    QString dest;
    if (url.contains('/' + user) || url.section('/', -1).contains(user))
        dest = QStringLiteral("%1/%2/users/%3").arg(base, platform, user);
    else
        dest = QStringLiteral("%1/%2/downloads").arg(base, platform);
    QDir().mkpath(dest);
    const QString target = dest + "/" + name;
    if (QFileInfo::exists(target) && !QDir(target).isEmpty()) {
        QMessageBox::warning(this, i18n::t("dir_exists"),
                             i18n::t("dir_exists_body").arg(target));
        return;
    }
    m_status->setText(i18n::t("cloning").arg(name));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    try {
        const QString path = gitSvc()->clone(url, dest, a.token, a.username, target);
        QApplication::restoreOverrideCursor();
        m_status->setText("✅ " + i18n::t("clone_success_body").arg(path));
        emit repoCloned(path);
    } catch (const std::exception &e) {
        QApplication::restoreOverrideCursor();
        m_status->setText("❌ " + i18n::t("clone_failed") + ": " + e.what());
        QMessageBox::warning(this, i18n::t("clone_failed"), e.what());
    }
}

void RepoPanelDialog::cloneSelected() {
    auto *item = m_repoList->currentItem();
    if (!item) return;
    const QJsonObject o = item->data(Qt::UserRole).toJsonObject();
    startClone(repoCloneUrl(o), repoName(o));
}

void RepoPanelDialog::forkSelected() {
    auto *item = m_repoList->currentItem();
    if (!item) return;
    const QJsonObject o = item->data(Qt::UserRole).toJsonObject();
    const QString full = repoFull(o);
    const QString owner = full.section('/', 0, 0);
    const QString repo = full.section('/', 1);
    const Account a = acct()->currentAccount();
    QPointer<RepoPanelDialog> self(this);
    auto done = [self, full, repo](bool ok, const QJsonArray &, const QJsonObject &, const QString &err) {
        if (!self) return;
        if (ok) {
            // Fork 成功 → 主动刷新"我的仓库"并选中它：这个仓库已经确定存在了，
            // 再让用户自己去点刷新是多余的（创建仓库同理）
            self->m_pendingSelect = repo;
            self->m_pendingMsg = "✅ " + i18n::t("forked") + ": " + full;
            self->refreshMyRepos();
            return;
        }
        const QString msg = err.contains(QLatin1String("Bad credentials"), Qt::CaseInsensitive)
                                ? i18n::t("token_invalid_msg") : err;
        self->m_status->setText("❌ " + i18n::t("fork_failed") + ": " + msg);
        self->m_status->setStyleSheet(QStringLiteral("color:#f85149;"));
    };
    if (a.platform == QLatin1String("gitee"))
        m_gitee->forkRepo(owner, repo, done);
    else
        m_gh->forkRepo(owner, repo, done);
}

void RepoPanelDialog::openInBrowser() {
    auto *item = m_repoList->currentItem();
    if (!item) return;
    const QUrl url = QUrl(item->data(Qt::UserRole).toJsonObject().value("html_url").toString());
    if (url.isValid()) QDesktopServices::openUrl(url);
}

void RepoPanelDialog::createRepo() {
    // 自建对话框：仓库名 + 公开/私有（QInputDialog 放不下单选）
    QDialog dlg(this);
    dlg.setWindowTitle(i18n::t("create_repo_btn"));
    dlg.setMinimumWidth(440);
    auto *v = new QVBoxLayout(&dlg);
    v->setSpacing(10);
    auto *nameLbl = new QLabel(i18n::t("repo_name_label"));
    v->addWidget(nameLbl);
    auto *nameEdit = new QLineEdit;
    nameEdit->setPlaceholderText(QStringLiteral("my-new-repo"));
    nameEdit->setMinimumHeight(32);
    v->addWidget(nameEdit);
    auto *visLbl = new QLabel(i18n::t("repo_visibility"));
    v->addWidget(visLbl);
    auto *pub = new QRadioButton(i18n::t("repo_public"));
    auto *priv = new QRadioButton(i18n::t("repo_private"));
    pub->setChecked(true);
    v->addWidget(pub);
    v->addWidget(priv);
    auto *row = new QHBoxLayout;
    row->addStretch(1);
    auto *cancel = new QPushButton(i18n::t("cancel"));
    auto *ok = new QPushButton(i18n::t("create_repo_btn"));
    ok->setDefault(true);
    row->addWidget(cancel);
    row->addWidget(ok);
    v->addLayout(row);
    connect(ok, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancel, &QPushButton::clicked, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) return;
    const QString name = nameEdit->text().trimmed();
    if (name.isEmpty()) { QMessageBox::warning(this, i18n::t("hint"), i18n::t("enter_repo_name")); return; }

    const Account a = acct()->currentAccount();
    if (a.token.isEmpty()) { m_status->setText("❌ " + i18n::t("no_account_hint")); return; }
    const bool wantPrivate = priv->isChecked();   // priv 是上面的私有单选按钮
    QPointer<RepoPanelDialog> self(this);
    auto done = [self, name](bool ok, const QJsonArray &, const QJsonObject &obj, const QString &err) {
        if (!self) return;
        if (!ok) {
            QMessageBox::warning(self, i18n::t("create_failed"), err);
            return;
        }
        // 创建成功就先刷新"我的仓库"：仓库已经建好了，让用户再手动点一次刷新
        // 是多余的。刷新结果里会定位并选中它，操作是否生效一眼可见
        self->m_pendingSelect = name;
        self->m_pendingMsg = "✅ " + i18n::t("created") + ": " + name;
        self->refreshMyRepos();
        // GitHub 返回 clone_url；Gitee 只有 html_url（补 .git），为空则误报失败
        QString url = obj.value("clone_url").toString();
        if (url.isEmpty()) url = obj.value("html_url").toString();
        if (url.isEmpty()) {
            self->m_status->setText("✅ " + name + " — " + i18n::t("saved_as_current"));
            return;
        }
        if (!url.endsWith(QLatin1String(".git"))) url += QLatin1String(".git");
        self->m_status->setText("✅ " + name + " — " + url);
        self->startClone(url, name);
    };
    // 走平台服务而不是手拼 JSON POST：Gitee 的建仓接口只认表单参数
    //（此前这里对两个平台都发 JSON，Gitee 上会失败）
    if (a.platform == QLatin1String("gitee"))
        m_gitee->createRepo(name, QString(), wantPrivate, true, done);
    else
        m_gh->createRepo(name, QString(), wantPrivate, true, QString(), QString(), done);
}

// 删除仓库：硬条件 = 仓库里没有任何文件（.git 除外）。
// 本地有副本就查本地（文件名树里删完、提交推送后本地即为空）；
// 本地没有副本才去问远程，避免"不克隆就能绕过检查"把有内容的仓库删掉
void RepoPanelDialog::deleteRepo() {
    auto *item = m_repoList->currentItem();
    if (!item) { m_status->setText("❌ " + i18n::t("select_repo_first")); return; }
    const QJsonObject o = item->data(Qt::UserRole).toJsonObject();
    const QString full = repoFull(o);
    const QString owner = full.section('/', 0, 0);
    const QString repo = full.section('/', 1);
    if (owner.isEmpty() || repo.isEmpty()) {
        m_status->setText("❌ " + i18n::t("no_remote"));
        return;
    }
    const Account a = acct()->currentAccount();
    if (a.token.isEmpty()) { m_status->setText("❌ " + i18n::t("no_account_hint")); return; }

    QString localPath;
    switch (localRepoState(repo, &localPath)) {
    case LocalRepoState::HasFiles:
        // 不给"仍然删除"的选项：文件必须先在文件树里清空并推送出去
        QMessageBox::warning(this, i18n::t("delete_repo_title"),
                             i18n::t("delete_repo_not_empty").arg(full, localPath));
        m_status->setText("❌ " + i18n::t("delete_repo_not_empty_short"));
        return;
    case LocalRepoState::Missing:
        checkRemoteEmptyThenDelete(owner, repo, full);
        return;
    case LocalRepoState::Empty:
        confirmAndDelete(owner, repo, full);
        return;
    }
}

// 本地没有副本：用 /contents 判断远程是否为空。
// 空仓库时 GitHub 直接返回 404（body 是 "This repository is empty."，而 apiError
// 取的是 message 字段，不会有 "404" 字样），Gitee 返回空数组或 404 —— 都要算空。
// 判定口径必须与本地一致：除 .git / README.md 外的条目才算内容。
// 判断不出来（网络/权限异常）时不放行，给出"先克隆下来确认"的可行路径
void RepoPanelDialog::checkRemoteEmptyThenDelete(const QString &owner, const QString &repo,
                                                 const QString &full) {
    m_status->setText(i18n::t("delete_repo_checking"));
    RestService *svc = acct()->currentAccount().platform == QLatin1String("gitee")
        ? static_cast<RestService *>(m_gitee)
        : static_cast<RestService *>(m_gh);
    QPointer<RepoPanelDialog> self(this);
    svc->get(QStringLiteral("/repos/%1/%2/contents").arg(owner, repo),
             [self, owner, repo, full](bool ok, const QJsonArray &arr, const QJsonObject &obj,
                                       const QString &err) {
        if (!self) return;
        const bool saysEmpty = err.contains(QLatin1String("404"))
                               || err.contains(QLatin1String("is empty"), Qt::CaseInsensitive)
                               || err.contains(QLatin1String("empty"), Qt::CaseInsensitive)
                               || err.contains(QStringLiteral("为空"));
        // 根目录下只有 README.md（以及 .git 之类）时也算空，与本地判定保持一致
        bool onlyIgnorable = true;
        for (const auto &v : arr) {
            if (!isIgnorableRepoEntry(v.toObject().value("name").toString())) {
                onlyIgnorable = false;
                break;
            }
        }
        if (saysEmpty || (ok && obj.isEmpty() && onlyIgnorable)) {
            self->confirmAndDelete(owner, repo, full);
            return;
        }
        if (!ok) {
            QMessageBox::warning(self, i18n::t("delete_repo_title"),
                                 i18n::t("delete_repo_unverified").arg(full, err));
            self->m_status->setText("❌ " + i18n::t("delete_repo_failed"));
            self->m_status->setStyleSheet(QStringLiteral("color:#f85149;"));
            return;
        }
        // 远程还有内容：引导先克隆下来、用文件树清空再删（与本地有副本时的要求一致）
        QMessageBox::warning(self, i18n::t("delete_repo_title"),
                             i18n::t("delete_repo_remote_not_empty").arg(full));
        self->m_status->setText("❌ " + i18n::t("delete_repo_not_empty_short"));
    });
}

void RepoPanelDialog::confirmAndDelete(const QString &owner, const QString &repo,
                                       const QString &full) {
    QMessageBox box(this);
    box.setWindowTitle(i18n::t("delete_repo_title"));
    box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(i18n::t("delete_repo_confirm").arg(full));
    auto *delBtn = box.addButton(i18n::t("delete_repo_confirm_btn"), QMessageBox::DestructiveRole);
    auto *cancelBtn = box.addButton(i18n::t("cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(cancelBtn);   // 默认落在"取消"，回车不会误删
    box.exec();
    if (box.clickedButton() != delBtn) return;

    m_status->setText(i18n::t("delete_repo_doing"));
    m_status->setStyleSheet(QString("color:%1;").arg(theme::accent()));
    QPointer<RepoPanelDialog> self(this);
    auto done = [self, full](bool ok, const QJsonArray &, const QJsonObject &, const QString &err) {
        if (!self) return;
        if (!ok) {
            // delete_repo（Gitee 为 projects）是独立权限，只有 repo 权限的 Token 会 403，
            // 这里把原因翻译成可执行的动作，而不是丢一句 Forbidden
            const bool perm = err.contains(QLatin1String("delete_repo"), Qt::CaseInsensitive)
                              || err.contains(QLatin1String("Forbidden"), Qt::CaseInsensitive)
                              || err.contains(QLatin1String("403"))
                              || err.contains(QLatin1String("insufficient"), Qt::CaseInsensitive);
            const QString msg = perm ? i18n::t("delete_repo_need_scope") : err;
            self->m_status->setText("❌ " + i18n::t("delete_repo_failed") + ": " + msg);
            self->m_status->setStyleSheet(QStringLiteral("color:#f85149;"));
            QMessageBox::warning(self, i18n::t("delete_repo_failed"), msg);
            return;
        }
        // 删掉了就刷新"我的仓库"，并把结果文案留到刷新完成后显示（同创建/Fork 的逻辑顺序）
        self->m_pendingMsg = "✅ " + i18n::t("delete_repo_done").arg(full);
        self->refreshMyRepos();
    };
    if (acct()->currentAccount().platform == QLatin1String("gitee"))
        m_gitee->deleteRepo(owner, repo, done);
    else
        m_gh->deleteRepo(owner, repo, done);
}

void RepoPanelDialog::createRelease() {
    auto *item = m_repoList->currentItem();
    if (!item) {
        m_status->setText("❌ " + i18n::t("release_repo") + ": —");
        return;
    }
    const QJsonObject o = item->data(Qt::UserRole).toJsonObject();
    const QString full = repoFull(o);
    const QString owner = full.section('/', 0, 0);
    const QString repo = full.section('/', 1);
    if (owner.isEmpty() || repo.isEmpty()) {
        m_status->setText("❌ " + i18n::t("no_remote"));
        return;
    }
    const Account a = acct()->currentAccount();
    const QString platform = a.platform.isEmpty() ? QStringLiteral("github") : a.platform;
    ReleaseDialog dlg(owner, repo, platform, this);
    if (dlg.exec() == QDialog::Accepted)
        m_status->setText("✅ " + i18n::t("release_created"));
}
