#pragma once
#include <QDialog>

class QListWidget;
class QLineEdit;
class QComboBox;
class QLabel;

// 账户与 Git 设置：Git 路径 / 文件位置 / 外观语言 / 账户 / 提交作者
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    void accept() override;

signals:
    void themeChanged(const QString &theme);
    void languageChanged(const QString &lang);
    void accountsChanged();

private slots:
    void detectGit();
    void browseGit();
    void browseStorage();
    void resetStorage();
    void saveAuthor();
    void addAccount();
    void deleteAccount();
    void setCurrentAccount();
    void testConnection();

private:
    void applyThemeLang();
    void loadAccounts();

    QLineEdit *m_gitPathEdit = nullptr, *m_storageEdit = nullptr;
    QLineEdit *m_authorName = nullptr, *m_authorEmail = nullptr;
    QComboBox *m_themeCombo = nullptr, *m_langCombo = nullptr;
    QListWidget *m_accountList = nullptr;
    QLabel *m_gitCurrent = nullptr, *m_gitStatus = nullptr, *m_status = nullptr;
};
