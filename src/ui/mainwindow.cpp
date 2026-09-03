#include "mainwindow.h"
#include "titlebar.h"
#include "codeeditor.h"
#include "highlighter.h"
#include "settingsdialog.h"
#include "repopaneldialog.h"
#include "aboutdialog.h"
#include "manualdialog.h"
#include "progressdialog.h"
#include "terminalpanel.h"
#include "icons.h"
#include "i18n.h"
#include "theme.h"
#include "settings.h"
#include "paths.h"
#include "services/gitservice.h"
#include "services/accountservice.h"
#include <QApplication>
#include <QCursor>
#ifdef _WIN32
#include <windows.h>
#endif
#include <QToolBar>
#include <QMenuBar>
#include <QComboBox>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QTextEdit>
#include <QProgressBar>
#include <QFrame>
#include <QStyledItemDelegate>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDesktopServices>
#include <QDate>
#include <QPainter>
#include <QStackedWidget>
#include <QtConcurrent>
#include <memory>
#include <QHash>
#include <functional>
#include <algorithm>
#include <QLineEdit>
#include <QDialog>
#include <QGroupBox>
#include <QSplitter>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFileDialog>
#include <QInputDialog>
#include <QMessageBox>
#include <QMenu>
#include <QProcess>
#include <QStatusBar>
#include <QHeaderView>
#include "proxy.h"
#include <QDir>
#include <QTimer>
#include <QFile>
#include <QProcessEnvironment>
#include <QSysInfo>

namespace {
GitService *git() { static GitService s; return &s; }

// 文件/历史列表：选中态不画虚线焦点框（视觉与资源管理器一致）
class NoFocusDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override {
        QStyleOptionViewItem opt = option;
        opt.state &= ~QStyle::State_HasFocus;
        QStyledItemDelegate::paint(painter, opt, index);
    }
};



// ANSI 颜色码 → HTML（分支图着色用）
QString ansiToHtml(const QString &in) {
    static const char *fg[] = {"#8b949e", "#e5534b", "#3fb950", "#d29922",
                               "#539bf5", "#c297ff", "#39c5cf", "#e6edf3"};
    QString out;
    out.reserve(in.size() * 2);
    bool open = false;
    for (int i = 0; i < in.size(); ++i) {
        const QChar ch = in.at(i);
        if (ch == QLatin1Char('\x1B') && i + 1 < in.size() && in.at(i + 1) == QLatin1Char('[')) {
            const int end = in.indexOf(QLatin1Char('m'), i);
            if (end < 0) break;
            const QString codes = in.mid(i + 2, end - i - 2);
            i = end;
            if (open) { out += QLatin1String("</span>"); open = false; }
            if (codes == QLatin1String("0") || codes.isEmpty()) continue;
            QString style;
            for (const QString &c : codes.split(';')) {
                bool ok = false;
                int v = c.toInt(&ok);
                if (!ok) continue;
                if (v == 1) style += QLatin1String("font-weight:bold;");
                else if (v == 2) style += QLatin1String("color:#8b949e;");
                else if (v >= 30 && v <= 37) style += QStringLiteral("color:%1;").arg(fg[v - 30]);
                else if (v >= 90 && v <= 97) style += QStringLiteral("color:%1;").arg(fg[v - 90]);
            }
            if (!style.isEmpty()) { out += QStringLiteral("<span style='%1'>").arg(style); open = true; }
        } else if (ch == QLatin1Char('<')) {
            out += QLatin1String("&lt;");
        } else if (ch == QLatin1Char('>')) {
            out += QLatin1String("&gt;");
        } else {
            out += ch;
        }
    }
    if (open) out += QLatin1String("</span>");
    return out;
}
AccountService *acct() { static AccountService s; return &s; }
} // namespace

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent) {
    setWindowTitle("GitFlow");
    setWindowIcon(icons::appIcon());
    setWindowFlags(Qt::Window | Qt::FramelessWindowHint);
    resize(1280, 760);

    buildMenu();
    buildToolbar();
    buildCentral();
    buildStatusBar();

    // 自绘标题栏（含菜单栏）
    auto *container = new QWidget;
    auto *v = new QVBoxLayout(container);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(0);
    m_titleBar = new TitleBar(this);
    v->addWidget(m_titleBar);
    v->addWidget(menuBar());
    setMenuWidget(container);

    git()->setGitPath(settings::gitPath());
    theme::applyToApp();
    updateConnectTitle();
    m_statusLabel->setText(i18n::t("ready"));
    // 恢复上次打开的项目
    const QString last = settings::lastProject();
    if (!last.isEmpty() && QDir(last).exists() && git()->isRepository(last))
        openRepo(last);
    // 加载中遮罩：打开项目/刷新时显示，防止误以为卡死
    m_loadingOverlay = new QWidget(this);
    m_loadingOverlay->setStyleSheet(QStringLiteral(
        "QWidget{background:rgba(13,17,23,120);}"));
    auto *pill = new QFrame(m_loadingOverlay);
    pill->setStyleSheet(QStringLiteral(
        "QFrame{background:rgba(33,38,45,240);border:1px solid rgba(139,148,158,120);"
        "border-radius:10px;}"));
    auto *pv = new QVBoxLayout(pill);
    pv->setContentsMargins(28, 18, 28, 16);
    pv->setSpacing(10);
    auto *loadingLabel = new QLabel(QStringLiteral("\u23F3 ") + i18n::t("loading_repo"));
    loadingLabel->setStyleSheet(QStringLiteral(
        "QLabel{background:transparent;border:none;color:#e6edf3;font-size:13px;font-weight:bold;}"));
    loadingLabel->setAlignment(Qt::AlignCenter);
    pv->addWidget(loadingLabel);
    auto *busy = new QProgressBar;
    busy->setRange(0, 0);   // 忙碌指示
    busy->setFixedHeight(8);
    busy->setStyleSheet(QStringLiteral(
        "QProgressBar{background:rgba(139,148,158,60);border:none;border-radius:4px;}"
        "QProgressBar::chunk{background:%1;border-radius:4px;}").arg(theme::accent()));
    pv->addWidget(busy);
    auto *ol = new QVBoxLayout(m_loadingOverlay);
    ol->setContentsMargins(0, 0, 0, 0);
    ol->addStretch(1);
    ol->addWidget(pill, 0, Qt::AlignHCenter);
    ol->addStretch(1);
    m_loadingOverlay->hide();

    // 内嵌 Git Bash 终端面板（Git 菜单 / Ctrl+` 开关）
    m_terminal = new TerminalPanel(this);
    m_terminal->setGitPath(settings::gitPath());
    m_terminal->setRepo(m_currentFile);
    m_terminal->hide();
    if (centralWidget()) {
        auto *centralLay = new QVBoxLayout;
        centralLay->setContentsMargins(0, 0, 0, 0);
        centralLay->setSpacing(0);
        centralLay->addWidget(centralWidget(), 1);
        centralLay->addWidget(m_terminal);
        QWidget *wrap = new QWidget;
        wrap->setLayout(centralLay);
        setCentralWidget(wrap);
    }

    // 后台预热：提前读取 git 全局作者信息（设置页首次打开零等待）
    QTimer::singleShot(200, this, [] {
        QThreadPool::globalInstance()->start([] {
            try { git()->identity(QString()); } catch (...) {}
        });
    });
}

#ifdef _WIN32
// 无边框窗口：标题行返回 HTCAPTION（原生拖动/双击最大化），边缘 6px 返回 HT*（原生调整大小）
bool MainWindow::nativeEvent(const QByteArray &eventType, void *message, qintptr *result) {
    MSG *msg = static_cast<MSG *>(message);
    if (eventType == "windows_generic_MSG" && msg && msg->message == WM_NCHITTEST && result) {
        QPoint g(static_cast<short>(LOWORD(msg->lParam)),
                 static_cast<short>(HIWORD(msg->lParam)));
        // lParam 的坐标随进程 DPI 虚拟化可能与 Qt 坐标不一致；
        // 真实鼠标交互时光标必然位于查询点，用光标位置保证映射自洽
        const QPoint cur = QCursor::pos();
        if ((g - cur).manhattanLength() > 40) g = cur;
        const QPoint p = mapFromGlobal(g);
        const QRect rc = rect();
        // 窗控按钮永远可点（右边缘热区与其重叠，必须先判定）
        if (auto *c = childAt(p); c && qobject_cast<QPushButton *>(c))
            return QMainWindow::nativeEvent(eventType, message, result);
        constexpr int m = 6;
        const bool eL = p.x() <= m, eR = p.x() >= rc.right() - m;
        const bool eT = p.y() <= m, eB = p.y() >= rc.bottom() - m;
        if (eL || eR || eT || eB) {
            if (!isMaximized()) {
                if (eT && eL) *result = HTTOPLEFT;
                else if (eT && eR) *result = HTTOPRIGHT;
                else if (eB && eL) *result = HTBOTTOMLEFT;
                else if (eB && eR) *result = HTBOTTOMRIGHT;
                else if (eL) *result = HTLEFT;
                else if (eR) *result = HTRIGHT;
                else if (eT) *result = HTTOP;
                else *result = HTBOTTOM;
                return true;
            }
            return false;
        }
        if (p.y() <= 46) {                       // 自绘标题栏行
            *result = HTCAPTION;
            return true;
        }
    }
    return QMainWindow::nativeEvent(eventType, message, result);
}
#endif

void MainWindow::closeEvent(QCloseEvent *e) {
    if (m_pushProcess) m_pushProcess->kill();
    e->accept();
}

// ─────────────── menu ───────────────
void MainWindow::rebuildMenus() { /* placeholder */ }

