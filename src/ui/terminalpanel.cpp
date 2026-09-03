#include "terminalpanel.h"
#include "theme.h"
#include "i18n.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QLabel>
#include <QProcess>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QHostInfo>
#include <QStandardPaths>
#include <QScrollBar>
#include <QTextCursor>
#include <QTextCharFormat>
#include <QColor>

namespace {
// 去掉 ANSI 转义序列（bash 输出的颜色/光标控制）
QString stripAnsi(const QString &in) {
    static const QRegularExpression re("\x1B\\[[0-9;?]*[A-Za-z]|\x1B\\][^\x07]*\x07");
    return QString(in).remove(re);
}
} // namespace

// 成员版本：头文件里声明为 static 成员
QString TerminalPanel::stripAnsi(const QString &in) {
    return ::stripAnsi(in);
}

TerminalPanel::TerminalPanel(QWidget *parent) : QWidget(parent) {
    m_user = qEnvironmentVariable("USERNAME");
    if (m_user.isEmpty()) m_user = qEnvironmentVariable("USER");
    m_host = QHostInfo::localHostName();

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 4, 4, 4);
    layout->setSpacing(4);

    m_out = new QPlainTextEdit;
    m_out->setReadOnly(true);
    m_out->setFont(QFont("Consolas", 10));
    m_out->setMaximumBlockCount(2000);
    m_out->setStyleSheet(QString(
        "QPlainTextEdit{background-color:%1;color:%2;border:1px solid %3;border-radius:6px;"
        "font-family:'Consolas','Courier New',monospace;font-size:10pt;}")
        .arg(theme::bg(), theme::text(), theme::border()));
    layout->addWidget(m_out, 1);

    m_prompt = new QLabel;
    m_prompt->setFont(QFont("Consolas", 10));
    m_prompt->setStyleSheet(QStringLiteral("font-family:'Consolas','Courier New',monospace;"));
    layout->addWidget(m_prompt);

    m_in = new QLineEdit;
    m_in->setFont(QFont("Consolas", 10));
    m_in->setPlaceholderText(i18n::t("terminal_placeholder"));
    m_in->setStyleSheet(QString(
        "QLineEdit{background-color:%1;color:%2;border:1px solid %3;border-radius:6px;padding:4px 8px;"
        "font-family:'Consolas','Courier New',monospace;font-size:10pt;}")
        .arg(theme::bg(), theme::text(), theme::border()));
    connect(m_in, &QLineEdit::returnPressed, this, &TerminalPanel::runCommand);
    m_in->installEventFilter(this);
    layout->addWidget(m_in);

    updatePrompt();
}

TerminalPanel::~TerminalPanel() {
    // 面板销毁时终止在跑的命令，避免孤儿 bash/解释器进程残留
    if (m_proc) {
        m_proc->kill();
        m_proc->deleteLater();
        m_proc = nullptr;
    }
}

void TerminalPanel::retranslate() {
    m_in->setPlaceholderText(i18n::t("terminal_placeholder"));
}

void TerminalPanel::setGitPath(const QString &gitPath) {
    // 由 git.exe 路径推回 Git 安装根，再找 bash.exe（cmd/git.exe → 根/bin/bash.exe）
    QDir d = QFileInfo(gitPath).absolutePath();
    for (int i = 0; i < 4; ++i) {
        const QString cands[] = { d.path() + "/bin/bash.exe",
                                  d.path() + "/usr/bin/bash.exe",
                                  d.path() + "/mingw64/bin/bash.exe" };
        for (const QString &c : cands)
            if (QFileInfo::exists(c)) { m_bashPath = QDir(c).absolutePath(); return; }
        if (!d.cdUp()) break;
    }
    m_bashPath = QStringLiteral("bash");   // 交给 PATH 兜底
}

void TerminalPanel::setRepo(const QString &path) {
    if (path.isEmpty()) return;
    m_cwd = QDir(path).absolutePath().replace('/', '\\');
    updatePrompt();
}

void TerminalPanel::setBranch(const QString &branch) {
    m_branch = branch;
    updatePrompt();
}

void TerminalPanel::runCommandText(const QString &cmd) {
    if (cmd.trimmed().isEmpty()) return;
    m_in->setText(cmd);
    // 代码运行入口：先清屏，避免和旧输出混在一起；手动输入的命令不清屏
    m_out->clear();
    runCommand();
}

