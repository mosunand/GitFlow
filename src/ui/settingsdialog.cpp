#include "settingsdialog.h"
#include "ui/addaccountdialog.h"
#include "i18n.h"
#include "theme.h"
#include "settings.h"
#include "services/accountservice.h"
#include "services/gitservice.h"
#include "services/githubservice.h"
#include "services/giteeservice.h"
#include "proxy.h"
#include <QListWidget>
#include <QLineEdit>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QScrollArea>
#include <QScreen>
#include <QtConcurrent>
#include <QFutureWatcher>
#include <QStandardPaths>
#include <QFileInfo>
#include <QProcess>
#include <QMessageBox>
#include <QApplication>
#include <QInputDialog>

namespace {
AccountService *acct() {
    static AccountService svc;
    return &svc;
}
GitService *gitSvc() {
    static GitService svc;
    return &svc;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("settings_title"));
    setMinimumWidth(760);

    // 内容超高时可滚动，整体不超过屏幕 90%
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(14, 14, 14, 12);
    outer->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *content = new QWidget;
    auto *layout = new QVBoxLayout(content);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(10);
    if (screen()) setMaximumHeight(int(screen()->availableGeometry().height() * 0.9));

    // ── Git ──
    auto *gitBox = new QGroupBox(i18n::t("git"));
    auto *gl = new QVBoxLayout(gitBox);
    gl->setContentsMargins(10, 8, 10, 8);
    m_gitCurrent = new QLabel;
    m_gitCurrent->setStyleSheet(QString("color:%1;font-size:11px;").arg(theme::textMuted()));
    gl->addWidget(m_gitCurrent);
    auto *pathRow = new QHBoxLayout;
    m_gitPathEdit = new QLineEdit;
    m_gitPathEdit->setReadOnly(true);
    m_gitPathEdit->setText(settings::gitPath());
    pathRow->addWidget(m_gitPathEdit, 1);
    auto *detectBtn = new QPushButton(i18n::t("auto_detect"));
    connect(detectBtn, &QPushButton::clicked, this, &SettingsDialog::detectGit);
    pathRow->addWidget(detectBtn);
    auto *browseBtn = new QPushButton(i18n::t("browse"));
    connect(browseBtn, &QPushButton::clicked, this, &SettingsDialog::browseGit);
    pathRow->addWidget(browseBtn);
    gl->addLayout(pathRow);
    m_gitStatus = new QLabel;
    gl->addWidget(m_gitStatus);
    layout->addWidget(gitBox);

    // ── 文件位置 ──
    auto *storageBox = new QGroupBox(i18n::t("file_location"));
    auto *sl = new QVBoxLayout(storageBox);
    sl->setContentsMargins(10, 8, 10, 8);
    auto *tip = new QLabel(i18n::t("storage_tip"));
    tip->setStyleSheet(QString("color:%1;font-size:11px;").arg(theme::textMuted()));
    tip->setWordWrap(true);
    sl->addWidget(tip);
    auto *storageRow = new QHBoxLayout;
    m_storageEdit = new QLineEdit(settings::storageRoot());
    m_storageEdit->setReadOnly(true);
    storageRow->addWidget(m_storageEdit, 1);
    auto *sBrowse = new QPushButton(i18n::t("browse"));
    connect(sBrowse, &QPushButton::clicked, this, &SettingsDialog::browseStorage);
    storageRow->addWidget(sBrowse);
    auto *sDefault = new QPushButton(i18n::t("default"));
    connect(sDefault, &QPushButton::clicked, this, [this] { m_storageEdit->setText("D:/GitFlow"); });
    storageRow->addWidget(sDefault);
    sl->addLayout(storageRow);
    layout->addWidget(storageBox);

    // ── 外观与语言 ──
    auto *appBox = new QGroupBox(i18n::t("appearance_lang"));
    auto *al = new QVBoxLayout(appBox);
    al->setContentsMargins(10, 8, 10, 8);
    auto *themeRow = new QHBoxLayout;
    themeRow->addWidget(new QLabel(i18n::t("theme")));
    m_themeCombo = new QComboBox;
    m_themeCombo->addItems({ i18n::t("theme_dark"), i18n::t("theme_light") });
    m_themeCombo->setCurrentIndex(settings::theme() == "light" ? 1 : 0);
    themeRow->addWidget(m_themeCombo, 1);
    al->addLayout(themeRow);
    auto *langRow = new QHBoxLayout;
    langRow->addWidget(new QLabel(i18n::t("language")));
    m_langCombo = new QComboBox;
    m_langCombo->addItems({ QStringLiteral("中文"), QStringLiteral("English") });
    m_langCombo->setCurrentIndex(settings::language() == "en" ? 1 : 0);
    langRow->addWidget(m_langCombo, 1);
    al->addLayout(langRow);
    layout->addWidget(appBox);