void MainWindow::buildMenu() {
    QMenuBar *mb = menuBar();
    auto &a = m_actions;

    a.connectMenu = mb->addMenu(i18n::t("menu.connect"));
    a.connectMenu->addAction(i18n::t("repo_search_ph"), this, &MainWindow::openRepoPanel);

    a.fileMenu = mb->addMenu(i18n::t("menu.file"));
    a.open = a.fileMenu->addAction(i18n::t("menu.open"));
    a.open->setShortcut(QKeySequence("Ctrl+O"));
    connect(a.open, &QAction::triggered, this, &MainWindow::openRepoDialog);
    a.init = a.fileMenu->addAction(i18n::t("menu.init"));
    connect(a.init, &QAction::triggered, this, &MainWindow::initRepoDialog);
    a.fileMenu->addSeparator();
    a.quit = a.fileMenu->addAction(i18n::t("menu.quit"));
    a.quit->setShortcut(QKeySequence("Ctrl+Q"));
    connect(a.quit, &QAction::triggered, this, &QWidget::close);

    a.gitMenu = mb->addMenu(i18n::t("menu.git"));
    a.searchRepo = a.gitMenu->addAction(i18n::t("menu.search_repo"));
    connect(a.searchRepo, &QAction::triggered, this, &MainWindow::openRepoPanel);
    a.gitMenu->addSeparator();
    a.pull = a.gitMenu->addAction(i18n::t("menu.pull"));
    connect(a.pull, &QAction::triggered, this, &MainWindow::pull);
    a.push = a.gitMenu->addAction(i18n::t("menu.push"));
    connect(a.push, &QAction::triggered, this, &MainWindow::push);
    a.gitMenu->addSeparator();
    a.stashSave = a.gitMenu->addAction(i18n::t("menu.stash_save"));
    connect(a.stashSave, &QAction::triggered, this, &MainWindow::stashSave);
    a.stashPop = a.gitMenu->addAction(i18n::t("menu.stash_pop"));
    connect(a.stashPop, &QAction::triggered, this, &MainWindow::stashPop);
    a.stashList = a.gitMenu->addAction(i18n::t("menu.stash_list"));
    connect(a.stashList, &QAction::triggered, this, &MainWindow::showStashList);
    a.gitMenu->addSeparator();
    a.tagCreate = a.gitMenu->addAction(i18n::t("menu.tag_create"));
    connect(a.tagCreate, &QAction::triggered, this, &MainWindow::createTagDialog);
    a.tagList = a.gitMenu->addAction(i18n::t("menu.tag_list"));
    connect(a.tagList, &QAction::triggered, this, &MainWindow::showTagList);
    a.gitMenu->addSeparator();
    a.grep = a.gitMenu->addAction(i18n::t("menu.grep"));
    connect(a.grep, &QAction::triggered, this, &MainWindow::showGlobalSearch);
    a.terminal = a.gitMenu->addAction(i18n::t("terminal"));
    a.terminal->setShortcut(QKeySequence("Ctrl+`"));
    connect(a.terminal, &QAction::triggered, this, &MainWindow::toggleTerminal);
    a.gitMenu->addSeparator();
    a.createRelease = a.gitMenu->addAction(i18n::t("create_release"));
    connect(a.createRelease, &QAction::triggered, this, &MainWindow::openRepoPanel);

    a.helpMenu = mb->addMenu(i18n::t("menu.help"));
    a.shortcut = a.helpMenu->addAction(i18n::t("menu.shortcut"));
    connect(a.shortcut, &QAction::triggered, this, &MainWindow::showShortcuts);
    a.manual = a.helpMenu->addAction(i18n::t("menu.manual"));
    connect(a.manual, &QAction::triggered, this, &MainWindow::showManual);
    a.helpMenu->addSeparator();
    a.about = a.helpMenu->addAction(i18n::t("menu.about"));
    connect(a.about, &QAction::triggered, this, &MainWindow::showAbout);

    a.settings = mb->addAction(i18n::t("menu.settings"));
    connect(a.settings, &QAction::triggered, this, &MainWindow::openSettingsDialog);
}

// ─────────────── toolbar ───────────────
void MainWindow::buildToolbar() {
    auto *tb = addToolBar(i18n::t("toolbar_name"));
    tb->setMovable(false);
    m_openBtn = new QPushButton(i18n::t("open_project"));
    connect(m_openBtn, &QPushButton::clicked, this, &MainWindow::openRepoDialog);
    tb->addWidget(m_openBtn);
    m_branchCombo = new QComboBox;
    m_branchCombo->setFixedWidth(180);
    connect(m_branchCombo, &QComboBox::currentTextChanged, this, &MainWindow::switchBranch);
    tb->addWidget(m_branchCombo);
    m_refreshBtn = new QPushButton(i18n::t("refresh"));
    m_refreshBtn->setShortcut(QKeySequence("F5"));
    // 编辑器字号快捷键（Ctrl+= 放大 / Ctrl+- 缩小 / Ctrl+0 复位）
    auto *zin = new QAction(this);
    zin->setShortcut(QKeySequence("Ctrl+="));
    connect(zin, &QAction::triggered, this, [this] {
        if (m_editorPanel) m_editorPanel->setEditorFontPointSize(m_editorPanel->editorFontPointSize() + 1);
    });
    addAction(zin);
    auto *zout = new QAction(this);
    zout->setShortcut(QKeySequence("Ctrl+-"));
    connect(zout, &QAction::triggered, this, [this] {
        if (m_editorPanel) m_editorPanel->setEditorFontPointSize(m_editorPanel->editorFontPointSize() - 1);
    });
    addAction(zout);
    auto *zreset = new QAction(this);
    zreset->setShortcut(QKeySequence("Ctrl+0"));
    connect(zreset, &QAction::triggered, this, [this] {
        if (m_editorPanel) m_editorPanel->setEditorFontPointSize(12);
    });
    addAction(zreset);
    connect(m_refreshBtn, &QPushButton::clicked, this, &MainWindow::refreshAll);
    tb->addWidget(m_refreshBtn);
    m_runBtn = new QPushButton(i18n::t("run_code"));
    m_runBtn->setShortcut(QKeySequence("Ctrl+R"));
    m_runBtn->setToolTip(i18n::t("run_code"));
    connect(m_runBtn, &QPushButton::clicked, this, [this] { runCurrentFile({}); });
    tb->addWidget(m_runBtn);
}

