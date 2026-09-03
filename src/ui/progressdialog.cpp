#include "progressdialog.h"
#include "i18n.h"
#include "theme.h"
#include <QLabel>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTimer>
#include <QDateTime>

ProgressDialog::ProgressDialog(const QString &action, QWidget *parent) : QDialog(parent) {
    setWindowTitle(action);
    setMinimumSize(560, 420);
    setModal(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 16);
    layout->setSpacing(12);

    auto *top = new QHBoxLayout;
    m_title = new QLabel("⏳ " + i18n::t("pushing"));
    m_title->setStyleSheet(
        QString("font-size:18px;font-weight:bold;color:%1;").arg(theme::text()));
    top->addWidget(m_title, 1);
    m_percent = new QLabel("0%");
    m_percent->setStyleSheet(
        QString("font-size:26px;font-weight:bold;color:%1;").arg(theme::accent()));
    top->addWidget(m_percent);
    layout->addLayout(top);

    m_stage = new QLabel;
    m_stage->setStyleSheet(QString("color:%1;font-size:12px;").arg(theme::textMuted()));
    m_stage->setWordWrap(true);
    layout->addWidget(m_stage);

    m_bar = new QProgressBar;
    m_bar->setRange(0, 100);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(10);
    m_bar->setStyleSheet(QString(
        "QProgressBar{background:%1;border:none;border-radius:5px;}"
        "QProgressBar::chunk{background:%2;border-radius:5px;}").arg(theme::bgTab(), theme::accent()));
    layout->addWidget(m_bar);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(300);
    m_log->setStyleSheet(QString(
        "QPlainTextEdit{background:%1;color:%2;border:1px solid %3;border-radius:8px;"
        "padding:6px;font-family:Consolas,monospace;font-size:12px;}")
        .arg(theme::bgInput(), theme::textDim(), theme::border()));
    layout->addWidget(m_log, 1);

    m_stuck = new QLabel;
    m_stuck->setStyleSheet(
        QString("color:%1;font-size:12px;font-style:italic;").arg(theme::textMuted()));
    m_stuck->setWordWrap(true);
    m_stuck->hide();
    layout->addWidget(m_stuck);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    m_hideBtn = new QPushButton(i18n::t("run_bg"));
    connect(m_hideBtn, &QPushButton::clicked, this, &QDialog::hide);
    btnRow->addWidget(m_hideBtn);
    layout->addLayout(btnRow);

    m_lastOutput = QDateTime::currentMSecsSinceEpoch();
    m_stuckTimer = new QTimer(this);
    m_stuckTimer->setInterval(3000);
    connect(m_stuckTimer, &QTimer::timeout, this, [this] {
        if (m_done) return;
        const qint64 idle = QDateTime::currentMSecsSinceEpoch() - m_lastOutput;
        if (idle > 10000) {
            m_stuck->setText("⚠️ " + i18n::t("stuck_hint") +
                             QStringLiteral(" (%1s)").arg(idle / 1000));
            m_stuck->show();
        }
    });
    m_stuckTimer->start();
}

void ProgressDialog::appendLine(const QString &line) {
    QMetaObject::invokeMethod(this, [this, line] { onLine(line); }, Qt::QueuedConnection);
}

void ProgressDialog::onLine(const QString &line) {
    m_lastOutput = QDateTime::currentMSecsSinceEpoch();
    m_stuck->hide();
    const QString clean = line.trimmed();
    if (clean.isEmpty()) return;
    m_log->appendPlainText(clean);

    int pct = m_bar->value();
    if (clean.contains("Counting objects")) {
        pct = qMax(pct, 10);
        m_stage->setText(i18n::t("stage_counting"));
    } else if (clean.contains("Compressing objects")) {
        pct = qMax(pct, 30);
        m_stage->setText(i18n::t("stage_compressing"));
    } else if (clean.contains("Writing objects")) {
        m_stage->setText(i18n::t("stage_writing"));
        bool ok = false;
        const int inner = clean.section('%', 0, 0).section(' ', -1).toInt(&ok);
        if (ok) pct = qMax(pct, 40 + inner / 2);
    } else if (clean.contains("Receiving objects") || clean.contains("Resolving deltas")) {
        m_stage->setText(i18n::t("stage_receiving"));
        bool ok = false;
        const int inner = clean.section('%', 0, 0).section(' ', -1).toInt(&ok);
        if (ok) pct = qMax(pct, 80 + inner / 5);
    } else if (clean.contains("total", Qt::CaseInsensitive)) {
        m_stage->setText(i18n::t("stage_final"));
        pct = qMax(pct, 95);
    }
    m_bar->setValue(qMin(pct, 99));
    m_percent->setText(QStringLiteral("%1%").arg(m_bar->value()));
}

void ProgressDialog::finishOk(const QString &msg) {
    m_done = true;
    m_stuckTimer->stop();
    m_bar->setValue(100);
    m_percent->setText("100%");
    m_title->setText("✅ " + i18n::t("push_success"));
    if (!msg.isEmpty()) m_log->appendPlainText(msg);
    m_hideBtn->setText(i18n::t("close"));
}

void ProgressDialog::finishFail(const QString &err, const QString &netDiag) {
    m_done = true;
    m_stuckTimer->stop();
    m_title->setText("❌ " + i18n::t("push_failed"));
    m_stage->setText(err.left(200));
    m_log->appendPlainText(err);
    if (!netDiag.isEmpty()) m_log->appendPlainText("\n" + netDiag);
    m_hideBtn->setText(i18n::t("close"));
}

void ProgressDialog::closeEvent(QCloseEvent *e) {
    // 允许关闭窗口（后台继续），不退出进程
    QDialog::closeEvent(e);
}
