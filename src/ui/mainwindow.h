#pragma once
#include <QMainWindow>
#include <QFutureWatcher>
#include "models.h"

class GitService;
class AccountService;
class TitleBar;
class EditorPanel;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;
class QListWidget;
class QPlainTextEdit;
class QTextEdit;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QGroupBox;
class QProcess;
class ProgressDialog;
class TerminalPanel;
class ManualDialog;
class QMenu;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *e) override;
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override;

private slots:
    // repo
    void openRepoDialog();
    void initRepoDialog();
    void refreshAll();
    // files
    void onFileDoubleClicked(const QString &path);
    void onContextMenu(const QPoint &pos);
    void addFileDialog();
    void createFileDialog();
    void deleteSelectedFile();
    void restoreSelectedFile();
    void showDiffForFile(const QString &path);
    void saveCurrentEditor();
    // git
    void commit();
    void commitAndPush();
    void pull();
    void push();
    void doPush();
    void onPushProgressLine(const QString &line);
    void onPushFinished();
    void onPushFailed(const QString &err);
    void switchBranch(const QString &name);
    void createBranchDialog();
    void stashSave();
    void stashPop();
    void showStashList();
    void createTagDialog();
    void showTagList();
    void revertToCommit();
    void showGlobalSearch();
    void createRelease();
    void runCurrentFile(const QString &path = {});  // 空=运行当前打开的文件
    // misc
    void openSettingsDialog();
    void openRepoPanel();
    void showShortcuts();
    void showManual();
    void showAbout();

private:
    void buildMenu();
    void updateConnectTitle();
    QString defaultBrowseDir() const;
    void buildToolbar();
    void buildCentral();
    void buildStatusBar();
    void refreshBranches();
    void refreshStatus();
    void refreshHistory();
    void openRepo(const QString &path);
    QString selectedFilePath() const;
    void rebuildMenus();
    void retranslateUi();

    // services
    GitService *m_git = nullptr;
    // widgets
    TitleBar *m_titleBar = nullptr;
    QComboBox *m_branchCombo = nullptr;
    QLabel *m_repoNameLabel = nullptr, *m_changesTitle = nullptr, *m_statusLabel = nullptr;
    QPushButton *m_openBtn = nullptr, *m_refreshBtn = nullptr, *m_runBtn = nullptr, *m_addFileBtn = nullptr, *m_newFileBtn = nullptr, *m_newBranchBtn = nullptr;
    QPushButton *m_commitBtn = nullptr, *m_commitPushBtn = nullptr;
    QTreeWidget *m_fileTree = nullptr, *m_changeTree = nullptr;
    QPlainTextEdit *m_commitInput = nullptr, *m_diffEdit = nullptr;
    QListWidget *m_historyList = nullptr;
    QTabWidget *m_detailTabs = nullptr;
    QProgressBar *m_progress = nullptr;
    QGroupBox *m_historyGroup = nullptr;
    QProcess *m_pushProcess = nullptr;
    ProgressDialog *m_progressDlg = nullptr;

    // 后台刷新引擎：git 调用全部在工作线程，结果按代数丢弃过期数据
    struct RefreshData {
        QStringList branches;
        GitRepoStatus st;
        QList<CommitInfo> history;
        QString graph;
        quint64 gen = 0;
        bool wantBranches = false, wantStatus = false, wantHistory = false, wantGraph = false;
    };
    QFutureWatcher<RefreshData> *m_refreshWatcher = nullptr;
    quint64 m_refreshGen = 0;
    quint64 m_treeGen = 0;   // 懒加载子目录请求的代数（树重建后作废）
    QString m_diffHash;   // 当前正在加载差异的提交
    QString m_historyLoadedFor;   // 历史懒加载：已加载的仓库
    QSet<QString> m_expandedDirs;      // 文件树已展开目录（刷新后恢复）
    QList<QPair<int, QPair<QString, QWidget *>>> m_hiddenTabs;   // 被关闭页签 (位置,(标签,控件))
    void startRefresh(bool branches, bool status, bool history, bool graph = false);
    void toggleTerminal();
    void applyImageZoom();
    void showImageZoomToast();
    bool eventFilter(QObject *obj, QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    void showLoading(bool on);
    void applyRefresh(const RefreshData &d);
    void expandDirLazy(QTreeWidgetItem *dirItem);      // 展开目录时后台加载子层
    QTreeWidgetItem *findItemByRel(const QString &rel);
    void ensureTab(QWidget *w, int pos, const QString &label);
    void collectExpandedDirs(QTreeWidgetItem *item);
    void restoreExpandedDirs();
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    QString m_currentFile;   // 当前打开编辑器对应的文件（简化：单文件编辑）
    EditorPanel *m_editorPanel = nullptr;
    QStackedWidget *m_editorStack = nullptr;
    QLabel *m_imageView = nullptr;
    QWidget *m_editorHost = nullptr;
    QLabel *m_imageZoomToast = nullptr;
    QTimer *m_imageZoomTimer = nullptr;
    QPixmap m_imagePix;
    QWidget *m_loadingOverlay = nullptr;
    double m_imageZoom = 1.0;   // 相对“适应窗口”的倍率
    QTextEdit *m_graphEdit = nullptr;
    QString m_graphLoadedFor;
    TerminalPanel *m_terminal = nullptr;
    struct MenuActions {
        QMenu *connectMenu = nullptr, *fileMenu = nullptr, *gitMenu = nullptr, *helpMenu = nullptr;
        QAction *connectRepos = nullptr;
        QAction *open = nullptr, *init = nullptr, *quit = nullptr, *searchRepo = nullptr,
            *pull = nullptr, *push = nullptr, *stashSave = nullptr, *stashPop = nullptr,
            *stashList = nullptr, *tagCreate = nullptr, *tagList = nullptr, *grep = nullptr,
            *createRelease = nullptr, *shortcut = nullptr, *manual = nullptr, *about = nullptr, *settings = nullptr,
        *terminal = nullptr;
    } m_actions;
};