// ─────────────── central ───────────────
void MainWindow::buildCentral() {
    auto *splitter = new QSplitter(Qt::Horizontal);
    setCentralWidget(splitter);

    // 左侧
    auto *left = new QWidget;
    auto *ll = new QVBoxLayout(left);
    ll->setContentsMargins(0, 0, 0, 0);
    ll->setSpacing(0);
    auto *lh = new QHBoxLayout;
    lh->setContentsMargins(4, 4, 4, 4);
    m_repoNameLabel = new QLabel(i18n::t("no_project"));
    lh->addWidget(m_repoNameLabel, 1);
    m_addFileBtn = new QPushButton("+ " + i18n::t("add_file"));
    connect(m_addFileBtn, &QPushButton::clicked, this, &MainWindow::addFileDialog);
    lh->addWidget(m_addFileBtn);
    m_newFileBtn = new QPushButton("+ " + i18n::t("new_file"));
    connect(m_newFileBtn, &QPushButton::clicked, this, &MainWindow::createFileDialog);
    lh->addWidget(m_newFileBtn);
    m_newBranchBtn = new QPushButton("+ " + i18n::t("new_branch"));
    connect(m_newBranchBtn, &QPushButton::clicked, this, &MainWindow::createBranchDialog);
    lh->addWidget(m_newBranchBtn);
    ll->addLayout(lh);

    auto *lsplit = new QSplitter(Qt::Vertical);
    m_fileTree = new QTreeWidget;
    m_fileTree->setIndentation(14);
    m_fileTree->setHeaderHidden(true);
    m_fileTree->setColumnCount(1);
    m_fileTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTree->setTextElideMode(Qt::ElideNone);
    m_fileTree->setUniformRowHeights(true);
    m_fileTree->setItemDelegate(new NoFocusDelegate(m_fileTree));
    connect(m_fileTree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *it) {
        expandDirLazy(it);
        if (!it->data(0, Qt::UserRole + 1).toString().isEmpty())
            m_expandedDirs.insert(it->data(0, Qt::UserRole).toString());
    });
    connect(m_fileTree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *it) {
        if (!it->data(0, Qt::UserRole + 1).toString().isEmpty())
            m_expandedDirs.remove(it->data(0, Qt::UserRole).toString());
    });
    connect(m_fileTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        QStringList parts;
        QTreeWidgetItem *n = item;
        while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
        const QString path = parts.join('/');
        if (path.isEmpty()) return;
        onFileDoubleClicked(path);
    });
    connect(m_fileTree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        QStringList parts;
        QTreeWidgetItem *n = item;
        while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
        const QString path = parts.join('/');
        if (path.isEmpty()) return;
        onFileDoubleClicked(path);
    });
    m_fileTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_fileTree, &QTreeWidget::customContextMenuRequested, this, &MainWindow::onContextMenu);
    lsplit->addWidget(m_fileTree);

    auto *changesPanel = new QWidget;
    auto *cl = new QVBoxLayout(changesPanel);
    cl->setContentsMargins(0, 0, 0, 0);
    cl->setSpacing(0);
    m_changesTitle = new QLabel("  📝 " + i18n::t("changes"));
    m_changesTitle->setStyleSheet("font-weight:bold;padding:4px 0;");
    cl->addWidget(m_changesTitle);
    m_changeTree = new QTreeWidget;
    m_changeTree->setHeaderLabels({ i18n::t("file"), i18n::t("status_col") });
    m_changeTree->setUniformRowHeights(true);
    m_changeTree->setItemDelegate(new NoFocusDelegate(m_changeTree));
    m_changeTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_changeTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    connect(m_changeTree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item, int) {
        onFileDoubleClicked(item->data(0, Qt::UserRole).toString());
    });
    m_changeTree->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_changeTree, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_changeTree->itemAt(pos);
        if (!item || item->data(0, Qt::UserRole).toString().isEmpty()) return;
        QMenu menu(this);
        menu.addAction(i18n::t("discard_changes"), this, &MainWindow::restoreSelectedFile);
        menu.addAction(i18n::t("confirm_delete"), this, &MainWindow::deleteSelectedFile);
        menu.exec(m_changeTree->viewport()->mapToGlobal(pos));
    });
    cl->addWidget(m_changeTree);
    lsplit->addWidget(changesPanel);
    lsplit->setStretchFactor(0, 3);
    lsplit->setStretchFactor(1, 1);
    ll->addWidget(lsplit, 1);

    auto *commitPanel = new QWidget;
    auto *cpl = new QVBoxLayout(commitPanel);
    cpl->setContentsMargins(4, 4, 4, 4);
    m_commitInput = new QPlainTextEdit;
    m_commitInput->setPlaceholderText(i18n::t("commit_placeholder"));
    m_commitInput->setFixedHeight(60);
    cpl->addWidget(m_commitInput);
    auto *btnRow = new QHBoxLayout;
    m_commitBtn = new QPushButton(i18n::t("commit_btn"));
    connect(m_commitBtn, &QPushButton::clicked, this, &MainWindow::commit);
    m_commitPushBtn = new QPushButton(i18n::t("commit_push_btn"));
    connect(m_commitPushBtn, &QPushButton::clicked, this, &MainWindow::commitAndPush);
    btnRow->addWidget(m_commitBtn);
    btnRow->addWidget(m_commitPushBtn);
    cpl->addLayout(btnRow);
    ll->addWidget(commitPanel);
    splitter->addWidget(left);

    // 右侧
    m_detailTabs = new QTabWidget;
    m_detailTabs->setTabsClosable(true);
    connect(m_detailTabs, &QTabWidget::tabCloseRequested, this, [this](int idx) {
        QWidget *w = m_detailTabs->widget(idx);
        const QString label = m_detailTabs->tabText(idx);
        m_hiddenTabs.append({ idx, { label, w } });
        m_detailTabs->removeTab(idx);
        if (m_detailTabs->currentIndex() < 0 && m_detailTabs->count() > 0)
            ensureTab(m_editorHost, 0, i18n::t("editor"));
    });
    m_detailTabs->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_detailTabs->tabBar(), &QTabBar::customContextMenuRequested, this, [this](const QPoint &pos) {
        if (m_hiddenTabs.isEmpty()) return;
        QMenu menu(this);
        for (int i = 0; i < m_hiddenTabs.size(); ++i) {
            const auto t = m_hiddenTabs.at(i);
            menu.addAction(QStringLiteral("\u663e\u793a %1").arg(t.second.first), this, [this, i] {
                const auto t2 = m_hiddenTabs.takeAt(i);
                ensureTab(t2.second.second, t2.first, t2.second.first);
            });
        }
        menu.exec(m_detailTabs->tabBar()->mapToGlobal(pos));
    });
    m_editorHost = new QWidget;
    auto *ehl = new QVBoxLayout(m_editorHost);
    ehl->setContentsMargins(0, 0, 0, 0);
    m_editorPanel = new EditorPanel(QString());
    connect(m_editorPanel, &EditorPanel::saveRequested, this, &MainWindow::saveCurrentEditor);
    connect(m_editorPanel, &EditorPanel::runRequested, this, [this] { runCurrentFile({}); });
    m_imageView = new QLabel;
    m_imageView->setAlignment(Qt::AlignCenter);
    m_imageView->setStyleSheet(QString("background-color:%1;").arg(theme::bg()));
    // 关键：图片控件不参与布局尺寸计算，否则放大图片会把整个窗口撑爆
    m_imageView->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Ignored);
    m_imageView->setMinimumSize(1, 1);
    m_editorStack = new QStackedWidget;
    m_editorStack->addWidget(m_editorPanel);
    m_editorStack->addWidget(m_imageView);
    ehl->addWidget(m_editorStack, 1);
    // 图片缩放悬浮提示
    m_imageZoomToast = new QLabel(m_imageView);
    m_imageZoomToast->setStyleSheet(QStringLiteral(
        "QLabel{background:rgba(33,38,45,225);color:#e6edf3;"
        "border:1px solid rgba(139,148,158,120);border-radius:8px;"
        "padding:4px 12px;font-size:12px;font-weight:bold;}"));
    m_imageZoomToast->hide();
    m_imageZoomTimer = new QTimer(this);
    m_imageZoomTimer->setSingleShot(true);
    connect(m_imageZoomTimer, &QTimer::timeout, m_imageZoomToast, &QLabel::hide);
    m_imageView->installEventFilter(this);
    m_detailTabs->addTab(m_editorHost, i18n::t("editor"));
    m_diffEdit = new QPlainTextEdit;
    m_diffEdit->setReadOnly(true);
    m_diffEdit->setFont(QFont("Consolas", 10));
    m_diffEdit->setStyleSheet(QString(
        "QPlainTextEdit{background-color:%1;color:%2;border:1px solid %3;"
        "font-family:'Consolas','Courier New',monospace;font-size:10pt;}")
        .arg(theme::bg(), theme::text(), theme::border()));
    m_detailTabs->addTab(m_diffEdit, "Diff");
    m_historyGroup = new QGroupBox(i18n::t("commit_history"));
    auto *hl = new QVBoxLayout(m_historyGroup);
    m_historyList = new QListWidget;
    connect(m_detailTabs, &QTabWidget::currentChanged, this, [this](int idx) {
        if (m_currentFile.isEmpty()) return;
        // 分支图懒加载
        if (m_graphEdit && idx == m_detailTabs->indexOf(m_graphEdit)) {
            if (m_graphLoadedFor != m_currentFile) {
                m_graphLoadedFor = m_currentFile;
                startRefresh(false, false, false, true);
            }
            return;
        }
        // 历史懒加载：首次切到历史页才加载（页签被关闭时跳过）
        if (m_detailTabs->indexOf(m_historyGroup) >= 0
                && idx == m_detailTabs->indexOf(m_historyGroup)
                && m_historyLoadedFor != m_currentFile) {
            m_historyLoadedFor = m_currentFile;
            startRefresh(false, false, true, false);
        }
    });
    connect(m_historyList, &QListWidget::currentRowChanged, this, [this](int row) {
        if (row < 0) return;
        const QVariant d = m_historyList->item(row)->data(Qt::UserRole);
        if (!d.isValid()) return;
        const QVariantMap m = d.toMap();
        m_detailTabs->setCurrentIndex(1);
        m_diffEdit->setPlainText(m.value("hash").toString() + "  " +
                                 m.value("subject").toString() + "\n\n..." );
        // 后台读取该提交的完整差异
        if (m_currentFile.isEmpty()) return;
        const QString repo = m_currentFile;
        const QString hash = m.value("hash").toString();
        auto *w = new QFutureWatcher<QString>(this);
        connect(w, &QFutureWatcher<QString>::finished, this, [this, w, hash] {
            w->deleteLater();
            if (hash != m_diffHash) return;      // 用户已切换到其他提交
            QString out = w->result();
            if (out.size() > 200000) out = out.left(200000) + "\n... " + i18n::t("truncated");
            m_diffEdit->setPlainText(out);
        });
        m_diffHash = hash;
        w->setFuture(QtConcurrent::run([repo, hash]() -> QString {
            try {
                return git()->run({ "show", "--format=fuller", "--stat", "--patch", hash }, repo, false);
            } catch (const std::exception &e) {
                return QString::fromUtf8(e.what());
            }
        }));
    });
    m_historyList->setItemDelegate(new NoFocusDelegate(m_historyList));
    hl->addWidget(m_historyList);
    m_historyList->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_historyList, &QListWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto *item = m_historyList->itemAt(pos);
        if (!item || m_currentFile.isEmpty()) return;
        const QString hash = item->data(Qt::UserRole).toMap().value("hash").toString();
        QMenu menu(this);
        menu.addAction(i18n::t("revert_here"), this, [this, hash] {
            if (QMessageBox::question(this, i18n::t("revert_here"),
                                      i18n::t("reset_hard_confirm")) != QMessageBox::Yes) return;
            try {
                git()->resetTo(m_currentFile, hash, true);
                refreshStatus(); refreshHistory();
                m_statusLabel->setText("\u2705 " + hash);
            } catch (const std::exception &e) {
                QMessageBox::critical(this, i18n::t("revert_here"), e.what());
            }
        });
        menu.exec(m_historyList->mapToGlobal(pos));
    });
    m_detailTabs->addTab(m_historyGroup, i18n::t("history"));
    m_graphEdit = new QTextEdit;
    m_graphEdit->setReadOnly(true);
    m_graphEdit->setFont(QFont("Consolas", 10));
    m_graphEdit->setStyleSheet(QString(
        "QTextEdit{background-color:%1;color:%2;border:1px solid %3;"
        "font-family:'Consolas','Courier New',monospace;font-size:10pt;}")
        .arg(theme::bg(), theme::text(), theme::border()));
    m_detailTabs->addTab(m_graphEdit, i18n::t("tab_graph"));
    ensureTab(m_editorHost, 0, i18n::t("editor"));
    splitter->addWidget(m_detailTabs);
    splitter->setStretchFactor(0, 3);
    splitter->setStretchFactor(1, 7);
}

void MainWindow::buildStatusBar() {
    auto *sb = statusBar();
    auto *iconLabel = new QLabel;
    iconLabel->setPixmap(QIcon(":/icon/logo.ico").pixmap(16, 16));
    sb->addPermanentWidget(iconLabel);
    m_statusLabel = new QLabel(i18n::t("ready"));
    sb->addPermanentWidget(m_statusLabel);
    m_progress = new QProgressBar;
    m_progress->setMaximumWidth(160);
    m_progress->setMaximumHeight(14);
    m_progress->setTextVisible(false);
    m_progress->hide();
    sb->addPermanentWidget(m_progress);
}

