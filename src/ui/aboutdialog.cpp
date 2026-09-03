#include "aboutdialog.h"
#include "theme.h"
#include "i18n.h"
#include "icons.h"
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QDesktopServices>
#include <QUrl>

namespace {
constexpr char kEmail[] = "moshuai1013@outlook.com";
constexpr char kRepo[] = "https://github.com/mosunand/GitFlow";
constexpr char kVersion[] = "v1.0.0";
} // namespace

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("about_title"));
    setMinimumSize(720, 640);
    resize(760, 680);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(28, 24, 28, 20);
    layout->setSpacing(14);

    auto *head = new QHBoxLayout;
    auto *logo = new QLabel;
    logo->setPixmap(icons::appIcon().pixmap(56, 56));
    head->addWidget(logo);
    auto *nameCol = new QVBoxLayout;
    auto *name = new QLabel("GitFlow");
    name->setStyleSheet(
        QString("font-size:30px;font-weight:800;color:%1;").arg(theme::text()));
    auto *slogan = new QLabel(
        QStringLiteral("让版本控制回归简单 · 一个以 GitHub / Gitee 为核心的桌面 Git 客户端"));
    slogan->setStyleSheet(QString("font-size:13px;color:%1;").arg(theme::textMuted()));
    nameCol->addWidget(name);
    nameCol->addWidget(slogan);
    head->addLayout(nameCol, 1);
    auto *ver = new QLabel(kVersion);
    ver->setStyleSheet(QString(
        "font-size:12px;color:%1;border:1px solid %1;border-radius:10px;padding:3px 12px;")
        .arg(theme::accent()));
    head->addWidget(ver);
    layout->addLayout(head);

    auto *line = new QLabel;
    line->setFixedHeight(1);
    line->setStyleSheet(QString("background-color:%1;border:none;").arg(theme::border()));
    layout->addWidget(line);

    const QString sec = QString(
        "font-size:14px;font-weight:bold;color:%1;margin:6px 0 4px 0;").arg(theme::text());
    const QString item = QString("color:%1;").arg(theme::textDim());
    const QString dim = QString("color:%1;").arg(theme::textMuted());
    const QString acc = QString("color:%1;font-weight:bold;").arg(theme::accent());

    auto *body = new QLabel(QStringLiteral(
        "<div>"
        "<div style='%1'>✨ 它能做什么</div>"
        "<div style='%2'>"
        "· 一站式管理本地仓库：文件浏览、代码编辑（语法高亮/行号/查找）、图片预览<br>"
        "· 完整 Git 工作流：暂存/提交/推送/拉取/分支/标签/Stash/回档<br>"
        "· 可视化推送进度：传输百分比、实时日志、卡住检测，失败时自动网络诊断<br>"
        "· 多平台多账户：GitHub 与 Gitee 双支持，Token 加密存储，一键切换身份<br>"
        "· 仓库发现：按用户名或完整 URL 搜索仓库，克隆、Fork、浏览器直达<br>"
        "· Release 发布：正式版/预发布，附件直传<br>"
        "· 100MB 大文件预检：推送前自动扫描所有超限文件，避免上传失败<br>"
        "· 深色/浅色主题，中英文界面"
        "</div>"
        "<div style='%1'>⚠️ 美中不足（请知悉）</div>"
        "<div style='%2'>"
        "· <b>删除远程仓库、修改仓库公开/私有</b>等敏感操作，请前往 GitHub / Gitee 网页端进行<br>"
        "· 单个文件超过 100MB 将无法推送（平台限制），建议使用 Git LFS 或删除后再推送<br>"
        "· 冲突解决目前仅支持命令行辅助，可视化合并工具还在路上<br>"
        "· 网络环境波动可能影响连接；软件已自动适配系统代理，如仍失败请检查代理软件"
        "</div>"
        "<div style='%1'>🛠️ 技术栈</div>"
        "<div style='%3'>C++ / Qt 6 · Git CLI · GitHub &amp; Gitee REST API · Windows CNG AES-256-GCM</div>"
        "<div style='%1'>👤 作者与开源</div>"
        "<div style='%2'>"
        "作者：<b>mosunand</b><br>"
        "邮箱：<a href='mailto:%4' style='%5'>%4</a><br>"
        "开源仓库：<a href='%6' style='%5'>%6</a><br>"
        "<span style='%3'>本项目完全开源，欢迎 Star、Issue 与 PR。你的反馈是它变得更好的动力。</span>"
        "</div>"
        "<div style='%1'>📜 声明</div>"
        "<div style='%3'>本软件按“现状”提供，使用前请自行备份重要数据。Token 使用账户独立"
        "随机密钥 AES-256-GCM 加密存储于本地，不会上传到任何第三方服务器。</div>"
        "</div>").arg(sec, item, dim, kEmail, acc, kRepo));
    body->setTextFormat(Qt::RichText);
    body->setWordWrap(true);
    body->setOpenExternalLinks(true);
    body->setAlignment(Qt::AlignTop);
    layout->addWidget(body, 1);

    auto *btnRow = new QHBoxLayout;
    auto *repoBtn = new QPushButton(i18n::t("about_btn_repo"));
    repoBtn->setCursor(Qt::PointingHandCursor);
    repoBtn->setStyleSheet(QString(
        "QPushButton{background:%1;color:white;font-weight:bold;border:none;"
        "border-radius:8px;padding:8px 18px;font-size:13px;}"
        "QPushButton:hover{background:%2;}").arg(theme::accent(), theme::accentHover()));
    connect(repoBtn, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl(kRepo)); });
    btnRow->addWidget(repoBtn);

    auto *mailBtn = new QPushButton(i18n::t("about_btn_mail"));
    mailBtn->setCursor(Qt::PointingHandCursor);
    mailBtn->setStyleSheet(QString(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:8px;"
        "padding:8px 18px;font-size:13px;}QPushButton:hover{background:%4;}")
        .arg(theme::bgButton(), theme::textDim(), theme::borderLight(), theme::bgHover()));
    connect(mailBtn, &QPushButton::clicked, this,
            [] { QDesktopServices::openUrl(QUrl(QStringLiteral("mailto:") + kEmail)); });
    btnRow->addWidget(mailBtn);
    btnRow->addStretch(1);

    auto *closeBtn = new QPushButton(i18n::t("close"));
    closeBtn->setFixedWidth(100);
    closeBtn->setStyleSheet(QString(
        "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:8px;"
        "padding:8px 18px;font-size:13px;}QPushButton:hover{background:%4;}")
        .arg(theme::bgButton(), theme::textDim(), theme::borderLight(), theme::bgHover()));
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}
