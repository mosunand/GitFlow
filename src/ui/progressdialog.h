#pragma once
#include <QDialog>

class QLabel;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;

// 推送/拉取进度弹窗：大百分比 + 阶段 + 实时日志 + 卡住检测
class ProgressDialog : public QDialog {
    Q_OBJECT
public:
    explicit ProgressDialog(const QString &action, QWidget *parent = nullptr);

public slots:
    void appendLine(const QString &line);   // 后台线程经队列信号转发
    void finishOk(const QString &msg);
    void finishFail(const QString &err, const QString &netDiag);

protected:
    void closeEvent(QCloseEvent *e) override;

signals:
    void lineReceived(const QString &line); // 内部跨线程转发

private:
    void onLine(const QString &line);
    void checkStuck();

    QLabel *m_title = nullptr, *m_stage = nullptr, *m_percent = nullptr, *m_stuck = nullptr;
    QProgressBar *m_bar = nullptr;
    QPlainTextEdit *m_log = nullptr;
    QPushButton *m_hideBtn = nullptr;
    qint64 m_lastOutput = 0;
    bool m_done = false;
    QTimer *m_stuckTimer = nullptr;
};