// ─────────────── repo ───────────────
// Qt 自带翻译缺失时保证文件对话框为中文标签
static void localizeFileDialog(QFileDialog &dlg) {
    dlg.setLabelText(QFileDialog::LookIn, i18n::t("fd_look_in"));
    dlg.setLabelText(QFileDialog::FileName, i18n::t("fd_directory"));
    dlg.setLabelText(QFileDialog::FileType, i18n::t("fd_file_type"));
    dlg.setLabelText(QFileDialog::Accept, i18n::t("choose_folder"));
    dlg.setLabelText(QFileDialog::Reject, i18n::t("cancel"));
}

// 目录对话框默认起点：存储根目录/平台/用户名（存在时），否则存储根目录
QString MainWindow::defaultBrowseDir() const {
    const QString root = settings::storageRoot();
    const Account a = acct()->currentAccount();
    if (!a.username.isEmpty()) {
        const QString deep = root + "/" + a.platform + "/" + a.username;
        if (QDir(deep).exists()) return deep;
    }
    return root;
}

void MainWindow::openRepoDialog() {
    // 非 native 对话框：原生对话框常落在上次浏览位置/上级目录，初始目录不生效
    QFileDialog dlg(this, i18n::t("open_repo_title"), defaultBrowseDir());
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    localizeFileDialog(dlg);
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return;
    openRepo(dlg.selectedFiles().first());
}

void MainWindow::initRepoDialog() {
    QFileDialog dlg(this, i18n::t("init_repo_title"), defaultBrowseDir());
    dlg.setFileMode(QFileDialog::Directory);
    dlg.setOption(QFileDialog::ShowDirsOnly, true);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    localizeFileDialog(dlg);
    if (dlg.exec() != QDialog::Accepted || dlg.selectedFiles().isEmpty()) return;
    const QString dir = dlg.selectedFiles().first();
    if (dir.isEmpty()) return;
    try {
        git()->init(dir);
        openRepo(dir);
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("init_failed"), e.what());
    }
}

void MainWindow::openRepo(const QString &path) {
    if (!git()->isRepository(path)) {
        if (QMessageBox::question(this, i18n::t("init_q"), i18n::t("init_btn")) == QMessageBox::Yes) {
            git()->init(path);
        } else {
            return;
        }
    }
    settings::setLastProject(path);
    m_currentFile = path;
    // 每仓库一次性启用 git 状态缓存（后台执行，不阻塞打开）；常规刷新不再重复配置
    QThreadPool::globalInstance()->start([path] {
        try { git()->enableStatusCache(path); } catch (...) {}
    });
    m_commitInput->setPlainText(i18n::t("default_commit_msg"));
    if (m_terminal) m_terminal->setRepo(path);
    const QString name = QDir(path).dirName();
    m_repoNameLabel->setText("📁 " + name);
    setWindowTitle("GitFlow - " + name);
    m_titleBar->setTitle("GitFlow - " + name);
    m_historyLoadedFor.clear();
    startRefresh(true, true, false, false);
}

void MainWindow::refreshAll() {
    if (!m_repoNameLabel->text().startsWith("📁")) return;
    m_historyLoadedFor.clear();
    startRefresh(true, true, false, false);
}

void MainWindow::refreshBranches() {
    if (m_currentFile.isEmpty()) return;
    startRefresh(true, false, false);
}

void MainWindow::refreshStatus() {
    if (m_currentFile.isEmpty()) return;
    startRefresh(false, true, false);
}

void MainWindow::refreshHistory() {
    if (m_currentFile.isEmpty()) return;
    startRefresh(false, false, true);
}

// ─────────────── 后台刷新引擎 ───────────────
// 每类数据一个并行任务（Windows 上 git 进程启动是大头，并行后总耗时≈最慢者）；
// 代数计数保证快速连点时旧结果被丢弃。
void MainWindow::startRefresh(bool branches, bool status, bool history, bool graph) {
    if (m_currentFile.isEmpty()) return;
    const QString repo = m_currentFile;
    if (status) showLoading(true);
    auto launch = [this, repo](QLatin1Char kind) {
        const quint64 gen = ++m_refreshGen;
        auto *w = new QFutureWatcher<RefreshData>(this);
        connect(w, &QFutureWatcher<RefreshData>::finished, this, [this, w, gen, kind] {
            const RefreshData d = w->result();
            w->deleteLater();
            if (gen != m_refreshGen) return;   // 已有更新的刷新，丢弃过期结果
            applyRefresh(d);
            if (kind == QLatin1Char('s')) showLoading(false);
        });
        w->setFuture(QtConcurrent::run([this, repo, gen, kind]() -> RefreshData {
            RefreshData d;
            d.gen = gen;
            switch (kind.toLatin1()) {
            case 'b':
                d.wantBranches = true;
                try { d.branches = git()->branches(repo); } catch (...) {}
                break;
            case 's':
                d.wantStatus = true;
                try { d.st = git()->status(repo); } catch (...) {}
                break;
            case 'h':
                d.wantHistory = true;
                try { d.history = git()->history(repo); } catch (...) {}
                break;
            case 'g':
                d.wantGraph = true;
                try {
                    d.graph = git()->run({ "log", "--graph", "--color=always",
                                           "--pretty=format:%C(yellow)%h%Creset %s %C(dim)[%an %ad]%C(auto)%d",
                                           "--date=short", "--all", "-n", "300" }, repo, false);
                } catch (...) {}
                break;
            }
            return d;
        }));
    };
    if (branches) launch(QLatin1Char('b'));
    if (status) launch(QLatin1Char('s'));
    if (history) launch(QLatin1Char('h'));
    if (graph) launch(QLatin1Char('g'));
}

void MainWindow::applyRefresh(const RefreshData &d) {
    if (d.wantBranches) {
        m_branchCombo->blockSignals(true);
        m_branchCombo->clear();
        m_branchCombo->addItems(d.branches);
        if (!d.st.branch.isEmpty()) m_branchCombo->setCurrentText(d.st.branch);
        m_branchCombo->blockSignals(false);
    }
    if (d.wantStatus) {
        // 算法优化：只构建第一层（单次目录枚举，毫秒级），子目录展开时才懒加载
        ++m_treeGen;   // 树重建，作废未完成的懒加载请求
        if (m_fileTree->topLevelItem(0)) collectExpandedDirs(m_fileTree->topLevelItem(0));
        m_fileTree->clear();
        auto *allRoot = new QTreeWidgetItem({ "📁 " + QDir(m_currentFile).dirName() });
        allRoot->setIcon(0, icons::folderIcon());
        m_fileTree->addTopLevelItem(allRoot);
        const QFileInfoList entries = QDir(m_currentFile).entryInfoList(
            QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDir::DirsFirst | QDir::Name);
        for (const QFileInfo &fi : entries) {
            if (fi.fileName() == QLatin1String(".git")) continue;
            auto *it = new QTreeWidgetItem({ fi.fileName() });
            if (fi.isDir()) {
                it->setIcon(0, icons::folderIcon());
                it->setData(0, Qt::UserRole, fi.fileName());
                it->setData(0, Qt::UserRole + 1, QStringLiteral("lazy"));
                auto *ph = new QTreeWidgetItem;
                ph->setFlags(Qt::NoItemFlags);
                it->addChild(ph);   // 占位：让箭头显示
            } else {
                it->setIcon(0, icons::fileIcon(fi.fileName()));
                it->setData(0, Qt::UserRole, fi.fileName());
            }
            allRoot->addChild(it);
        }
        restoreExpandedDirs();
        // 变更分组
        m_changeTree->clear();
        auto addTree = [&](const QString &title, const QList<GitFileItem> &items) {
            if (items.isEmpty()) return;
            auto *root = new QTreeWidgetItem({ title });
            for (const GitFileItem &f : items) {
                auto *c = new QTreeWidgetItem({ QFileInfo(f.path).fileName() });
                c->setIcon(0, icons::fileIcon(f.path));
                c->setText(1, [f] {
                    switch (f.status) {
                    case GitFileStatus::Modified: return i18n::t("st_modified");
                    case GitFileStatus::Added: return i18n::t("st_added");
                    case GitFileStatus::Deleted: return i18n::t("st_deleted");
                    case GitFileStatus::Renamed: return i18n::t("st_renamed");
                    case GitFileStatus::Untracked: return i18n::t("st_untracked");
                    default: return i18n::t("st_conflict");
                    }
                }());
                c->setData(0, Qt::UserRole, f.path);
                root->addChild(c);
            }
            m_changeTree->addTopLevelItem(root);
            root->setExpanded(true);
        };
        QList<GitFileItem> staged, modified, untracked;
        for (const GitFileItem &f : d.st.files) {
            if (f.status == GitFileStatus::Untracked) untracked.append(f);
            else if (f.staged) staged.append(f);
            else modified.append(f);
        }
        addTree(i18n::t("group_staged"), staged);
        addTree(i18n::t("group_modified"), modified);
        addTree(i18n::t("group_untracked"), untracked);
        m_statusLabel->setText(QStringLiteral("%1 | +%2 -%3").arg(d.st.branch).arg(d.st.ahead).arg(d.st.behind));
    }
    if (d.wantGraph && m_graphEdit)
        m_graphEdit->setHtml(QStringLiteral(
            "<pre style='font-family:Consolas,monospace; white-space:pre-wrap;'>%1</pre>")
            .arg(ansiToHtml(d.graph)));
    if (d.wantStatus && m_terminal) m_terminal->setBranch(d.st.branch);
    if (d.wantHistory) {
        m_historyLoadedFor = m_currentFile;
        m_historyList->blockSignals(true);
        m_historyList->clear();
        for (const CommitInfo &c : d.history) {
            auto *item = new QListWidgetItem(
                QStringLiteral("● %1  %2  (%3)").arg(c.shortHash, c.subject, c.date));
            item->setData(Qt::UserRole, QVariantMap {
                { "hash", c.hash }, { "subject", c.subject } });
            m_historyList->addItem(item);
        }
        m_historyList->blockSignals(false);
    }
}

