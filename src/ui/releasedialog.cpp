#include "releasedialog.h"
#include "i18n.h"
#include "theme.h"
#include "services/accountservice.h"
#include "services/githubservice.h"
#include "services/giteeservice.h"
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QRadioButton>
#include <QButtonGroup>
#include <QListWidget>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QFileInfo>
#include <QPointer>
#include <QDesktopServices>
#include <QUrl>
#include <QJsonObject>

namespace {
AccountService *acct() { static AccountService s; return &s; }

} // namespace

ReleaseDialog::ReleaseDialog(const QString &owner, const QString &repo,
                             const QString &platform, QWidget *parent)
    : QDialog(parent), m_owner(owner), m_repo(repo), m_platform(platform) {
    setWindowTitle(i18n::t("create_release"));
    setMinimumSize(720, 640);
    resize(760, 680);

    m_gh = acct()->github();
    m_gitee = acct()->gitee();
    m_gh->setParent(this);
    m_gitee->setParent(this);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 20, 24, 16);
    layout->setSpacing(12);

    auto *head = new QLabel(i18n::t("create_release"));
    head->setStyleSheet(QString("font-size:20px;font-weight:800;color:%1;").arg(theme::text()));
    layout->addWidget(head);

    auto *repoRow = new QHBoxLayout;
    auto *repoCap = new QLabel(i18n::t("release_repo") + ":");
    repoCap->setStyleSheet(QString("color:%1;").arg(theme::textMuted()));
    auto *repoVal = new QLabel(QStringLiteral("%1/%2").arg(owner, repo));
    repoVal->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::accent()));
    repoRow->addWidget(repoCap);
    repoRow->addWidget(repoVal, 1);
    layout->addLayout(repoRow);

    auto *typeCap = new QLabel(i18n::t("release_type"));
    typeCap->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(typeCap);
    auto *typeRow = new QHBoxLayout;
    m_stableRadio = new QRadioButton(i18n::t("release_stable"));
    m_preRadio = new QRadioButton(i18n::t("release_prerelease"));
    m_stableRadio->setChecked(true);
    auto *grp = new QButtonGroup(this);
    grp->addButton(m_stableRadio);
    grp->addButton(m_preRadio);
    typeRow->addWidget(m_stableRadio);
    typeRow->addWidget(m_preRadio);
    typeRow->addStretch(1);
    layout->addLayout(typeRow);

    auto *tagCap = new QLabel(i18n::t("tag_name"));
    tagCap->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(tagCap);
    m_tagEdit = new QLineEdit;
    m_tagEdit->setPlaceholderText(QStringLiteral("v1.0.0"));
    m_tagEdit->setText(QStringLiteral("v1.0.0"));
    m_tagEdit->setMinimumHeight(32);
    layout->addWidget(m_tagEdit);

    auto *titleCap = new QLabel(i18n::t("release_title_l"));
    titleCap->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(titleCap);
    m_titleEdit = new QLineEdit;
    m_titleEdit->setPlaceholderText(i18n::t("release_title_l"));
    m_titleEdit->setText(QStringLiteral("v1.0.0"));
    m_titleEdit->setMinimumHeight(32);
    layout->addWidget(m_titleEdit);
    connect(m_tagEdit, &QLineEdit::textChanged, this, [this](const QString &t) {
        // 标题未手动改过时，跟随标签
        static QString lastSynced = QStringLiteral("v1.0.0");
        if (m_titleEdit->text() == lastSynced) {
            m_titleEdit->setText(t);
            lastSynced = t;
        }
    });

    auto *bodyCap = new QLabel(i18n::t("release_body_l"));
    bodyCap->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(bodyCap);
    m_bodyEdit = new QPlainTextEdit;
    m_bodyEdit->setPlaceholderText(i18n::t("release_body_ph"));
    m_bodyEdit->setMinimumHeight(160);
    layout->addWidget(m_bodyEdit, 1);

    auto *assetCap = new QLabel(i18n::t("upload_assets"));
    assetCap->setStyleSheet(QString("font-weight:bold;color:%1;").arg(theme::text()));
    layout->addWidget(assetCap);
    m_assetList = new QListWidget;
    m_assetList->setMinimumHeight(90);
    m_assetList->setMaximumHeight(140);
    layout->addWidget(m_assetList);
    auto *assetRow = new QHBoxLayout;
    auto *addBtn = new QPushButton(i18n::t("choose_files"));
    connect(addBtn, &QPushButton::clicked, this, &ReleaseDialog::chooseAssets);
    auto *rmBtn = new QPushButton(i18n::t("delete_sel"));
    connect(rmBtn, &QPushButton::clicked, this, &ReleaseDialog::removeSelectedAsset);
    assetRow->addWidget(addBtn);
    assetRow->addWidget(rmBtn);
    assetRow->addStretch(1);
    layout->addLayout(assetRow);

    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setStyleSheet(QString("color:%1;").arg(theme::textMuted()));
    layout->addWidget(m_status);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    m_cancelBtn = new QPushButton(i18n::t("cancel"));
    connect(m_cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    m_publishBtn = new QPushButton(i18n::t("create_release"));
    m_publishBtn->setDefault(true);
    connect(m_publishBtn, &QPushButton::clicked, this, &ReleaseDialog::publish);
    btnRow->addWidget(m_cancelBtn);
    btnRow->addWidget(m_publishBtn);
    layout->addLayout(btnRow);
}

