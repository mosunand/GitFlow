#include "titlebar.h"
#include "theme.h"
#include "i18n.h"
#include "icons.h"
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QMouseEvent>
#include <QWindow>

TitleBar::TitleBar(QWidget *window, QWidget *parent)
    : QWidget(parent), m_window(window) {
    setFixedHeight(46);   // 双行账户区（账号 + Token 倒计时）需要更高的标题栏
    setObjectName(QStringLiteral("gitflowTitleBar"));
    setAttribute(Qt::WA_StyledBackground, true);

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSpacing(10);

    auto *logo = new QLabel;
    logo->setPixmap(icons::appIcon().pixmap(16, 16));
    layout->addWidget(logo);

    m_title = new QLabel("GitFlow");
    layout->addWidget(m_title);

    layout->addStretch(1);
    m_github = new QLabel;
    m_github->hide();
    layout->addWidget(m_github);
    layout->addStretch(1);

    const char *btnStyle =
        "QPushButton{background:transparent;border:none;color:%1;font-size:13px;padding:0 12px;}"
        "QPushButton:hover{background:%2;}";
    auto mkBtn = [&](const QString &text) {
        auto *b = new QPushButton(text);
        b->setFixedSize(40, 46);
        b->setFocusPolicy(Qt::NoFocus);   // 窗控按钮不抢焦点（避免虚线框）
        b->setStyleSheet(QString(btnStyle).arg(theme::textDim(), theme::bgHover()));
        return b;
    };
    m_minBtn = mkBtn(QStringLiteral("—"));
    connect(m_minBtn, &QPushButton::clicked, m_window, &QWidget::showMinimized);
    m_maxBtn = mkBtn(QStringLiteral("□"));
    connect(m_maxBtn, &QPushButton::clicked, this, &TitleBar::toggleMax);
    m_closeBtn = mkBtn(QStringLiteral("✕"));
    connect(m_closeBtn, &QPushButton::clicked, m_window, &QWidget::close);

    layout->addWidget(m_minBtn);
    layout->addWidget(m_maxBtn);
    layout->addWidget(m_closeBtn);

    applyTheme();
}

void TitleBar::applyTheme() {
    // 用选择器限定背景，避免裸声明级联到子控件（亮色主题下出现黑块）
    setStyleSheet(QString("#gitflowTitleBar{background-color:%1;}").arg(theme::bgElevated()));
    if (m_title)
        m_title->setStyleSheet(
            QString("color:%1; background:transparent; font-size:13px; font-weight:bold;")
                .arg(theme::text()));
    if (m_github)
        m_github->setStyleSheet(
            QString("color:%1; background:transparent; font-size:13px;").arg(theme::textMuted()));
    const QString btn = QString(
        "QPushButton{background:transparent;border:none;color:%1;font-size:13px;padding:0 12px;}"
        "QPushButton:hover{background:%2;}").arg(theme::textDim(), theme::bgHover());
    if (m_minBtn) m_minBtn->setStyleSheet(btn);
    if (m_maxBtn) m_maxBtn->setStyleSheet(btn);
    if (m_closeBtn)
        m_closeBtn->setStyleSheet(btn + "QPushButton:hover{background:#c62828;color:white;}");
}

void TitleBar::setTitle(const QString &text) { m_title->setText(text); }

void TitleBar::setGithubLabel(const QString &text) {
    m_githubText = text;
    updateGithubHtml();
}

void TitleBar::setGithubAccount(const QString &line1, const QString &expiry, bool urgent) {
    m_githubText = line1;
    m_expiryText = expiry;
    m_expiryUrgent = urgent;
    updateGithubHtml();
}

// 双行账户区：账号行 + 可选的 Token 剩余天数行（urgent 时红色加粗）
void TitleBar::updateGithubHtml() {
    if (m_githubText.isEmpty()) { m_github->hide(); return; }
    QString html = QStringLiteral(
        "<div style='font-size:13px;color:%1;'>%2</div>")
        .arg(theme::textMuted(), m_githubText.toHtmlEscaped());
    if (!m_expiryText.isEmpty())
        html += QStringLiteral(
            "<div style='font-size:10px;color:%1;font-weight:%2;'>%3</div>")
            .arg(m_expiryUrgent ? QStringLiteral("#e5534b") : theme::textMuted(),
                 m_expiryUrgent ? QStringLiteral("bold") : QStringLiteral("normal"),
                 m_expiryText.toHtmlEscaped());
    m_github->setText(html);
    m_github->show();
}

bool TitleBar::onInteractiveChild(const QPoint &pos) const {
    return childAt(pos) != nullptr;
}

// 边缘 6px 热区触发系统级调整窗口大小（与 Python 版一致）
bool TitleBar::tryStartResize(const QPoint &gp) {
    if (!m_window || m_window->isMaximized()) return false;
    QWindow *h = windowHandle();
    if (!h) return false;
    const QRect r = m_window->frameGeometry();
    constexpr int m = 6;
    const bool onL = gp.x() <= r.left() + m;
    const bool onR = gp.x() >= r.right() - m;
    const bool onT = gp.y() <= r.top() + m;
    const bool onB = gp.y() >= r.bottom() - m;
    if (!(onL || onR || onT || onB)) return false;
    Qt::Edges edges;
    if (onL) edges |= Qt::LeftEdge;
    if (onR) edges |= Qt::RightEdge;
    if (onT) edges |= Qt::TopEdge;
    if (onB) edges |= Qt::BottomEdge;
    h->startSystemResize(edges);
    return true;
}

void TitleBar::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton) {
        if (tryStartResize(e->globalPosition().toPoint()))
            return;
        if (!onInteractiveChild(e->position().toPoint())) {
            if (windowHandle()) windowHandle()->startSystemMove();
            return;
        }
    }
    QWidget::mousePressEvent(e);
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *e) {
    if (onInteractiveChild(e->position().toPoint())) return;
    toggleMax();
}

void TitleBar::toggleMax() {
    if (m_window->isMaximized()) {
        m_window->showNormal();
        m_maxBtn->setText(QStringLiteral("□"));
    } else {
        m_window->showMaximized();
        m_maxBtn->setText(QStringLiteral("❐"));
    }
}