// ─────────────── files ───────────────
QString MainWindow::selectedFilePath() const {
    auto *item = m_fileTree->currentItem();
    if (!item) return {};
    QStringList parts;
    QTreeWidgetItem *n = item;
    while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
    return parts.join('/');
}

void MainWindow::onFileDoubleClicked(const QString &path) {
    if (path.isEmpty()) return;
    const QString full = QDir(m_currentFile).filePath(path);
    if (QFileInfo(full).isDir()) return;
    const QString suffix = QFileInfo(full).suffix().toLower();
    const QString baseName = QFileInfo(full).completeBaseName().toLower();
    static const QSet<QString> images { "png", "jpg", "jpeg", "bmp", "gif", "webp" };
    static const QSet<QString> texts { "py", "c", "cpp", "h", "hpp", "java", "js", "ts", "go",
                                       "rs", "cs", "php", "sh", "json", "yaml", "yml", "toml",
                                       "xml", "html", "css", "sql", "txt", "md", "ini", "cfg",
                                       "properties", "bat", "cmd", "ps1", "lua", "rb", "kt",
                                       "swift", "scala", "csv", "log", "svg" };
    // 无扩展名常见文本文件（.gitignore/.gitattributes/Dockerfile/Makefile...）
    static const QSet<QString> textNames { "gitignore", "gitattributes", "dockerfile",
                                           "makefile", "license", "readme", "changelog" };
    const bool isText = texts.contains(suffix) || textNames.contains(baseName)
                        || suffix.isEmpty();   // 无扩展名默认按文本尝试
    if (images.contains(suffix)) {
        QPixmap pm(full);
        if (pm.isNull()) { QMessageBox::warning(this, path, i18n::t("img_load_failed")); return; }
        m_imagePix = pm;
        m_imageZoom = 1.0;
        applyImageZoom();
        m_editorStack->setCurrentWidget(m_imageView);
        ensureTab(m_editorHost, 0, i18n::t("editor"));
        m_detailTabs->setTabText(0, "\U0001F5BC " + path);
        return;
    }
    if (!isText) {
        showDiffForFile(path);
        return;
    }
    QFile f(full);
    if (!f.open(QIODevice::ReadOnly)) return;
    m_editorPanel->setPlainText(QString::fromUtf8(f.readAll()));
    m_editorStack->setCurrentWidget(m_editorPanel);
    if (m_detailTabs->indexOf(m_editorHost) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_editorHost), i18n::t("editor"));
    m_currentFile = m_currentFile; // repo root unchanged
    ensureTab(m_editorHost, 0, i18n::t("editor"));
    m_editorPanel->setOpenPath(full);
    m_statusLabel->setText(full);
}

// 运行当前（或右键指定的）代码文件：按扩展名找解释器/编译器，命令注入终端
void MainWindow::runCurrentFile(const QString &path) {
    QString file = path;
    if (file.isEmpty()) file = m_editorPanel->openPath();
    if (file.isEmpty()) {
        QMessageBox::information(this, i18n::t("hint"), i18n::t("no_file_open"));
        return;
    }

    // 编辑器里有未保存修改时提醒：可能运行的是旧代码
    if (m_editorPanel->isModified()) {
        QMessageBox box(this);
        box.setWindowTitle(i18n::t("hint"));
        box.setIcon(QMessageBox::Warning);
        box.setText(i18n::t("run_unsaved_warn"));
        auto *saveBtn = box.addButton(i18n::t("save_and_run"), QMessageBox::AcceptRole);
        auto *runBtn = box.addButton(i18n::t("run_anyway"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        QAbstractButton *clicked = box.clickedButton();
        if (clicked == nullptr || clicked == box.button(QMessageBox::Cancel)) return;
        if (clicked == saveBtn) saveCurrentEditor();   // 保存后再运行
    }
    const QFileInfo fi(file);
    const QString suffix = fi.suffix().toLower();
    const QString full = fi.absoluteFilePath();

    // 扩展名 → 可执行程序名
    QString exe;
    if (suffix == QLatin1String("py"))      exe = QStringLiteral("python");
    else if (suffix == QLatin1String("js")) exe = QStringLiteral("node");
    else if (suffix == QLatin1String("ts")) { exe = QStringLiteral("ts-node"); }
    else if (suffix == QLatin1String("c") || suffix == QLatin1String("cpp")
             || suffix == QLatin1String("cxx") || suffix == QLatin1String("h")) {
        exe = QStringLiteral("g++");   // 编译：g++ 源文件 -o 同名exe 再运行
    }
    else if (suffix == QLatin1String("java")) {
        exe = QStringLiteral("javac");   // Java：javac 编译后 java 运行类名
    }
    else if (suffix == QLatin1String("html") || suffix == QLatin1String("htm")) {
        // 用默认浏览器打开
        QDesktopServices::openUrl(QUrl::fromLocalFile(full));
        return;
    }
    else {
        QMessageBox::information(this, i18n::t("hint"), i18n::t("run_unsupported").arg(suffix));
        return;
    }

    // 从 PATH 找解释器/编译器（Windows 上带 .exe / .bat / .cmd）
    QString interp;
    const QStringList dirs = QProcessEnvironment::systemEnvironment().value("PATH").split(';', Qt::SkipEmptyParts);
    for (const QString &d : dirs) {
        for (const char *ext : {".exe", ".bat", ".cmd", ""}) {
            const QString cand = QDir(d).filePath(exe + QLatin1String(ext));
            if (QFileInfo::exists(cand)) { interp = QDir::toNativeSeparators(cand); break; }
        }
        if (!interp.isEmpty()) break;
    }
    if (interp.isEmpty()) {
        QMessageBox::warning(this, i18n::t("hint"),
                             i18n::t("interpreter_not_found").arg(exe));
        return;
    }
    interp.replace('\\', '/');   // 命令在 bash 下执行，路径统一正斜杠

    // 组装命令（终端用 bash -c 执行，路径统一转正斜杠）
    QString cmd;
    const QString dirFwd = fi.absolutePath().replace('\\', '/');
    QString fullFwd = full;
    fullFwd.replace('\\', '/');
    if (suffix == QLatin1String("java")) {
        // 切到文件目录：javac 编译 → java 运行类名
        cmd = QStringLiteral("cd \"%1\" && javac \"%2\" && java \"%3\"")
                  .arg(dirFwd, fi.fileName(), fi.completeBaseName());
    } else if (suffix == QLatin1String("c") || suffix == QLatin1String("cpp")
               || suffix == QLatin1String("cxx") || suffix == QLatin1String("h")) {
        // g++ 编译出同名 exe 后运行
        const QString outExe = dirFwd + "/" + fi.completeBaseName() + ".exe";
        cmd = QStringLiteral("cd \"%1\" && \"%2\" \"%3\" -o \"%4\" && \"%4\"")
                  .arg(dirFwd, interp, fullFwd, outExe);
    } else {
        cmd = QStringLiteral("\"%1\" \"%2\"").arg(interp, fullFwd);
    }

    // 显示终端并执行
    m_statusLabel->setText("\u25B6 " + i18n::t("run_code") + ": " + fi.fileName());
    if (m_terminal) {
        m_terminal->setRepo(fi.absolutePath());   // 工作目录切到文件所在目录
        m_terminal->runCommandText(cmd);
        if (!m_terminal->isVisible()) toggleTerminal();
    }
}

void MainWindow::onContextMenu(const QPoint &pos) {
    auto *item = m_fileTree->itemAt(pos);
    if (!item) return;
    m_fileTree->setCurrentItem(item);
    QMenu menu(this);
    // 重组完整路径
    QStringList parts;
    QTreeWidgetItem *n = item;
    while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
    const QString path = parts.join('/');
    const QString full = QDir(m_currentFile).filePath(path);
    menu.addAction(i18n::t("open_edit"), this, [this, path] { onFileDoubleClicked(path); });
    menu.addAction(i18n::t("new_file"), this, &MainWindow::createFileDialog);
    menu.addAction(i18n::t("add_file_menu"), this, &MainWindow::addFileDialog);
    if (QFileInfo(full).isFile()) {
        menu.addAction(i18n::t("run_code"), this, [this, full] { runCurrentFile(full); });
        menu.addSeparator();
        menu.addAction(i18n::t("view_diff"), this, [this, path] { showDiffForFile(path); });
        menu.addAction(i18n::t("view_blame"), this, [this, path] {
            try {
                m_diffEdit->setPlainText(git()->blame(m_currentFile, path));
                m_detailTabs->setCurrentIndex(1);
            } catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("error"), e.what()); }
        });
        menu.addSeparator();
        menu.addAction(i18n::t("discard_changes"), this, [this, path] {
            try { git()->restore(m_currentFile, path); refreshStatus(); }
            catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("error"), e.what()); }
        });
        menu.addAction(i18n::t("delete_file"), this, [this, path] {
            if (QMessageBox::question(this, i18n::t("confirm_delete"),
                                      i18n::t("delete_q").arg(path)) != QMessageBox::Yes) return;
            try { git()->deleteFile(m_currentFile, path); refreshStatus(); }
            catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("delete_failed"), e.what()); }
        });
    }
    menu.exec(m_fileTree->viewport()->mapToGlobal(pos));
}