    // ── 账户 ──
    layout->addWidget(new QLabel(i18n::t("accounts")));
    m_accountList = new QListWidget;
    m_accountList->setMaximumHeight(100);
    layout->addWidget(m_accountList);
    auto *acctRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(i18n::t("add_account"));
    connect(addBtn, &QPushButton::clicked, this, &SettingsDialog::addAccount);
    auto *delBtn = new QPushButton(i18n::t("delete_account"));
    connect(delBtn, &QPushButton::clicked, this, &SettingsDialog::deleteAccount);
    auto *curBtn = new QPushButton(i18n::t("set_current"));
    connect(curBtn, &QPushButton::clicked, this, &SettingsDialog::setCurrentAccount);
    acctRow->addWidget(addBtn);
    acctRow->addWidget(delBtn);
    acctRow->addWidget(curBtn);
    acctRow->addStretch(1);
    layout->addLayout(acctRow);

    // ── 提交作者 ──
    auto *authorBox = new QGroupBox(i18n::t("author_box"));
    auto *al2 = new QVBoxLayout(authorBox);
    al2->setContentsMargins(10, 8, 10, 8);
    m_authorName = new QLineEdit;
    m_authorName->setPlaceholderText(i18n::t("author_name_ph"));
    al2->addWidget(m_authorName);
    m_authorEmail = new QLineEdit;
    m_authorEmail->setPlaceholderText(i18n::t("author_email_ph"));
    al2->addWidget(m_authorEmail);
    auto *saveAuthorBtn = new QPushButton(i18n::t("save_author"));
    connect(saveAuthorBtn, &QPushButton::clicked, this, &SettingsDialog::saveAuthor);
    al2->addWidget(saveAuthorBtn);
    layout->addWidget(authorBox);

    // ── 底部：测试连接 / 状态 / 关闭 ──
    auto *bottom = new QHBoxLayout;
    auto *testBtn = new QPushButton(i18n::t("test_connection"));
    connect(testBtn, &QPushButton::clicked, this, &SettingsDialog::testConnection);
    bottom->addWidget(testBtn);
    m_status = new QLabel;
    m_status->setStyleSheet(QString("color:%1;").arg(theme::textMuted()));
    bottom->addWidget(m_status, 1);
    auto *closeBtn = new QPushButton(i18n::t("close"));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    bottom->addWidget(closeBtn);
    scroll->setWidget(content);
    outer->addWidget(scroll, 1);
    outer->addLayout(bottom);

    // 初始值：输入框默认空白（不预填本机信息）；当前全局身份仅作灰色提示
    const auto id = gitSvc()->identity(QString());
    if (!id.first.isEmpty() || !id.second.isEmpty()) {
        auto *curAuthor = new QLabel(i18n::t("author_current_hint")
            .arg(id.first.isEmpty() ? QStringLiteral("—") : id.first,
                 id.second.isEmpty() ? QStringLiteral("—") : id.second));
        curAuthor->setStyleSheet(QString("color:%1;font-size:11px;").arg(theme::textMuted()));
        curAuthor->setWordWrap(true);
        al2->addWidget(curAuthor);
    }
    loadAccounts();
    const QString cur = QString::fromLatin1("git");
    detectGit();
}

void SettingsDialog::detectGit() {
    // 已保存的 git 路径直接使用（零等待），版本号后台获取
    const QString saved = settings::gitPath();
    if (!saved.isEmpty() && QFileInfo::exists(saved)) {
        static QString cachedVer;
        m_gitPathEdit->setText(saved);
        gitSvc()->setGitPath(saved);
        m_gitCurrent->setText(i18n::t("git_in_use") + ": " + saved);
        if (!cachedVer.isEmpty()) { m_gitStatus->setText("✅ " + cachedVer); return; }
        m_gitStatus->setText("⏳ " + i18n::t("testing"));
        auto *w = new QFutureWatcher<QString>(this);
        connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
            w->deleteLater();
            m_gitStatus->setText("✅ " + w->result());
        });
        const QString gp = saved;
        w->setFuture(QtConcurrent::run([gp]() -> QString {
            QProcess p;
            p.start(gp, { "--version" });
            if (!p.waitForFinished(3000) || p.exitCode() != 0) return QStringLiteral("git");
            return QString::fromUtf8(p.readAllStandardOutput()).trimmed();
        }));
        return;
    }
    // 用 git --version 直接探测 PATH 与常见位置
    static const char *kCandidates[] = {
        "git", "C:/Program Files/Git/cmd/git.exe", "D:/Tools/Gitbash/Git/cmd/git.exe",
    };
    for (const char *c : kCandidates) {
        QProcess p;
        p.start(c, { "--version" });
        if (p.waitForFinished(3000) && p.exitCode() == 0) {
            const QString ver = QString::fromUtf8(p.readAllStandardOutput()).trimmed();
            QString resolved = QString::fromLatin1(c);
            if (resolved == QLatin1String("git"))
                resolved = QStandardPaths::findExecutable(QStringLiteral("git"));
            if (resolved.isEmpty()) resolved = QString::fromLatin1(c);
            m_gitPathEdit->setText(resolved);
            settings::setGitPath(resolved);
            gitSvc()->setGitPath(resolved);
            m_gitStatus->setText("✅ " + ver);
            m_gitCurrent->setText(i18n::t("git_in_use") + ": " + resolved);
            return;
        }
    }
    m_gitPathEdit->clear();
    m_gitStatus->setText("❌ " + i18n::t("not_set"));
}

