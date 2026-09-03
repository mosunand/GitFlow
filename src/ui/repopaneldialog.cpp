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
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QDesktopServices>
#include <QApplication>
#include <QPointer>
#include <QRadioButton>

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
        "https://github.com/bilawalsidhu/gods-eye-view\n"
        "https://gitee.com/owner/repo\nowner/repo"));
    tip->setAlignment(Qt::AlignCenter);
    tip->setStyleSheet(QString("color:%1;font-size:12px;").arg(theme::textMuted()));
    layout->addWidget(tip);

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    m_searchInput = new QLineEdit;
    m_searchInput->setPlaceholderText(i18n::t("search_ph"));
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
    m_releaseBtn = mkBtn(new QPushButton(i18n::t("create_release")));
    connect(m_releaseBtn, &QPushButton::clicked, this, &RepoPanelDialog::createRelease);
    auto *closeBtn = mkBtn(new QPushButton(i18n::t("close")));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);

    auto *btnRow1 = new QHBoxLayout;   // 常用操作
    btnRow1->setSpacing(10);
    btnRow1->addWidget(m_cloneBtn);
    btnRow1->addWidget(m_createBtn);
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
    m_status->setText(i18n::t("loading"));
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
    QString q = m_searchInput->text().trimmed().chopped(0);
    while (q.endsWith('/')) q.chop(1);
    if (q.isEmpty()) return;
    for (const char *h : {"github.com/", "gitee.com/"}) {
        if (q.contains(h)) { q = q.section(h, 1); break; }
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
        auto *item = new QListWidgetItem(
            QIcon(QStringLiteral(":/icon/%1.png").arg(gitee ? "gitee" : "github")), full);
        item->setData(Qt::UserRole, o);
        m_repoList->addItem(item);
    }
    m_status->setText(i18n::t("repos_found").arg(arr.size()));
    m_status->setStyleSheet(QString("color:%1;").arg(theme::accent()));
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
    auto done = [self, full](bool ok, const QJsonArray &, const QJsonObject &, const QString &err) {
        if (!self) return;
        const QString msg = err.contains(QLatin1String("Bad credentials"), Qt::CaseInsensitive)
                                ? i18n::t("token_invalid_msg") : err;
        self->m_status->setText(ok ? "✅ " + i18n::t("forked") + ": " + full
                                   : "❌ " + i18n::t("fork_failed") + ": " + msg);
        self->m_status->setStyleSheet(QString("color:%1;").arg(ok ? "#2ea043" : "#f85149"));
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
    RestService *svc = a.platform == QLatin1String("gitee")
        ? static_cast<RestService *>(m_gitee)
        : static_cast<RestService *>(m_gh);
    QJsonObject body;
    body.insert("name", name);
    body.insert("auto_init", true);
    body.insert("private", priv->isChecked());

    QPointer<RepoPanelDialog> self(this);
    svc->post("/user/repos", body,
              [self, name](bool ok, const QJsonArray &, const QJsonObject &obj, const QString &err) {
        if (!self) return;
        if (!ok) {
            QMessageBox::warning(self, i18n::t("create_failed"), err);
            return;
        }
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
    });
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