void MainWindow::addFileDialog() {
    if (m_currentFile.isEmpty()) return;
    QFileDialog dlg(this, i18n::t("pick_files"), m_currentFile);
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    localizeFileDialog(dlg);
    if (dlg.exec() != QDialog::Accepted) return;
    const QStringList paths = dlg.selectedFiles();
    for (const QString &src : paths)
        QFile::copy(src, QDir(m_currentFile).filePath(QFileInfo(src).fileName()));
    refreshStatus();
}

void MainWindow::createFileDialog() {
    if (m_currentFile.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("new_file_title"),
                                               i18n::t("file_name_label"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QString full = QDir(m_currentFile).filePath(name.trimmed());
    if (QFileInfo::exists(full)) { QMessageBox::warning(this, i18n::t("file_exists"), i18n::t("file_exists_body")); return; }
    QDir().mkpath(QFileInfo(full).path());
    QFile f(full);
    f.open(QIODevice::WriteOnly);
    f.close();
    refreshStatus();
    m_editorPanel->setPlainText({});
    m_editorStack->setCurrentWidget(m_editorPanel);
    m_editorPanel->setOpenPath(full);
    m_statusLabel->setText(full);
}

void MainWindow::showDiffForFile(const QString &path) {
    ensureTab(m_diffEdit, 1, QStringLiteral("Diff"));
    if (m_currentFile.isEmpty()) return;
    m_diffEdit->setPlainText(i18n::t("refreshing"));
    const QString repo = m_currentFile;
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        w->deleteLater();
        m_diffEdit->setPlainText(w->result());
    });
    w->setFuture(QtConcurrent::run([repo, path]() -> QString {
        try { return git()->diff(repo, path); }
        catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    }));
}

void MainWindow::saveCurrentEditor() {
    const QString path = m_editorPanel->openPath();
    if (path.isEmpty()) return;
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    f.write(m_editorPanel->text().toUtf8());
    f.close();
    m_editorPanel->setModified(false);
    m_statusLabel->setText("✅ " + path);
    refreshStatus();
}

// ─────────────── git ops ───────────────
void MainWindow::commit() {
    const QString msg = m_commitInput->toPlainText().trimmed();
    if (msg.isEmpty()) { QMessageBox::information(this, i18n::t("hint"), i18n::t("enter_commit_msg")); return; }
    if (m_currentFile.isEmpty()) return;
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    m_statusLabel->setText("⏳ " + i18n::t("committing_local"));
    const QString repo = m_currentFile;
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err] {
        w->deleteLater();
        m_commitBtn->setEnabled(true);
        m_commitPushBtn->setEnabled(true);
        if (!w->result()) {
            m_statusLabel->setText("❌ " + i18n::t("commit_failed"));
            QMessageBox::critical(this, i18n::t("commit_failed"), *err);
            return;
        }
        m_commitInput->setPlainText(i18n::t("default_commit_msg"));
        m_statusLabel->setText("✅ " + i18n::t("commit_success"));
        m_historyLoadedFor.clear();
        startRefresh(false, true, false);
    });
    w->setFuture(QtConcurrent::run([repo, msg, err]() -> bool {
        try {
            git()->commit(repo, msg, {});
            return true;
        } catch (const std::exception &e) {
            *err = QString::fromUtf8(e.what());
            return false;
        }
    }));
}

void MainWindow::commitAndPush() {
    const QString msg = m_commitInput->toPlainText().trimmed();
    if (msg.isEmpty()) { QMessageBox::information(this, i18n::t("hint"), i18n::t("enter_commit_msg")); return; }
    if (m_currentFile.isEmpty()) return;
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    m_statusLabel->setText("⏳ " + i18n::t("committing_local"));
    const QString repo = m_currentFile;
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err] {
        w->deleteLater();
        if (!w->result()) {
            m_commitBtn->setEnabled(true);
            m_commitPushBtn->setEnabled(true);
            m_statusLabel->setText("❌ " + i18n::t("commit_failed"));
            QMessageBox::critical(this, i18n::t("commit_failed"), *err);
            return;
        }
        m_commitInput->setPlainText(i18n::t("default_commit_msg"));
        // 提交落盘后再推送，避免 push 走的是旧历史
        push();
    });
    w->setFuture(QtConcurrent::run([repo, msg, err]() -> bool {
        try {
            git()->commit(repo, msg, {});
            return true;
        } catch (const std::exception &e) {
            *err = QString::fromUtf8(e.what());
            return false;
        }
    }));
}

void MainWindow::pull() {
    const Account a = acct()->currentAccount();
    try {
        git()->pull(m_currentFile, a.token, a.username);
        m_statusLabel->setText("✅ " + i18n::t("pull_success"));
        refreshStatus(); refreshHistory();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("pull_failed"), e.what());
    }
}

// 推送入口：后台扫描大文件（不卡 UI），确认后进入 doPush
void MainWindow::push() {
    if (m_currentFile.isEmpty()) { doPush(); return; }
    const QString repo = m_currentFile;
    auto *w = new QFutureWatcher<QList<OversizedFile>>(this);
    connect(w, &QFutureWatcher<QList<OversizedFile>>::finished, this, [this, w] {
        const QList<OversizedFile> big = w->result();
        w->deleteLater();
        if (big.isEmpty()) { doPush(); return; }
        // 扫描完成后：所有超限文件直接列在正文（文件名 + 相对路径 + 大小）
        QString list;
        for (const auto &f : big)
            list += QStringLiteral("<li><code>%1</code> — %2 MB</li>")
                        .arg(f.path.toHtmlEscaped(), QString::number(f.sizeMb, 'f', 1));
        QMessageBox box(this);
        box.setWindowTitle(i18n::t("oversized_title"));
        box.setIcon(QMessageBox::Warning);
        box.setTextFormat(Qt::RichText);
        box.setText(QStringLiteral("<b>%1</b><br>%2<ul style='margin:6px 0 6px 18px;'>%3</ul>")
                        .arg(i18n::t("oversized_title"),
                             i18n::t("oversized_body"), list));
        box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
        box.button(QMessageBox::Yes)->setText(i18n::t("push_anyway"));
        box.button(QMessageBox::No)->setText(i18n::t("cancel"));
        if (box.exec() == QMessageBox::Yes) doPush();
    });
    w->setFuture(QtConcurrent::run([repo] { return git()->findOversizedFiles(repo); }));
}

void MainWindow::doPush() {
    if (m_pushProcess) { delete m_pushProcess; m_pushProcess = nullptr; }

    const Account a = acct()->currentAccount();
    m_pushProcess = new QProcess(this);
    QProcessEnvironment pe = QProcessEnvironment::systemEnvironment();
    pe.insert("GIT_TERMINAL_PROMPT", "0");
    // askpass
    const QString dir = QDir::tempPath() + "/gitflow_auth";
    QDir().mkpath(dir);
    const QString bat = dir + "/askpass.cmd";
    { QFile f(bat); if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream ts(&f);
        ts << "@echo off\r\necho %~1 | findstr /I \"Username\" >nul\r\n";
        ts << "if not errorlevel 1 (echo " << (a.username.isEmpty() ? "oauth2" : a.username) << ")"
           << " else (echo " << a.token << ")\r\n"; } }
    pe.insert("GIT_ASKPASS", bat);
    // 代理
    const QString proxy = proxy::detectSystemProxy();
    if (!proxy.isEmpty()) { pe.insert("HTTPS_PROXY", proxy); pe.insert("HTTP_PROXY", proxy); }
    m_pushProcess->setProcessEnvironment(pe);
    m_pushProcess->setWorkingDirectory(m_currentFile);
    // 进度弹窗：实时展示推送百分比/阶段，卡住检测
    m_progressDlg = new ProgressDialog(i18n::t("pushing"), this);
    m_progressDlg->show();
    m_progressDlg->raise();
    m_progressDlg->activateWindow();
    connect(m_pushProcess, &QProcess::readyReadStandardError, this, [this] {
        const QStringList lines = QString::fromUtf8(m_pushProcess->readAllStandardError())
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : lines) onPushProgressLine(l.trimmed());
    });
    connect(m_pushProcess, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) {
        if (code == 0) onPushFinished();
        else onPushFailed(QString::fromUtf8(m_pushProcess->readAllStandardError()));
    });
    m_pushProcess->start(git()->gitPath(), { "-c", "http.version=HTTP/1.1", "push", "--progress" });
}

// ─────────────── push 进度槽 ───────────────
void MainWindow::onPushProgressLine(const QString &line) {
    m_statusLabel->setText("\u23f3 " + i18n::t("pushing"));
    if (m_progressDlg) m_progressDlg->appendLine(line);
}

void MainWindow::onPushFinished() {
    if (m_progressDlg) { m_progressDlg->finishOk(i18n::t("push_success")); m_progressDlg = nullptr; }
    m_statusLabel->setText("\u2705 " + i18n::t("push_success"));
    refreshStatus(); refreshHistory();
}

void MainWindow::onPushFailed(const QString &err) {
    if (m_progressDlg) { m_progressDlg->finishFail(err, GitService::diagnoseNetwork()); m_progressDlg = nullptr; }
    m_statusLabel->setText("\u274c " + i18n::t("push_failed"));
}

void MainWindow::deleteSelectedFile() {
    auto *item = m_changeTree->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, "GitFlow", i18n::t("confirm_delete").arg(path)) != QMessageBox::Yes) return;
    try { git()->deleteFile(m_currentFile, path); refreshStatus(); refreshHistory(); }
    catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("delete_failed"), e.what()); }
}

void MainWindow::restoreSelectedFile() {
    auto *item = m_changeTree->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, "GitFlow", i18n::t("discard_changes").arg(path)) != QMessageBox::Yes) return;
    try { git()->restore(m_currentFile, path); refreshStatus(); }
    catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("restore_failed"), e.what()); }
}