void TerminalPanel::updatePrompt() {
    // 简洁提示符：用户名> （与命令回显格式一致）
    m_promptPlain = m_user + "> ";
    const QString html = QStringLiteral(
        "<span style='color:#f9f1a5;font-weight:bold;'>%1</span><span style='color:%2;'>&gt;</span> ")
        .arg(m_user.toHtmlEscaped(), theme::textDim());
    m_prompt->setText(html);
}

bool TerminalPanel::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_in && e->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent *>(e);
        if (ke->key() == Qt::Key_Up) {
            if (m_history.isEmpty()) return true;
            if (m_histIdx < 0) m_histIdx = m_history.size() - 1;
            else if (m_histIdx > 0) --m_histIdx;
            m_in->setText(m_history.at(m_histIdx));
            return true;
        }
        if (ke->key() == Qt::Key_Down) {
            if (m_histIdx >= 0 && m_histIdx < m_history.size() - 1) {
                ++m_histIdx;
                m_in->setText(m_history.at(m_histIdx));
            } else {
                m_histIdx = -1;
                m_in->clear();
            }
            return true;
        }
    }
    return QWidget::eventFilter(obj, e);
}

void TerminalPanel::runCommand() {
    const QString cmd = m_in->text().trimmed();
    if (cmd.isEmpty()) return;
    m_history.append(cmd);
    m_histIdx = -1;
    m_in->clear();

    // 特殊命令
    if (cmd == QLatin1String("clear") || cmd == QLatin1String("cls")) {
        m_out->clear();
        return;
    }
    echoCommand(cmd);
    // 纯 cd 命令（不带引号组合：cd、cd ~、cd 路径）才走内置逻辑；
    // 带 && 等 bash 组合（如代码运行的 cd xxx && g++ ...）交给 bash 执行
    static const QRegularExpression isPlainCd("^cd(?:\\s+[^\\s&|;]+)?$");
    if (isPlainCd.match(cmd).hasMatch()) {
        QString target = cmd.mid(2).trimmed();
        if (target.startsWith('"') && target.endsWith('"') && target.size() >= 2)
            target = target.mid(1, target.size() - 2);
        QDir d(m_cwd);
        if (target.isEmpty() || target == QLatin1String("~")) {
            const QString home = QDir::homePath();
            m_cwd = QDir(home).absolutePath();
        } else if (!d.cd(target)) {
            appendOutput(QStringLiteral("bash: cd: %1: No such file or directory").arg(target));
            return;
        } else {
            m_cwd = d.absolutePath();
        }
        updatePrompt();
        return;
    }

    if (m_proc) { m_proc->kill(); m_proc->deleteLater(); }
    m_proc = new QProcess(this);
    m_proc->setWorkingDirectory(m_cwd);
    connect(m_proc, &QProcess::readyReadStandardOutput, this, &TerminalPanel::onOutput);
    connect(m_proc, &QProcess::readyReadStandardError, this, &TerminalPanel::onOutput);
    connect(m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus st) {
        if (st == QProcess::CrashExit)
            appendOutput("[" + i18n::t("proc_crashed") + "]");
        else if (code != 0)
            appendOutput("[" + i18n::t("exit_code").arg(code) + "]");
        m_proc->deleteLater();
        m_proc = nullptr;
    });
    // bash -c "<cmd>"：完整 bash 语义（管道/重定向/&&）
    m_proc->start(m_bashPath, { "-c", cmd });
}

void TerminalPanel::onOutput() {
    if (!m_proc) return;
    QString text = stripAnsi(QString::fromUtf8(m_proc->readAllStandardOutput()));
    const QString err = stripAnsi(QString::fromUtf8(m_proc->readAllStandardError()));
    if (!err.isEmpty()) text += err;
    appendOutput(text);
}

void TerminalPanel::echoCommand(const QString &cmd) {
    QTextCursor c = m_out->textCursor();
    c.movePosition(QTextCursor::End);
    if (!m_out->document()->isEmpty())
        c.insertText(QStringLiteral("\n"));

    QTextCharFormat userFmt;
    userFmt.setForeground(QColor("#f9f1a5"));
    userFmt.setFontWeight(QFont::Bold);
    c.insertText(m_user, userFmt);

    QTextCharFormat restFmt;
    restFmt.setForeground(QColor(theme::text()));
    restFmt.setFontWeight(QFont::Normal);
    c.insertText(QStringLiteral("> ") + cmd, restFmt);
    m_out->setTextCursor(c);

    QScrollBar *sb = m_out->verticalScrollBar();
    sb->setValue(sb->maximum());
}

void TerminalPanel::appendOutput(const QString &text) {
    m_out->appendPlainText(text);
    QScrollBar *sb = m_out->verticalScrollBar();
    sb->setValue(sb->maximum());
}
