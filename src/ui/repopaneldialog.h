#pragma once
#include <QDialog>

class QListWidget;
class QLineEdit;
class QLabel;
class QPushButton;
class GitHubService;
class GiteeService;
struct RepoInfo;

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
    void createRelease();

private:
    void renderRepos(const QJsonArray &repos);
    void startClone(const QString &url, const QString &name);

    QListWidget *m_repoList = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QLabel *m_status = nullptr;
    QPushButton *m_cloneBtn = nullptr, *m_forkBtn = nullptr, *m_createBtn = nullptr, *m_releaseBtn = nullptr, *m_openBtn = nullptr, *m_refreshBtn = nullptr;

    // API 服务由对话框持有：随对话框销毁自动取消在途请求，避免回调访问已析构的 this
    GitHubService *m_gh = nullptr;
    GiteeService *m_gitee = nullptr;
};