void MainWindow::switchBranch(const QString &name) {
    if (name.isEmpty() || m_currentFile.isEmpty()) return;
    try { git()->switchBranch(m_currentFile, name); refreshStatus(); refreshHistory(); }
    catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("switch_failed"), e.what()); }
}

void MainWindow::createBranchDialog() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("new_branch_t"),
                                               i18n::t("branch_name"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    git()->createBranch(m_currentFile, name.trimmed());
    refreshBranches();
}

void MainWindow::stashSave() {
    bool ok = false;
    const QString msg = QInputDialog::getText(this, i18n::t("menu.stash_save"),
                                              i18n::t("stash_hint"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    git()->stashSave(m_currentFile, msg);
    m_statusLabel->setText("✅ " + i18n::t("stash_ok"));
    refreshStatus();
}

void MainWindow::stashPop() {
    git()->stashPop(m_currentFile);
    m_statusLabel->setText("✅ " + i18n::t("stash_popped_t"));
    refreshStatus();
}

void MainWindow::showStashList() {
    const auto stashes = git()->stashList(m_currentFile);
    if (stashes.isEmpty()) { QMessageBox::information(this, i18n::t("stash_list_t"), i18n::t("no_stash")); return; }
    QDialog dlg(this);
    dlg.setWindowTitle(i18n::t("stash_list_t"));
    dlg.setMinimumWidth(480);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(i18n::t("stash_del_hint")));
    auto *list = new QListWidget;
    for (const auto &s : stashes)
        list->addItem(s.ref + "  " + s.subject);
    layout->addWidget(list);
    layout->addWidget(new QLabel(i18n::t("stash_del_hint")));
    dlg.exec();
}

void MainWindow::createTagDialog() {
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("menu.tag_create"),
                                               i18n::t("tag_name"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    git()->createTag(m_currentFile, name.trimmed());
    m_statusLabel->setText("✅ " + i18n::t("tag_created"));
}

void MainWindow::showTagList() {
    const QStringList tags = git()->tags(m_currentFile);
    QMessageBox::information(this, i18n::t("tag_list_t"), tags.join('\n'));
}

void MainWindow::revertToCommit() {
    auto *item = m_historyList->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QVariantMap m = item->data(Qt::UserRole).toMap();
    const QString hash = m.value("hash").toString();
    if (hash.isEmpty()) return;
    if (QMessageBox::question(this, i18n::t("revert_here"),
                              i18n::t("reset_hard_confirm")) != QMessageBox::Yes) return;
    try {
        git()->resetTo(m_currentFile, hash, true);
        refreshStatus(); refreshHistory();
        m_statusLabel->setText("\u2705 " + hash);
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("revert_here"), e.what());
    }
}

// 按当前倍率渲染图片（1.0 = 适应窗口）
void MainWindow::applyImageZoom() {
    if (m_imagePix.isNull() || !m_imageView || m_imageView->width() <= 0) return;
    const double fit = qMin(double(m_imageView->width()) / m_imagePix.width(),
                            double(m_imageView->height()) / m_imagePix.height());
    const double scale = qBound(0.05, fit * m_imageZoom, 8.0);
    m_imageView->setPixmap(m_imagePix.scaled(
        int(m_imagePix.width() * scale), int(m_imagePix.height() * scale),
        Qt::KeepAspectRatio, Qt::SmoothTransformation));
}

void MainWindow::showImageZoomToast() {
    if (!m_imageZoomToast || m_imagePix.isNull()) return;
    const double fit = qMin(double(m_imageView->width()) / m_imagePix.width(),
                            double(m_imageView->height()) / m_imagePix.height());
    m_imageZoomToast->setText(QStringLiteral("%1%").arg(qRound(fit * m_imageZoom * 100)));
    m_imageZoomToast->adjustSize();
    m_imageZoomToast->move(m_imageView->width() - m_imageZoomToast->width() - 16,
                           m_imageView->height() - m_imageZoomToast->height() - 16);
    m_imageZoomToast->raise();
    m_imageZoomToast->show();
    m_imageZoomTimer->start(900);
}

// 图片视图：Ctrl+滚轮缩放，双击复原
bool MainWindow::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_imageView && m_imageView) {
        if (e->type() == QEvent::Wheel) {
            auto *we = static_cast<QWheelEvent *>(e);
            if (we->modifiers() & Qt::ControlModifier) {
                m_imageZoom = qBound(0.1, m_imageZoom * (we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15), 12.0);
                applyImageZoom();
                showImageZoomToast();
                we->accept();
                return true;
            }
        } else if (e->type() == QEvent::MouseButtonDblClick) {
            m_imageZoom = 1.0;
            applyImageZoom();
            showImageZoomToast();
            return true;
        }
    }
    return QMainWindow::eventFilter(obj, e);
}

// 加载遮罩开关
void MainWindow::showLoading(bool on) {
    if (!m_loadingOverlay) return;
    if (on) {
        m_loadingOverlay->setGeometry(rect());
        m_loadingOverlay->raise();
        m_loadingOverlay->show();
    } else {
        m_loadingOverlay->hide();
    }
}

// 拖拽文件夹到窗口直接打开项目
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() && QFileInfo(e->mimeData()->urls().first().toLocalFile()).isDir()) {
        e->acceptProposedAction();
        m_statusLabel->setText("\U0001F4C2 " + e->mimeData()->urls().first().toLocalFile());
    }
}

void MainWindow::dropEvent(QDropEvent *e) {
    if (!e->mimeData()->hasUrls()) return;
    const QString dir = e->mimeData()->urls().first().toLocalFile();
    if (QFileInfo(dir).isDir()) {
        e->acceptProposedAction();
        openRepo(dir);
    }
}

void MainWindow::resizeEvent(QResizeEvent *e) {
    QMainWindow::resizeEvent(e);
    if (m_loadingOverlay && m_loadingOverlay->isVisible())
        m_loadingOverlay->setGeometry(rect());
}

// 页签定位/恢复：存在则切过去，被关闭则插回原位置
void MainWindow::ensureTab(QWidget *w, int pos, const QString &label) {
    const int idx = m_detailTabs->indexOf(w);
    if (idx >= 0) { m_detailTabs->setCurrentIndex(idx); return; }
    const int at = qBound(0, pos, m_detailTabs->count());
    m_detailTabs->insertTab(at, w, label);
    m_detailTabs->setCurrentIndex(at);
}

void MainWindow::collectExpandedDirs(QTreeWidgetItem *item) {
    if (!item) return;
    if (!item->data(0, Qt::UserRole + 1).toString().isEmpty()) {
        const QString rel = item->data(0, Qt::UserRole).toString();
        if (item->isExpanded()) m_expandedDirs.insert(rel);
        else m_expandedDirs.remove(rel);
    }
    for (int i = 0; i < item->childCount(); ++i)
        collectExpandedDirs(item->child(i));
}

// 树重建后按记录恢复展开状态（顶层立即恢复；深层随懒加载级联恢复）
void MainWindow::restoreExpandedDirs() {
    QTreeWidgetItem *root = m_fileTree->topLevelItem(0);
    if (!root) return;
    root->setExpanded(true);
    for (int i = 0; i < root->childCount(); ++i) {
        QTreeWidgetItem *c = root->child(i);
        if (c->data(0, Qt::UserRole + 1).toString().isEmpty()) continue;
        c->setExpanded(m_expandedDirs.contains(c->data(0, Qt::UserRole).toString()));
    }
}

// 展开目录：后台枚举该层子项，完成后按相对路径回填（树重建则丢弃）
void MainWindow::expandDirLazy(QTreeWidgetItem *item) {
    if (!item || item->data(0, Qt::UserRole + 1).toString() != QLatin1String("lazy")) return;
    item->setData(0, Qt::UserRole + 1, QStringLiteral("loading"));
    QStringList parts;
    QTreeWidgetItem *n = item;
    while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
    const QString rel = parts.join('/');
    const QString abs = QDir(m_currentFile).filePath(rel);
    const quint64 gen = ++m_treeGen;
    auto *w = new QFutureWatcher<QFileInfoList>(this);
    connect(w, &QFutureWatcher<QFileInfoList>::finished, this, [this, w, rel, gen] {
        const QFileInfoList entries = w->result();
        w->deleteLater();
        if (gen != m_treeGen) return;                 // 树已重建，丢弃
        QTreeWidgetItem *target = findItemByRel(rel);
        if (!target) return;
        while (target->childCount() > 0) {            // 移除占位符
            auto *c = target->child(0);
            target->removeChild(c);
            delete c;
        }
        for (const QFileInfo &fi : entries) {
            if (fi.fileName() == QLatin1String(".git")) continue;
            auto *it = new QTreeWidgetItem({ fi.fileName() });
            const QString childRel = rel + QLatin1Char('/') + fi.fileName();
            if (fi.isDir()) {
                it->setIcon(0, icons::folderIcon());
                it->setData(0, Qt::UserRole, childRel);
                it->setData(0, Qt::UserRole + 1, QStringLiteral("lazy"));
                auto *ph = new QTreeWidgetItem;
                ph->setFlags(Qt::NoItemFlags);
                it->addChild(ph);
            } else {
                it->setIcon(0, icons::fileIcon(fi.fileName()));
                it->setData(0, Qt::UserRole, childRel);
            }
            target->addChild(it);
        }
        // 记忆展开：子目录此前处于展开状态 → 自动展开（级联触发下一层懒加载）
        for (int k = 0; k < target->childCount(); ++k) {
            QTreeWidgetItem *c = target->child(k);
            if (!c->data(0, Qt::UserRole + 1).toString().isEmpty()
                    && m_expandedDirs.contains(c->data(0, Qt::UserRole).toString()))
                c->setExpanded(true);
        }
    });
    w->setFuture(QtConcurrent::run([abs]() -> QFileInfoList {
        return QDir(abs).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                                       QDir::DirsFirst | QDir::Name);
    }));
}