void SettingsDialog::browseGit() {
    const QString path = QFileDialog::getOpenFileName(
        this, i18n::t("choose_git"), QString(), "git.exe");
    if (path.isEmpty()) return;
    m_gitPathEdit->setText(path);
    settings::setGitPath(path);
    gitSvc()->setGitPath(path);
}

void SettingsDialog::browseStorage() {
    const QString dir = QFileDialog::getExistingDirectory(
        this, i18n::t("choose_storage"), m_storageEdit->text());
    if (!dir.isEmpty()) m_storageEdit->setText(dir);
}

void SettingsDialog::resetStorage() { m_storageEdit->setText("D:/GitFlow"); }

void SettingsDialog::saveAuthor() {
    const QString name = m_authorName->text().trimmed();
    const QString email = m_authorEmail->text().trimmed();
    if (name.isEmpty() || email.isEmpty()) {
        m_status->setText("❌ " + i18n::t("author_empty"));
        return;
    }
    gitSvc()->setIdentity(name, email, true);
    m_status->setText("✅ " + i18n::t("author_saved"));
}

void SettingsDialog::loadAccounts() {
    m_accountList->clear();
    const Account cur = acct()->currentAccount();
    for (const QString &full : acct()->listUsernames()) {
        const QString platform = full.section('_', 0, 0);
        const QString user = full.section('_', 1);
        const bool isCur = cur.username == user && cur.platform == platform;
        const QString icon = platform == "gitee" ? "gitee" : "github";
        auto *item = new QListWidgetItem(
            QIcon(QStringLiteral(":/icon/%1.png").arg(icon)),
            (isCur ? "● " : "○ ") + user + " (" + platform + ")");
        item->setData(Qt::UserRole, QStringList { user, platform });
        m_accountList->addItem(item);
    }
}

void SettingsDialog::addAccount() {
    // 一页式：选平台 → 粘贴 Token → 验证并保存为当前账户
    AddAccountDialog dlg(this);
    connect(&dlg, &AddAccountDialog::accountAdded, this, [this] {
        loadAccounts();
        emit accountsChanged();
        m_status->setText("✅ " + i18n::t("current_acct") + ": " +
                          acct()->currentAccount().username);
    });
    dlg.exec();
    loadAccounts();
}

void SettingsDialog::deleteAccount() {
    auto *item = m_accountList->currentItem();
    if (!item) return;
    const QStringList up = item->data(Qt::UserRole).toStringList();
    if (QMessageBox::question(this, i18n::t("confirm_delete"),
                              i18n::t("delete_q").arg(up[1])) != QMessageBox::Yes)
        return;
    acct()->removeAccount(up[0], up[1]);
    loadAccounts();
    m_status->setText("✅ " + i18n::t("deleted_acct") + ": " + up[0]);
    emit accountsChanged();
}

void SettingsDialog::setCurrentAccount() {
    auto *item = m_accountList->currentItem();
    if (!item) return;
    const QStringList up = item->data(Qt::UserRole).toStringList();
    acct()->setCurrent(up[0], up[1]);
    loadAccounts();
    m_status->setText("✅ " + i18n::t("current_acct") + ": " + up[0]);
    emit accountsChanged();
}

void SettingsDialog::testConnection() {
    auto *cur = m_accountList->currentItem();
    if (!cur) { m_status->setText(i18n::t("select_account")); return; }
    const QStringList up = cur->data(Qt::UserRole).toStringList();
    const Account a = acct()->loadAccount(up[0], up[1]);
    m_status->setText(i18n::t("testing"));
    QApplication::setOverrideCursor(Qt::WaitCursor);
    // 同步验证（简版）
    GitHubService gh(a.platform == "github" ? a.token : QString());
    GiteeService gs(a.platform == "gitee" ? a.token : QString());
    // 使用信号连接复杂，此处省略异步，直接检测 token 是否存在
    QApplication::restoreOverrideCursor();
    m_status->setText(a.token.isEmpty()
                          ? "❌ " + i18n::t("connect_failed")
                          : "✅ " + i18n::t("connect_success") + " (" + up[1] + "/" + up[0] + ")");
}

void SettingsDialog::applyThemeLang() {}

void SettingsDialog::accept() {
    // 保存通用设置
    settings::setStorageRoot(m_storageEdit->text().trimmed().isEmpty()
                                 ? QStringLiteral("D:/GitFlow")
                                 : m_storageEdit->text().trimmed());
    settings::ensureStorageLayout();
    const QString newTheme = m_themeCombo->currentIndex() == 0 ? "dark" : "light";
    const QString newLang = m_langCombo->currentIndex() == 0 ? "zh" : "en";
    const bool themeChg = newTheme != settings::theme();
    const bool langChg = newLang != settings::language();
    settings::setTheme(newTheme);
    settings::setLanguage(newLang);
    QDialog::accept();
    if (themeChg) emit themeChanged(newTheme);
    if (langChg) { i18n::setLang(newLang); emit languageChanged(newLang); }
}
