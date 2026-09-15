#pragma once
#include <QDialog>
#include <QJsonArray>

class QListWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class GitHubService;
class GiteeService;

// 仓库面板：我的仓库 / 搜索（URL 或 owner/repo）/ 克隆 / Fork / 创建仓库 / 发布
class RepoPanelDialog : public QDialog {
    Q_OBJECT
public:
    explicit RepoPanelDialog(QWidget *parent = nullptr);

signals:
    void repoCloned(const QString &path);   // 克隆成功，请求主窗口打开

private slots:
    void refreshMyRepos();
    void doSearch();
    void onSearchDone(bool ok, const QJsonArray &arr, const QString &err);
    void cloneSelected();
    void forkSelected();
    void openInBrowser();
    void createRepo();
    void deleteRepo();
    void createRelease();

private:
    void startClone(const QString &url, const QString &name);
    // 删除仓库：先过硬条件（本地副本除 .git 外必须为空），再确认、再调 API
    void confirmAndDelete(const QString &owner, const QString &repo, const QString &full);
    void checkRemoteEmptyThenDelete(const QString &owner, const QString &repo, const QString &full);

    QListWidget *m_repoList = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cloneBtn = nullptr, *m_forkBtn = nullptr, *m_createBtn = nullptr, *m_deleteBtn = nullptr, *m_releaseBtn = nullptr, *m_openBtn = nullptr, *m_refreshBtn = nullptr;

    // 创建仓库 / Fork 成功后会主动刷新"我的仓库"，这两个成员用于在刷新结果里
    // 定位并选中刚操作的那一项，并把操作结果文案留到刷新完成后显示
    QString m_pendingSelect;
    QString m_pendingMsg;

    // API 服务由对话框持有：随对话框销毁自动取消在途请求，避免回调访问已析构的 this
    GitHubService *m_gh = nullptr;
    GiteeService *m_gitee = nullptr;
};