// 按相对路径在当前树中定位节点（树重建后返回 nullptr，避免悬挂指针）
QTreeWidgetItem *MainWindow::findItemByRel(const QString &rel) {
    QTreeWidgetItem *cur = m_fileTree->topLevelItem(0);
    if (rel.isEmpty()) return cur;
    const QStringList segs = rel.split('/');
    for (const QString &seg : segs) {
        if (!cur) break;
        QTreeWidgetItem *next = nullptr;
        for (int k = 0; k < cur->childCount(); ++k) {
            if (cur->child(k)->text(0) == seg) { next = cur->child(k); break; }
        }
        cur = next;
    }
    return cur;
}

void MainWindow::toggleTerminal() {
    if (!m_terminal) return;
    m_terminal->setVisible(!m_terminal->isVisible());
    if (m_terminal->isVisible()) {
        m_terminal->setRepo(m_currentFile);
        if (auto *le = m_terminal->findChild<QLineEdit *>()) le->setFocus();
    }
}

void MainWindow::showGlobalSearch() {
    if (m_currentFile.isEmpty()) {
        QMessageBox::information(this, i18n::t("menu.grep"), i18n::t("no_project"));
        return;
    }
    QDialog d(this);
    d.setWindowTitle(i18n::t("menu.grep"));
    d.resize(680, 480);
    auto *v = new QVBoxLayout(&d);
    auto *input = new QLineEdit;
    input->setPlaceholderText(i18n::t("find_placeholder"));
    v->addWidget(input);
    auto *list = new QListWidget;
    v->addWidget(list, 1);
    connect(input, &QLineEdit::returnPressed, &d, [this, input, list] {
        list->clear();
        const QString q = input->text().trimmed();
        if (q.isEmpty()) return;
        try {
            const QString out = git()->run({ "grep", "-n", "-I", q }, m_currentFile, false);
            for (const QString &l : out.split('\n', Qt::SkipEmptyParts)) list->addItem(l);
            if (list->count() == 0) list->addItem(i18n::t("no_results"));
        } catch (const std::exception &e) {
            list->addItem(QString::fromUtf8(e.what()));
        }
    });
    connect(list, &QListWidget::itemDoubleClicked, &d, [this, list, &d](QListWidgetItem *it) {
        onFileDoubleClicked(it->text().section(':', 0, 0));
        d.accept();
    });
    d.exec();
}

void MainWindow::createRelease() {
    RepoPanelDialog dlg(this);
    dlg.exec();
}

void MainWindow::openSettingsDialog() {
    SettingsDialog dlg(this);
    connect(&dlg, &SettingsDialog::accountsChanged, this, &MainWindow::updateConnectTitle);
    connect(&dlg, &SettingsDialog::themeChanged, this, [this](const QString &t) {
        theme::setTheme(t);
        theme::applyToApp();
        m_titleBar->applyTheme();
        m_editorPanel->applyTheme();
        m_imageView->setStyleSheet(QString("background-color:%1;").arg(theme::bg()));
    });
    connect(&dlg, &SettingsDialog::languageChanged, this, &MainWindow::retranslateUi);
    dlg.exec();
}

// 连接用户标题：已登录加 ✓（与 Python 版一致）；标题栏居中显示 账户域名；
// 若账户设置了 Token 有效期，账号下方第二行显示剩余天数（≤7 天红色加粗）
void MainWindow::updateConnectTitle() {
    const Account a = acct()->currentAccount();
    m_actions.connectMenu->setTitle(i18n::t("menu.connect") + (a.token.isEmpty() ? QString() : QStringLiteral(" \u2713")));
    if (a.token.isEmpty()) {
        m_titleBar->setGithubLabel(QString());
        return;
    }
    const QString host = a.platform == QLatin1String("gitee")
        ? QStringLiteral("gitee.com/") : QStringLiteral("github.com/");
    const QString link = QStringLiteral("\U0001F517 ") + host + a.username;
    const QDate exp = QDate::fromString(a.expiresAt, Qt::ISODate);
    if (!exp.isValid()) {
        m_titleBar->setGithubAccount(link, QString(), false);
        return;
    }
    const qint64 days = QDate::currentDate().daysTo(exp);
    if (days < 0)
        m_titleBar->setGithubAccount(link, i18n::t("token_expired"), true);
    else if (days <= 7)
        m_titleBar->setGithubAccount(link, i18n::t("token_expires_in").arg(days), true);
    else
        m_titleBar->setGithubAccount(link, i18n::t("token_expires_in").arg(days), false);
}

void MainWindow::openRepoPanel() {
    RepoPanelDialog dlg(this);
    connect(&dlg, &RepoPanelDialog::repoCloned, this, &MainWindow::openRepo);
    dlg.exec();
    updateConnectTitle();
}

void MainWindow::showShortcuts() {
    QMessageBox::information(this, i18n::t("shortcut_title"),
        "Ctrl+O  " + i18n::t("sc_open") + "\n"
        "Ctrl+S  " + i18n::t("sc_save") + "\n"
        "Ctrl+F  " + i18n::t("sc_find") + "\n"
        "Ctrl+R  " + i18n::t("run_code") + "\n"
        "Ctrl+Q  " + i18n::t("sc_quit") + "\n\n" +
        i18n::t("sc_findbar") + "\n"
        "  Enter  " + i18n::t("sc_next") + "\n"
        "  Shift+Enter  " + i18n::t("sc_prev") + "\n"
        "  Esc  " + i18n::t("sc_close_find") + "\n");
}

void MainWindow::showManual() {
    ManualDialog dlg(this);
    dlg.exec();
}

void MainWindow::showAbout() {
    QDialog d(this);
    d.setWindowTitle(i18n::t("about_title"));
    d.setMinimumSize(720, 640);
    auto *layout = new QVBoxLayout(&d);
    auto *lbl = new QLabel(QStringLiteral(
        "<h1>GitFlow</h1><p>%1</p><p>%2</p>"
        "<p><a href='https://github.com/mosunand/GitFlow'>github.com/mosunand/GitFlow</a><br>"
        "<a href='mailto:moshuai1013@outlook.com'>moshuai1013@outlook.com</a></p>")
        .arg(tr("C++ / Qt implementation"), tr("Author: mosunand")));
    lbl->setOpenExternalLinks(true);
    layout->addWidget(lbl);
    auto *closeBtn = new QPushButton(i18n::t("close"));
    connect(closeBtn, &QPushButton::clicked, &d, &QDialog::accept);
    layout->addWidget(closeBtn);
    d.exec();
}

void MainWindow::retranslateUi() {
    auto &a = m_actions;
    a.connectMenu->setTitle(i18n::t("menu.connect"));
    a.fileMenu->setTitle(i18n::t("menu.file"));
    a.open->setText(i18n::t("menu.open"));
    a.init->setText(i18n::t("menu.init"));
    a.quit->setText(i18n::t("menu.quit"));
    a.gitMenu->setTitle(i18n::t("menu.git"));
    a.searchRepo->setText(i18n::t("menu.search_repo"));
    a.pull->setText(i18n::t("menu.pull"));
    a.push->setText(i18n::t("menu.push"));
    a.stashSave->setText(i18n::t("menu.stash_save"));
    a.stashPop->setText(i18n::t("menu.stash_pop"));
    a.stashList->setText(i18n::t("menu.stash_list"));
    a.tagCreate->setText(i18n::t("menu.tag_create"));
    a.tagList->setText(i18n::t("menu.tag_list"));
    a.grep->setText(i18n::t("menu.grep"));
    a.createRelease->setText(i18n::t("create_release"));
    a.helpMenu->setTitle(i18n::t("menu.help"));
    a.shortcut->setText(i18n::t("menu.shortcut"));
    a.manual->setText(i18n::t("menu.manual"));
    a.about->setText(i18n::t("menu.about"));
    a.terminal->setText(i18n::t("terminal"));
    a.settings->setText(i18n::t("menu.settings"));
    m_openBtn->setText(i18n::t("open_project"));
    m_refreshBtn->setText(i18n::t("refresh"));
    m_refreshBtn->setToolTip(i18n::t("refresh") + " (F5)");
    m_runBtn->setText(i18n::t("run_code"));
    m_runBtn->setToolTip(i18n::t("run_code"));
    m_addFileBtn->setText("+ " + i18n::t("add_file"));
    m_newFileBtn->setText("+ " + i18n::t("new_file"));
    m_newBranchBtn->setText("+ " + i18n::t("new_branch"));
    m_commitBtn->setText(i18n::t("commit_btn"));
    m_commitPushBtn->setText(i18n::t("commit_push_btn"));
    m_commitInput->setPlaceholderText(i18n::t("commit_placeholder"));
    m_changesTitle->setText("  \U0001F4DD " + i18n::t("changes"));
    m_changeTree->setHeaderLabels({ i18n::t("file"), i18n::t("status_col") });
    m_detailTabs->setTabText(0, i18n::t("editor"));
    if (m_detailTabs->indexOf(m_diffEdit) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_diffEdit), "Diff");
    if (m_detailTabs->indexOf(m_historyGroup) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_historyGroup), i18n::t("history"));
    if (m_detailTabs->indexOf(m_graphEdit) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_graphEdit), i18n::t("tab_graph"));
    m_detailTabs->setTabText(3, i18n::t("tab_graph"));
    m_historyGroup->setTitle(i18n::t("commit_history"));
    if (m_repoNameLabel->text().startsWith("\U0001F4C1") == false)
        m_repoNameLabel->setText(i18n::t("no_project"));
    m_statusLabel->setText(i18n::t("ready"));
    updateConnectTitle();   // 语言切换后刷新账户区（含 Token 倒计时文案）
}
