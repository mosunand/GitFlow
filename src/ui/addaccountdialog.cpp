#include "addaccountdialog.h"
#include "i18n.h"
#include "theme.h"
#include "services/accountservice.h"
#include "services/githubservice.h"
#include "services/giteeservice.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QMessageBox>
#include <QTimer>
#include <QJsonObject>
#include <QJsonArray>
#include <QComboBox>
#include <QLineEdit>
#include <QDate>

namespace {
AccountService *acct() {
    static AccountService s;
    return &s;
}
} // namespace

AddAccountDialog::AddAccountDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("add_account"));
    resize(520, 380);

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(16, 16, 16, 16);
    root->setSpacing(12);

    // ── 平台 ──
    auto *pl = new QLabel(i18n::t("platform"));
    pl->setStyleSheet("font-weight:bold;");
    root->addWidget(pl);

    m_githubBtn = new QPushButton("\U0001F419  GitHub");
    m_giteeBtn = new QPushButton("\U0001F7E0  Gitee");
    for (auto *b : { m_githubBtn, m_giteeBtn }) {
        b->setCheckable(true);
        b->setMinimumHeight(38);
    }
    connect(m_githubBtn, &QPushButton::clicked, this, [this] { selectPlatform(QStringLiteral("github")); });
    connect(m_giteeBtn, &QPushButton::clicked, this, [this] { selectPlatform(QStringLiteral("gitee")); });
    auto *prow = new QHBoxLayout;
    prow->setSpacing(10);
    prow->addWidget(m_githubBtn, 1);
    prow->addWidget(m_giteeBtn, 1);
    root->addLayout(prow);

    // ── Token ──
    auto *tl = new QLabel(QStringLiteral("Access Token"));
    tl->setStyleSheet("font-weight:bold;");
    root->addWidget(tl);

    auto *tip = new QLabel(i18n::t("token_tip"));
    tip->setStyleSheet(QString("color:%1; font-size:11px;").arg(theme::textMuted()));
    tip->setWordWrap(true);
    root->addWidget(tip);

    m_token = new QLineEdit;
    m_token->setEchoMode(QLineEdit::Password);
    m_token->setMinimumHeight(32);
    m_showBtn = new QPushButton(i18n::t("show"));
    m_showBtn->setCheckable(true);
    connect(m_showBtn, &QPushButton::toggled, this, &AddAccountDialog::toggleTokenVisible);
    auto *trow = new QHBoxLayout;
    trow->setSpacing(6);
    trow->addWidget(m_token, 1);
    trow->addWidget(m_showBtn);
    root->addLayout(trow);

    // ── Token 有效期（可选）──
    auto *vl = new QLabel(i18n::t("token_validity_label"));
    vl->setStyleSheet(QString("color:%1; font-size:11px;").arg(theme::textMuted()));
    root->addWidget(vl);
    // 可直接输入数字，也可下拉选常用天数；留空 = 不限
    m_tokenDays = new QComboBox;
    m_tokenDays->setEditable(true);
    m_tokenDays->addItems({ QStringLiteral("30"), QStringLiteral("60"), QStringLiteral("90"),
                            QStringLiteral("180"), QStringLiteral("365"),
                            QStringLiteral("730"), QStringLiteral("3650") });
    m_tokenDays->lineEdit()->setPlaceholderText(i18n::t("validity_unlimited"));
    m_tokenDays->setMinimumHeight(32);
    m_tokenDays->setToolTip(i18n::t("token_validity_tip"));
    root->addWidget(m_tokenDays);

    // ── 按钮 / 状态 ──
    m_verifyBtn = new QPushButton(i18n::t("verify_add"));
    m_verifyBtn->setMinimumHeight(34);
    m_verifyBtn->setStyleSheet("font-weight:bold;");
    connect(m_verifyBtn, &QPushButton::clicked, this, &AddAccountDialog::verifyAndAdd);
    auto *cancel = new QPushButton(i18n::t("cancel"));
    cancel->setMinimumHeight(34);
    connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
    auto *brow = new QHBoxLayout;
    brow->addWidget(m_verifyBtn, 1);
    brow->addWidget(cancel);
    root->addLayout(brow);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setMinimumHeight(24);
    root->addWidget(m_status);

    root->addStretch();
    selectPlatform(QStringLiteral("github"));
}

