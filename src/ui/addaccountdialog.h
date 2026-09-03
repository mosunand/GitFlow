#pragma once
#include <QDialog>
#include <QJsonObject>

class QLabel;
class QLineEdit;
class QPushButton;
class QComboBox;

// 一页式添加账户：选平台 → 粘贴 Token（可选填有效期）→ 验证并保存为当前账户
class AddAccountDialog : public QDialog {
    Q_OBJECT
public:
    explicit AddAccountDialog(QWidget *parent = nullptr);

signals:
    void accountAdded();

private slots:
    void selectPlatform(const QString &platform);
    void toggleTokenVisible(bool on);
    void verifyAndAdd();
    void handleVerify(bool ok, const QJsonObject &obj, const QString &err);
    void onVerifyOk(const QString &login);
    void onVerifyFail(const QString &err);

private:
    QString m_platform = QStringLiteral("github");
    QLineEdit *m_token = nullptr;
    QComboBox *m_tokenDays = nullptr;   // 有效天数：可直接输入，含常用预设（空=不限）
    QPushButton *m_githubBtn = nullptr, *m_giteeBtn = nullptr;
    QPushButton *m_showBtn = nullptr, *m_verifyBtn = nullptr;
    QLabel *m_status = nullptr;
};
