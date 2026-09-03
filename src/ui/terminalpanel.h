#pragma once
#include <QWidget>

class QPlainTextEdit;
class QLineEdit;
class QLabel;
class QProcess;

// 内嵌 Git Bash 风格命令面板：mintty 式彩色提示符 + bash 执行 + 输出回显
class TerminalPanel : public QWidget {
    Q_OBJECT
public:
    explicit TerminalPanel(QWidget *parent = nullptr);

    void setGitPath(const QString &gitPath);
    void setRepo(const QString &path);      // 打开项目时同步工作目录
    void setBranch(const QString &branch);  // 分支变化时同步提示符
    void runCommandText(const QString &cmd); // 外部注入命令执行（编辑器"运行"）

private slots:
    void runCommand();
    void onOutput();

private:
    bool eventFilter(QObject *obj, QEvent *e) override;
    void updatePrompt();
    void echoCommand(const QString &cmd);
    void appendOutput(const QString &text);
    QString m_promptPlain;
    static QString stripAnsi(const QString &text);

    QPlainTextEdit *m_out = nullptr;
    QLineEdit *m_in = nullptr;
    QLabel *m_prompt = nullptr;
    QProcess *m_proc = nullptr;
    QString m_cwd, m_bashPath, m_user, m_host, m_branch;
    QStringList m_history;
    int m_histIdx = -1;
};