void AddAccountDialog::selectPlatform(const QString &p) {
    m_platform = p;
    const bool gh = p == QLatin1String("github");
    m_githubBtn->setChecked(gh);
    m_giteeBtn->setChecked(!gh);
    m_githubBtn->setStyleSheet(gh
        ? QStringLiteral("background-color:#238636;color:white;font-weight:bold;")
        : QStringLiteral("background:transparent;color:%1;").arg(theme::textDim()));
    m_giteeBtn->setStyleSheet(!gh
        ? QStringLiteral("background-color:#c71d23;color:white;font-weight:bold;")
        : QStringLiteral("background:transparent;color:%1;").arg(theme::textDim()));
    m_token->setPlaceholderText(i18n::t(gh ? "token_ph_github" : "token_ph_gitee"));
}

void AddAccountDialog::toggleTokenVisible(bool on) {
    m_token->setEchoMode(on ? QLineEdit::Normal : QLineEdit::Password);
    m_showBtn->setText(i18n::t(on ? "hide" : "show"));
}

void AddAccountDialog::verifyAndAdd() {
    const QString token = m_token->text().trimmed();
    if (token.isEmpty()) {
        QMessageBox::warning(this, i18n::t("hint"), i18n::t("enter_token"));
        return;
    }
    m_status->setText(i18n::t("verifying"));
    m_verifyBtn->setEnabled(false);
    // 服务无父对象，回调里 deleteLater 回收；verifyUser 在具体服务类上
    if (m_platform == QLatin1String("gitee")) {
        auto *svc = new GiteeService(token);
        svc->verifyUser([this, svc](bool ok, const QJsonArray &, const QJsonObject &obj, const QString &err) {
            svc->deleteLater();
            handleVerify(ok, obj, err);
        });
    } else {
        auto *svc = new GitHubService(token);
        svc->verifyUser([this, svc](bool ok, const QJsonArray &, const QJsonObject &obj, const QString &err) {
            svc->deleteLater();
            handleVerify(ok, obj, err);
        });
    }
}

void AddAccountDialog::handleVerify(bool ok, const QJsonObject &obj, const QString &err) {
    if (!ok) { onVerifyFail(err); return; }
    QString login = obj.value("login").toString();
    if (login.isEmpty()) login = obj.value("username").toString();
    if (login.isEmpty()) { onVerifyFail(QStringLiteral("no login in response")); return; }
    onVerifyOk(login);
}

void AddAccountDialog::onVerifyOk(const QString &login) {
    const QString platform = m_platform;
    const QString token = m_token->text().trimmed();
    if (acct()->hasAccount(login, platform)) {
        const QString name = platform == QLatin1String("github") ? QStringLiteral("GitHub") : QStringLiteral("Gitee");
        if (QMessageBox::question(this, i18n::t("acct_exists"),
                                  i18n::t("acct_exists_body").arg(login, name)) != QMessageBox::Yes) {
            m_verifyBtn->setEnabled(true);
            m_status->clear();
            return;
        }
    }
    Account a;
    a.username = login;
    a.token = token;
    a.platform = platform;
    // 有效期（可选）：输入框直接敲数字（1~3650），换算成绝对过期日期供标题栏倒计时
    if (m_tokenDays) {
        bool ok = false;
        const int days = m_tokenDays->currentText().trimmed().toInt(&ok);
        if (ok && days > 0 && days <= 3650)
            a.expiresAt = QDate::currentDate().addDays(days).toString(Qt::ISODate);
    }
    acct()->saveAccount(a);
    acct()->setCurrent(login, platform);
    m_status->setText(QStringLiteral("✅ ") + i18n::t("verify_success_body") + "\n   " +
                      i18n::t("username_label") + ": " + login + "\n   " +
                      i18n::t("platform_label") + ": " +
                      (platform == QLatin1String("github") ? QStringLiteral("GitHub") : QStringLiteral("Gitee")) +
                      "\n   " + i18n::t("saved_as_current"));
    emit accountAdded();
    // 让用户看到成功详情后再关闭
    QTimer::singleShot(800, this, &QDialog::accept);
}

void AddAccountDialog::onVerifyFail(const QString &err) {
    m_status->setText("❌ " + i18n::t("verify_failed") + ": " + err);
    m_verifyBtn->setEnabled(true);
}