void ReleaseDialog::chooseAssets() {
    const QStringList files = QFileDialog::getOpenFileNames(this, i18n::t("choose_assets"));
    for (const QString &f : files) {
        bool exists = false;
        for (int i = 0; i < m_assetList->count(); ++i)
            if (m_assetList->item(i)->data(Qt::UserRole).toString() == f) { exists = true; break; }
        if (exists) continue;
        const QFileInfo fi(f);
        auto *it = new QListWidgetItem(QStringLiteral("%1  (%2 KB)").arg(fi.fileName()).arg(fi.size() / 1024));
        it->setData(Qt::UserRole, f);
        m_assetList->addItem(it);
    }
}

void ReleaseDialog::removeSelectedAsset() {
    const auto items = m_assetList->selectedItems();
    for (auto *it : items)
        delete m_assetList->takeItem(m_assetList->row(it));
}

void ReleaseDialog::setBusy(bool on) {
    m_publishBtn->setEnabled(!on);
    m_cancelBtn->setEnabled(!on);
    m_tagEdit->setEnabled(!on);
    m_titleEdit->setEnabled(!on);
    m_bodyEdit->setEnabled(!on);
    m_stableRadio->setEnabled(!on);
    m_preRadio->setEnabled(!on);
    m_assetList->setEnabled(!on);
}

void ReleaseDialog::publish() {
    const QString tag = m_tagEdit->text().trimmed();
    if (tag.isEmpty()) {
        m_status->setText("❌ " + i18n::t("enter_tag"));
        m_status->setStyleSheet(QStringLiteral("color:#f85149;"));
        return;
    }
    QString title = m_titleEdit->text().trimmed();
    if (title.isEmpty()) title = tag;
    const QString body = m_bodyEdit->toPlainText();
    const bool prerelease = m_preRadio->isChecked();

    m_pendingAssets.clear();
    for (int i = 0; i < m_assetList->count(); ++i)
        m_pendingAssets << m_assetList->item(i)->data(Qt::UserRole).toString();
    m_uploadIdx = 0;

    setBusy(true);
    m_status->setText(i18n::t("creating"));
    m_status->setStyleSheet(QString("color:%1;").arg(theme::accent()));

    QPointer<ReleaseDialog> self(this);
    auto done = [self](bool ok, const QJsonArray &, const QJsonObject &obj, const QString &err) {
        if (!self) return;
        if (!ok) {
            self->finishErr(err);
            return;
        }
        const qint64 id = obj.value("id").toVariant().toLongLong();
        const QString url = obj.value("html_url").toString();
        if (self->m_pendingAssets.isEmpty())
            self->finishOk(url);
        else
            self->uploadNext(id, url);
    };

    if (m_platform == QLatin1String("gitee"))
        m_gitee->createRelease(m_owner, m_repo, tag, title, body, prerelease, done);
    else
        m_gh->createRelease(m_owner, m_repo, tag, title, body, prerelease, done);
}

void ReleaseDialog::uploadNext(qint64 releaseId, const QString &htmlUrl) {
    if (m_uploadIdx >= m_pendingAssets.size()) {
        finishOk(htmlUrl);
        return;
    }
    const QString path = m_pendingAssets.at(m_uploadIdx);
    m_status->setText(i18n::t("creating") + "  " + QFileInfo(path).fileName()
                      + QStringLiteral("  (%1/%2)").arg(m_uploadIdx + 1).arg(m_pendingAssets.size()));

    QPointer<ReleaseDialog> self(this);
    auto cb = [self, releaseId, htmlUrl](bool ok, const QJsonArray &, const QJsonObject &, const QString &err) {
        if (!self) return;
        if (!ok) {
            self->finishErr(err);
            return;
        }
        ++self->m_uploadIdx;
        self->uploadNext(releaseId, htmlUrl);
    };
    if (m_platform == QLatin1String("gitee"))
        m_gitee->uploadAsset(m_owner, m_repo, releaseId, path, cb);
    else
        m_gh->uploadAsset(m_owner, m_repo, releaseId, path, cb);
}

void ReleaseDialog::finishOk(const QString &htmlUrl) {
    setBusy(false);
    m_status->setText("✅ " + i18n::t("release_created") + "  " + htmlUrl);
    m_status->setStyleSheet(QStringLiteral("color:#2ea043;"));
    if (!htmlUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(htmlUrl));
    accept();
}

void ReleaseDialog::finishErr(const QString &err) {
    setBusy(false);
    QString msg = err;
    if (err.contains(QLatin1String("already_exists"), Qt::CaseInsensitive)
        || err.contains(QLatin1String("already exists"), Qt::CaseInsensitive))
        msg = i18n::t("tag_exists_msg").arg(m_tagEdit->text().trimmed());
    else if (err.contains(QLatin1String("Not Found"), Qt::CaseInsensitive)
             || err.contains(QStringLiteral("404")))
        msg = i18n::t("repo_404_msg");
    m_status->setText("❌ " + i18n::t("release_failed") + ": " + msg);
    m_status->setStyleSheet(QStringLiteral("color:#f85149;"));
}
