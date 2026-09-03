#pragma once
#include <QWidget>

class QLabel;
class QPushButton;

// 无边框自定义标题栏：logo + 标题 + github 标签 + 最小化/最大化/关闭
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *window, QWidget *parent = nullptr);
    void setTitle(const QString &text);
    void setGithubLabel(const QString &text);
    // 双行账户区：第一行 host/username，第二行 Token 剩余天数（urgent=红色加粗）
    void setGithubAccount(const QString &line1, const QString &expiry, bool urgent);
    void applyTheme();

signals:
    void githubClicked();

protected:
    void mousePressEvent(QMouseEvent *e) override;
    void mouseDoubleClickEvent(QMouseEvent *e) override;

private:
    bool onInteractiveChild(const QPoint &pos) const;
    bool tryStartResize(const QPoint &globalPos);
    void toggleMax();
    void updateGithubHtml();

    QWidget *m_window = nullptr;
    QLabel *m_title = nullptr, *m_github = nullptr;
    QPushButton *m_minBtn = nullptr, *m_maxBtn = nullptr, *m_closeBtn = nullptr;
    QString m_githubText, m_expiryText;
    bool m_expiryUrgent = false;
};
