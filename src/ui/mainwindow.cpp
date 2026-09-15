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
#include <QTextCharFormat>
#include <QTextCursor>
#include <QColor>
#include <QDirIterator>
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
#include <QProgressDialog>
#include <QEventLoop>
#include <QImageReader>
#include <QMenu>
#include <QProcess>
#include <QStatusBar>
#include <QHeaderView>
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

// Diff/blame 着色回显：+ 行绿、- 行红、@@ 行蓝（模拟终端 diff 配色）
void setColoredDiff(QPlainTextEdit *edit, const QString &text) {
    edit->clear();
    QTextCursor cur = edit->textCursor();
    QTextCharFormat add, del, hunk, norm;
    add.setForeground(QColor("#3fb950"));
    del.setForeground(QColor("#e5534b"));
    hunk.setForeground(QColor("#58a6ff"));
    for (const QString &line : text.split('\n')) {
        const QTextCharFormat *f = &norm;
        if (line.startsWith('+')) f = &add;
        else if (line.startsWith('-')) f = &del;
        else if (line.startsWith("@@")) f = &hunk;
        cur.insertText(line + '\n', *f);
    }
    edit->setTextCursor(cur);
}



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
// 文件树：支持把外部文件/文件夹拖到指定目录节点上。
// QTreeWidget 默认不处理外部 URL 拖放，事件会冒泡到主窗口的 dropEvent，
// 而那里只认仓库根 —— 于是拖到哪个目录上都只会落在根目录
class DropTree : public QTreeWidget {
public:
    // (被拖入的本地路径列表, 目标目录相对路径；空串=仓库根)
    std::function<void(const QStringList &, const QString &)> onDrop;
    // 拖拽过程中提示落点（rel 为空串 = 仓库根），以及拖拽离开
    std::function<void(const QString &)> onHoverTarget;
    std::function<void()> onHoverEnd;

    explicit DropTree(QWidget *parent = nullptr) : QTreeWidget(parent) {
        setAcceptDrops(true);
        setDragDropMode(QAbstractItemView::DropOnly);   // 只接收拖入，不做内部拖拽移动
    }

protected:
    // 外部 URL 拖放自己接管：交给基类处理时，QAbstractItemView 会因模型不认
    // uri-list 而 ignore，接受状态拿不回来
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (e->mimeData()->hasUrls()) { e->acceptProposedAction(); return; }
        QTreeWidget::dragEnterEvent(e);
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (!e->mimeData()->hasUrls()) { QTreeWidget::dragMoveEvent(e); return; }
        // 实时高亮落点：把目标目录设为当前项（复用选中样式），并把路径报给状态栏。
        // 不给这个反馈，用户根本不知道松手后会放进哪个目录
        QTreeWidgetItem *it = itemAt(e->position().toPoint());
        if (it != m_hot) {
            m_hot = it;
            if (m_hot) setCurrentItem(m_hot);
            if (onHoverTarget) onHoverTarget(dirRelOf(m_hot));
        }
        e->acceptProposedAction();
    }
    void dragLeaveEvent(QDragLeaveEvent *e) override {
        m_hot = nullptr;
        if (onHoverEnd) onHoverEnd();
        QTreeWidget::dragLeaveEvent(e);
    }
    void dropEvent(QDropEvent *e) override {
        if (!e->mimeData()->hasUrls()) { QTreeWidget::dropEvent(e); return; }
        QStringList paths;
        for (const QUrl &u : e->mimeData()->urls()) {
            const QString local = u.toLocalFile();
            if (!local.isEmpty()) paths << local;
        }
        const QString base = dirRelOf(m_hot ? m_hot : itemAt(e->position().toPoint()));
        m_hot = nullptr;
        if (onHoverEnd) onHoverEnd();
        if (paths.isEmpty()) { e->ignore(); return; }
        e->acceptProposedAction();
        if (onDrop) onDrop(paths, base);
    }

private:
    // 节点对应的目录：目录节点=它本身，文件节点=其所在目录，根节点/空白=仓库根("")
    QString dirRelOf(QTreeWidgetItem *item) const {
        if (!item) return {};
        QStringList parts;
        QTreeWidgetItem *n = item;
        while (n && n->parent()) { parts.prepend(n->text(0)); n = n->parent(); }
        const QString rel = parts.join('/');
        if (rel.isEmpty()) return {};
        if (!item->data(0, Qt::UserRole + 1).toString().isEmpty()) return rel;   // 目录
        const int slash = rel.lastIndexOf('/');
        return slash < 0 ? QString() : rel.left(slash);
    }
    QTreeWidgetItem *m_hot = nullptr;
};

// 只读等宽面板（Diff / 分支图）的内联样式，主题切换时要重刷，故抽出来共用
QString monoReadOnlyQss(const char *widget) {
    return QStringLiteral("%1{background-color:%2;color:%3;border:1px solid %4;"
                          "font-family:'Consolas','Courier New',monospace;font-size:10pt;}")
        .arg(QLatin1String(widget), theme::bg(), theme::text(), theme::border());
}

AccountService *acct() { static AccountService s; return &s; }

// 用户输入的名字若写成绝对路径（D:/x、/x 等），QDir::filePath 会原样返回，
// 于是文件/文件夹被建到仓库外面，刷新后仓库里什么都看不到
bool insideRepo(const QString &repo, const QString &full) {
    const QString root = QDir::cleanPath(QDir(repo).absolutePath());
    return QDir::cleanPath(full).startsWith(root + QLatin1Char('/'));
}
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
    // push 三阶段的中间进程（fetch/计数/变基）也要终止，
    // 否则窗口析构后 git 子进程会变成孤儿进程残留
    const auto kids = findChildren<QProcess *>();
    for (QProcess *p : kids) {
        if (p != m_pushProcess && p->state() == QProcess::Running)
            p->kill();
    }
    e->accept();
}

// ─────────────── menu ───────────────
void MainWindow::rebuildMenus() { /* placeholder */ }

void MainWindow::buildMenu() {
    QMenuBar *mb = menuBar();
    auto &a = m_actions;

    a.connectMenu = mb->addMenu(i18n::t("menu.connect"));
    a.connectRepos = a.connectMenu->addAction(i18n::t("repo_search_ph"));
    connect(a.connectRepos, &QAction::triggered, this, &MainWindow::openRepoPanel);

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
    m_newFolderBtn = new QPushButton("+ " + i18n::t("new_folder"));
    connect(m_newFolderBtn, &QPushButton::clicked, this, &MainWindow::createFolderDialog);
    lh->addWidget(m_newFolderBtn);
    m_newBranchBtn = new QPushButton("+ " + i18n::t("new_branch"));
    connect(m_newBranchBtn, &QPushButton::clicked, this, &MainWindow::createBranchDialog);
    lh->addWidget(m_newBranchBtn);
    ll->addLayout(lh);

    auto *lsplit = new QSplitter(Qt::Vertical);
    auto *tree = new DropTree;
    tree->onDrop = [this](const QStringList &paths, const QString &baseRel) {
        importIntoRepo(paths, baseRel);
    };
    // 拖拽中在状态栏写明落点（并把原位文案暂存，松手/离开后还原）
    tree->onHoverTarget = [this](const QString &rel) {
        if (!m_hoverStatusSaved) {
            m_statusBeforeHover = m_statusLabel->text();
            m_hoverStatusSaved = true;
        }
        const QString where = rel.isEmpty() ? QDir(m_currentFile).dirName() : rel;
        m_statusLabel->setText("\U0001F4C2 " + i18n::t("drop_target_hint").arg(where));
    };
    tree->onHoverEnd = [this] {
        if (!m_hoverStatusSaved) return;
        m_statusLabel->setText(m_statusBeforeHover);
        m_hoverStatusSaved = false;
    };
    m_fileTree = tree;
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
    // 图片随控件尺寸重算的去抖定时器（见 eventFilter 的 Resize 分支）
    m_imageFitTimer = new QTimer(this);
    m_imageFitTimer->setSingleShot(true);
    connect(m_imageFitTimer, &QTimer::timeout, this, &MainWindow::applyImageZoom);
    m_imageView->installEventFilter(this);
    m_detailTabs->addTab(m_editorHost, i18n::t("editor"));
    m_diffEdit = new QPlainTextEdit;
    m_diffEdit->setReadOnly(true);
    m_diffEdit->setFont(QFont("Consolas", 10));
    m_diffEdit->setStyleSheet(monoReadOnlyQss("QPlainTextEdit"));
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
        auto *rowItem = m_historyList->item(row);
        if (!rowItem) return;
        const QVariant d = rowItem->data(Qt::UserRole);
        if (!d.isValid()) return;
        const QVariantMap m = d.toMap();
        // 不能用固定下标：Diff 页签可被关闭/移位，indexOf 才能定位到真正的差异页
        ensureTab(m_diffEdit, 1, QStringLiteral("Diff"));
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
            setColoredDiff(m_diffEdit, out);
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
    m_graphEdit->setStyleSheet(monoReadOnlyQss("QTextEdit"));
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
            // openRepo 会被拖放处理器和面板信号直接调用，异常逃出去就是未定义行为
            try {
                git()->init(path);
            } catch (const std::exception &e) {
                QMessageBox::critical(this, i18n::t("init_failed"), e.what());
                return;
            }
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
            // 无论结果是否过期都要收遮罩：丢弃旧结果≠不需要收尾，
            // 否则新任务只刷分支/历史时，旧的 status 任务会把遮罩永远挂住
            if (kind == QLatin1Char('s')) showLoading(false);
            const RefreshData d = w->result();
            w->deleteLater();
            if (gen != m_refreshGen) return;   // 已有更新的刷新，丢弃过期结果
            applyRefresh(d);
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
        // 当前分支来自 status 任务，而 branch/status 是两个并行任务、到达顺序不定，
        // 所以这里读 m_currentBranch（原实现只读同一个结果里的 d.st.branch，
        // 分支任务里根本没有 status 字段 → 下拉框永远停在字母序第一个分支上）
        const QString want = !d.st.branch.isEmpty() ? d.st.branch : m_currentBranch;
        if (!want.isEmpty()) {
            if (m_branchCombo->findText(want) < 0)
                m_branchCombo->addItem(want);   // 例如分离头指针的 "(detached)"
            m_branchCombo->setCurrentText(want);
        }
        m_branchCombo->blockSignals(false);
    }
    if (d.wantStatus) {
        // 当前分支同步到下拉框：与分支列表谁先到都不影响最终显示
        m_currentBranch = d.st.branch;
        if (!m_currentBranch.isEmpty()) {
            m_branchCombo->blockSignals(true);
            if (m_branchCombo->findText(m_currentBranch) < 0)
                m_branchCombo->addItem(m_currentBranch);
            m_branchCombo->setCurrentText(m_currentBranch);
            m_branchCombo->blockSignals(false);
        }
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
        // 再叠加"刚新建/添加"要露出的目录：必须放在旧状态快照之后，否则会被覆盖掉
        if (!m_revealDirs.isEmpty()) {
            m_expandedDirs.unite(m_revealDirs);
            m_revealDirs.clear();
            restoreExpandedDirs();
        }
        revealPendingItem();
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
                c->setData(0, Qt::UserRole + 1, int(f.status));
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

// 新建/添加的目标目录：跟随文件树当前选中项 —— 选中的是目录就用它本身，
// 选中的是文件就用它所在目录，什么都没选就是仓库根（空串）。
// 三个入口原先一律按仓库根解析，于是在文件夹上右键新建，文件却跑到了根目录
QString MainWindow::selectedTargetDir() const {
    if (!m_fileTree || m_currentFile.isEmpty()) return {};
    const QString rel = selectedFilePath();
    if (rel.isEmpty()) return {};
    if (QFileInfo(QDir(m_currentFile).filePath(rel)).isDir()) return rel;
    const int slash = rel.lastIndexOf('/');
    return slash < 0 ? QString() : rel.left(slash);
}

// 让某个目录及其父链在下次树重建后自动展开。
// 注意不能直接写 m_expandedDirs：applyRefresh 重建树之前会先用 collectExpandedDirs
// 把"旧树"的展开状态快照进 m_expandedDirs，而刚建好、尚未展开的目录在那一步会被
// remove 掉 —— 展开请求就丢了，文件建在折叠目录里、界面上看不见（用户以为没建成）
void MainWindow::markDirsExpanded(const QString &relDir) {
    QString acc;
    for (const QString &seg : relDir.split('/', Qt::SkipEmptyParts)) {
        acc = acc.isEmpty() ? seg : acc + QLatin1Char('/') + seg;
        m_revealDirs.insert(acc);
    }
}

// 树建好/某层目录展开后，把"刚新建/添加"的那一项选中并滚到可见位置。
// 找不到就返回（所在目录还没加载到那一层，等下一层展开回调再调一次）
void MainWindow::revealPendingItem() {
    if (m_revealPath.isEmpty()) return;
    QTreeWidgetItem *it = findItemByRel(m_revealPath);
    if (!it) return;
    m_fileTree->setCurrentItem(it);
    m_fileTree->scrollToItem(it);
    m_revealPath.clear();
}

void MainWindow::onFileDoubleClicked(const QString &path) {    if (path.isEmpty()) return;
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
    const bool knownTextName = textNames.contains(baseName);
    bool isText = texts.contains(suffix) || knownTextName;
    if (suffix.isEmpty() && !knownTextName) {
        // 无扩展名：先嗅探再当文本，直接当文本会把二进制乱码灌进编辑器
        QFile probe(full);
        if (probe.open(QIODevice::ReadOnly)) {
            const QByteArray head = probe.read(8192);
            probe.close();
            isText = !head.contains('\0');   // 含 NUL 基本可断定是二进制
        } else {
            isText = false;
        }
    }
    if (images.contains(suffix)) {
        // 先只读头部拿尺寸（不解码像素）：超大图直接拒开，否则解码会长时间卡住界面
        QImageReader rd(full);
        const QSize sz = rd.size();
        if (sz.isValid() && qint64(sz.width()) * qint64(sz.height()) > 100000000LL) {
            QMessageBox::warning(this, i18n::t("file_too_large"),
                                 i18n::t("img_too_large_body").arg(sz.width()).arg(sz.height()));
            return;
        }
        QPixmap pm(full);
        if (pm.isNull()) { QMessageBox::warning(this, path, i18n::t("img_load_failed")); return; }
        m_imagePix = pm;
        m_imageZoom = 1.0;
        applyImageZoom();
        m_editorStack->setCurrentWidget(m_imageView);
        ensureTab(m_editorHost, 0, i18n::t("editor"));
        const int edIdx = m_detailTabs->indexOf(m_editorHost);
        if (edIdx >= 0) m_detailTabs->setTabText(edIdx, "\U0001F5BC " + path);
        return;
    }
    if (!isText) {
        showDiffForFile(path);
        return;
    }
    // 内置编辑器不适合超大文本：QPlainTextEdit 载入几十 MB 会卡住界面，
    // 而且编辑器是可写的，截断后保存会毁掉原文件 —— 直接拒绝打开更安全
    const qint64 size = QFileInfo(full).size();
    if (size > 20LL * 1024 * 1024) {
        QMessageBox::warning(this, i18n::t("file_too_large"),
                             i18n::t("file_too_large_body")
                                 .arg(QString::number(size / 1024.0 / 1024.0, 'f', 1)));
        return;
    }
    QFile f(full);
    if (!f.open(QIODevice::ReadOnly)) return;
    // 编辑器是单文档：打开新文件会覆盖当前内容，有未保存修改时先确认，避免静默丢改动
    if (m_editorPanel->isModified() && !m_editorPanel->openPath().isEmpty()
        && m_editorPanel->openPath() != full) {
        QMessageBox box(this);
        box.setWindowTitle(i18n::t("hint"));
        box.setIcon(QMessageBox::Warning);
        box.setText(i18n::t("unsaved_switch_q"));
        auto *saveBtn = box.addButton(i18n::t("save"), QMessageBox::AcceptRole);
        auto *dropBtn = box.addButton(i18n::t("discard_btn"), QMessageBox::DestructiveRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        const QAbstractButton *clicked = box.clickedButton();
        if (clicked == saveBtn) saveCurrentEditor();
        else if (clicked != dropBtn) return;   // 取消或直接关窗
    }
    const QByteArray raw = f.readAll();
    m_editorPanel->setPlainText(QString::fromUtf8(raw));
    // 记住原文件用的是 LF 还是 CRLF，保存时按原样写回
    m_editorPanel->setLineEnding(raw.contains("\r\n") ? QStringLiteral("\r\n")
                                                     : QStringLiteral("\n"));
    m_editorStack->setCurrentWidget(m_editorPanel);
    if (m_detailTabs->indexOf(m_editorHost) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_editorHost), i18n::t("editor"));
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

    // 仅当运行的就是编辑器里打开的文件且有未保存修改时才提醒，
    // 右键运行树上其他文件与编辑器内容无关，不该误报
    if (file == m_editorPanel->openPath() && m_editorPanel->isModified()) {
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
    menu.addAction(i18n::t("new_folder"), this, &MainWindow::createFolderDialog);
    menu.addAction(i18n::t("add_file_menu"), this, &MainWindow::addFileDialog);
    if (QFileInfo(full).isFile()) {
        menu.addAction(i18n::t("run_code"), this, [this, full] { runCurrentFile(full); });
        menu.addSeparator();
        menu.addAction(i18n::t("view_diff"), this, [this, path] { showDiffForFile(path); });
        menu.addAction(i18n::t("view_blame"), this, [this, path] {
            try {
                setColoredDiff(m_diffEdit, git()->blame(m_currentFile, path));
                ensureTab(m_diffEdit, 1, QStringLiteral("Diff"));
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
    const QString base = selectedTargetDir();
    QFileDialog dlg(this, i18n::t("pick_files"), m_currentFile);
    dlg.setFileMode(QFileDialog::ExistingFiles);
    dlg.setOption(QFileDialog::DontUseNativeDialog, true);
    localizeFileDialog(dlg);
    if (dlg.exec() != QDialog::Accepted) return;
    const QStringList paths = dlg.selectedFiles();
    int done = 0;
    QStringList added;
    for (const QString &src : paths) {
        const QString name = QFileInfo(src).fileName();
        const QString rel = base.isEmpty() ? name : base + QLatin1Char('/') + name;
        // 目标同名时 copy 会失败，计数后如实告知
        if (QFile::copy(src, QDir(m_currentFile).filePath(rel))) {
            ++done;
            added << rel;
        }
    }
    if (done > 0) {
        // 与拖放导入保持同一语义：添加即暂存，否则两个入口一个要手动 add 一个不用
        try {
            git()->add(m_currentFile, added);
            m_revealPath = added.first();
            markDirsExpanded(base);
            refreshStatus();
            m_statusLabel->setText("✅ " + i18n::t("added_body").arg(done)
                                   + QStringLiteral("  (%1)").arg(i18n::t("dropped_staged_short")));
        } catch (const std::exception &ex) {
            QMessageBox::critical(this, i18n::t("error"), ex.what());
        }
    } else {
        refreshStatus();
        m_statusLabel->setText("⚠ " + i18n::t("dropped_none")
                               + QStringLiteral("  (%1)").arg(i18n::t("file_exists")));
    }
}

void MainWindow::createFileDialog() {
    if (m_currentFile.isEmpty()) return;
    const QString base = selectedTargetDir();
    // 目标目录写进对话框文案，避免"不知道会建在哪"的猜测
    const QString where = base.isEmpty() ? QDir(m_currentFile).dirName() : base;
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("new_file_title"),
                                               i18n::t("file_name_label").arg(where),
                                               QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    const QString rel = base.isEmpty() ? name.trimmed() : base + QLatin1Char('/') + name.trimmed();
    const QString full = QDir(m_currentFile).filePath(rel);
    if (!insideRepo(m_currentFile, full)) {
        QMessageBox::warning(this, i18n::t("hint"), i18n::t("path_outside_repo"));
        return;
    }
    if (QFileInfo::exists(full)) { QMessageBox::warning(this, i18n::t("file_exists"), i18n::t("file_exists_body")); return; }
    QDir().mkpath(QFileInfo(full).path());
    QFile f(full);
    f.open(QIODevice::WriteOnly);
    f.close();
    m_revealPath = rel;          // 刷新后展开所在目录并选中这个新文件
    markDirsExpanded(base);
    refreshStatus();
    m_editorPanel->setPlainText({});
    m_editorPanel->setLineEnding(QStringLiteral("\n"));   // 新文件统一用 LF
    m_editorStack->setCurrentWidget(m_editorPanel);
    // 必须把"编辑器"页签切到前面：若当前停在 历史/分支图 页签，
    // 编辑器内容虽然换了但界面毫无变化，看起来就像什么都没发生
    ensureTab(m_editorHost, 0, i18n::t("editor"));
    m_editorPanel->setOpenPath(full);
    m_statusLabel->setText(full);
}

// 新建文件夹。Git 只跟踪文件：空文件夹既不会出现在变更列表里，也不会被提交/推送，
// 所以建完必须问一句要不要放 .gitkeep 占位，否则用户会遇到"我建的文件夹提交后没了"
void MainWindow::createFolderDialog() {
    if (m_currentFile.isEmpty()) return;
    const QString base = selectedTargetDir();
    const QString where = base.isEmpty() ? QDir(m_currentFile).dirName() : base;
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("new_folder_title"),
                                               i18n::t("folder_name_label").arg(where),
                                               QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    if (name.trimmed().isEmpty()) return;
    const QString relName = name.trimmed();
    const QString rel = base.isEmpty() ? relName : base + QLatin1Char('/') + relName;
    const QString full = QDir(m_currentFile).filePath(rel);
    if (!insideRepo(m_currentFile, full)) {
        QMessageBox::warning(this, i18n::t("hint"), i18n::t("path_outside_repo"));
        return;
    }
    if (QFileInfo::exists(full)) {
        QMessageBox::warning(this, i18n::t("file_exists"), i18n::t("file_exists_body"));
        return;
    }
    if (!QDir().mkpath(full)) {
        QMessageBox::warning(this, i18n::t("create_failed"), rel);
        return;
    }

    QMessageBox box(this);
    box.setWindowTitle(i18n::t("new_folder_title"));
    box.setIcon(QMessageBox::Question);
    box.setText(i18n::t("empty_folder_hint"));
    auto *keepBtn = box.addButton(i18n::t("folder_keep_placeholder"), QMessageBox::AcceptRole);
    auto *emptyBtn = box.addButton(i18n::t("folder_keep_empty"), QMessageBox::RejectRole);
    box.setDefaultButton(keepBtn);
    box.exec();
    if (box.clickedButton() == keepBtn) {
        QFile f(full + "/.gitkeep");
        if (f.open(QIODevice::WriteOnly)) f.close();   // 空占位文件
    } else if (box.clickedButton() != emptyBtn) {
        return;   // 直接关窗：文件夹已经建好了，只是没放占位文件
    }
    m_revealPath = rel;          // 刷新后展开并选中刚建的文件夹（含新放的 .gitkeep）
    markDirsExpanded(base);
    refreshStatus();
    m_statusLabel->setText("✅ " + i18n::t("folder_created").arg(rel));
}

void MainWindow::showDiffForFile(const QString &path) {
    ensureTab(m_diffEdit, 1, QStringLiteral("Diff"));
    if (m_currentFile.isEmpty()) return;
    m_diffEdit->setPlainText(i18n::t("refreshing"));
    const QString repo = m_currentFile;
    auto *w = new QFutureWatcher<QString>(this);
    connect(w, &QFutureWatcher<QString>::finished, this, [this, w] {
        w->deleteLater();
        setColoredDiff(m_diffEdit, w->result());
    });
    w->setFuture(QtConcurrent::run([repo, path]() -> QString {
        try {
            // 只跑 `git diff` 时，已 git add 的文件（变更在索引里）会得到空输出，
            // 被误报成"没有差异"。工作区无差异时回退到暂存区差异
            QString out = git()->diff(repo, path);
            if (out.isEmpty()) out = git()->diff(repo, path, true);
            return out.isEmpty() ? i18n::t("no_diff") : out;
        }
        catch (const std::exception &e) { return QString::fromUtf8(e.what()); }
    }));
}

void MainWindow::saveCurrentEditor() {
    const QString path = m_editorPanel->openPath();
    if (path.isEmpty()) return;
    QString text = m_editorPanel->text();
    // 按原文件的行尾风格写回：编辑器的文本永远是 \n 分行，
    // 直接落盘会把 CRLF 文件静默改成 LF（反之 QIODevice::Text 会把 LF 改成 CRLF），
    // 对 Git 客户端来说就是"改一个字符 → 整个文件差异"
    text.replace(QLatin1String("\r\n"), QLatin1String("\n"));
    const QString eol = m_editorPanel->lineEnding();
    if (eol != QLatin1String("\n")) text.replace(QLatin1String("\n"), eol);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {   // 不要 Text 标志，见上
        QMessageBox::critical(this, i18n::t("save_failed"),
                              i18n::t("save_failed_body").arg(path, f.errorString()));
        return;   // 保留未保存标记：不能让用户以为已经存上了
    }
    const QByteArray bytes = text.toUtf8();
    const qint64 written = f.write(bytes);
    const bool bad = (written != bytes.size()) || !f.flush() || f.error() != QFileDevice::NoError;
    f.close();
    if (bad) {
        // 磁盘满/无权限时 QFile::write 会短写，原文件已经被截断 ——
        // 不查返回值就会报"已保存"并清掉未保存标记，属于静默丢数据
        QMessageBox::critical(this, i18n::t("save_failed"),
                              i18n::t("save_failed_body").arg(path, f.errorString()));
        return;
    }
    m_editorPanel->setModified(false);
    m_statusLabel->setText("✅ " + path);
    refreshStatus();
}

// ─────────────── git ops ───────────────
// 提交入口（纯提交 / 提交并推送）都要先过大文件闸门：扫描在后台线程跑，绝不阻塞界面
void MainWindow::commit() {
    const QString msg = m_commitInput->toPlainText().trimmed();
    if (msg.isEmpty()) { QMessageBox::information(this, i18n::t("hint"), i18n::t("enter_commit_msg")); return; }
    if (m_currentFile.isEmpty()) return;
    const QString repo = m_currentFile;
    checkOversizedBeforeCommit(repo, [this, repo, msg] { doCommit(repo, msg, false); });
}

void MainWindow::commitAndPush() {
    const QString msg = m_commitInput->toPlainText().trimmed();
    if (msg.isEmpty()) { QMessageBox::information(this, i18n::t("hint"), i18n::t("enter_commit_msg")); return; }
    if (m_currentFile.isEmpty()) return;
    const QString repo = m_currentFile;
    checkOversizedBeforeCommit(repo, [this, repo, msg] { doCommit(repo, msg, true); });
}

// 提交前扫描：命中超限文件则一个都不许进入提交；扫描失败不拦（真有问题 git 自己会报错）
void MainWindow::checkOversizedBeforeCommit(const QString &repo, const std::function<void()> &onClear) {
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    m_statusLabel->setText("⏳ " + i18n::t("oversized_checking"));
    auto *w = new QFutureWatcher<QList<OversizedFile>>(this);
    connect(w, &QFutureWatcher<QList<OversizedFile>>::finished, this, [this, w, onClear] {
        const QList<OversizedFile> big = w->result();
        w->deleteLater();
        // 无论通过与否都要恢复按钮：任何一条分支漏掉都会让界面像卡死
        m_commitBtn->setEnabled(true);
        m_commitPushBtn->setEnabled(true);
        if (big.isEmpty()) {
            onClear();
            return;
        }
        showCommitBlocked(big);
    });
    w->setFuture(QtConcurrent::run([repo]() -> QList<OversizedFile> {
        try { return git()->findOversizedInCommit(repo); }
        catch (...) { return {}; }   // 扫描本身出错不阻断提交
    }));
}

void MainWindow::showCommitBlocked(const QList<OversizedFile> &files) {
    QStringList rows;
    for (const auto &f : files)
        rows << QStringLiteral("<li><code>%1</code> — %2 MB</li>")
                    .arg(f.path.toHtmlEscaped(), QString::number(f.sizeMb, 'f', 1));
    QMessageBox box(this);
    box.setWindowTitle(i18n::t("commit_blocked_title"));
    box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(QStringLiteral("<b>%1</b><ul style='margin:6px 0 6px 18px;'>%2</ul>%3")
                    .arg(i18n::t("commit_blocked_body"), rows.join(QString()),
                         i18n::t("commit_blocked_hint")));
    box.setStandardButtons(QMessageBox::Ok);
    box.exec();
    m_statusLabel->setText("\u274c " + i18n::t("commit_blocked_title"));
}

void MainWindow::doCommit(const QString &repo, const QString &msg, bool thenPush) {
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    m_statusLabel->setText("⏳ " + i18n::t("committing_local"));
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err, thenPush] {
        w->deleteLater();
        m_commitBtn->setEnabled(true);
        m_commitPushBtn->setEnabled(true);
        if (!w->result()) {
            // 没有可提交的内容：给友好提示，而不是把 git 的英文 hint 原样丢出来
            if (err->contains(QLatin1String("nothing to commit"), Qt::CaseInsensitive)) {
                m_statusLabel->setText("\u2139 " + i18n::t("no_changes"));
                return;
            }
            m_statusLabel->setText("❌ " + i18n::t("commit_failed"));
            QMessageBox::critical(this, i18n::t("commit_failed"), *err);
            return;
        }
        m_commitInput->setPlainText(i18n::t("default_commit_msg"));
        m_historyLoadedFor.clear();
        if (thenPush) {
            // 提交落盘后再推送，避免 push 走的是旧历史
            push();
        } else {
            m_statusLabel->setText("✅ " + i18n::t("commit_success"));
            startRefresh(false, true, false);
        }
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
    if (m_currentFile.isEmpty()) return;
    const Account a = acct()->currentAccount();
    const QString repo = m_currentFile;
    m_statusLabel->setText("⏳ " + i18n::t("pulling"));
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err] {
        w->deleteLater();
        if (!w->result()) {
            m_statusLabel->setText("❌ " + i18n::t("pull_failed"));
            QMessageBox::critical(this, i18n::t("pull_failed"), *err);
            return;
        }
        m_statusLabel->setText("✅ " + i18n::t("pull_success"));
        refreshStatus(); refreshHistory();
    });
    w->setFuture(QtConcurrent::run([repo, a, err]() -> bool {
        try {
            git()->pull(repo, a.token, a.username);
            return true;
        } catch (const std::exception &e) {
            *err = QString::fromUtf8(e.what());
            return false;
        }
    }));
}

// 推送入口：后台扫描大文件（不卡 UI），确认后进入 doPush
void MainWindow::push() {
    if (m_currentFile.isEmpty()) { doPush(); return; }
    const QString repo = m_currentFile;
    auto *w = new QFutureWatcher<QList<OversizedFile>>(this);
    connect(w, &QFutureWatcher<QList<OversizedFile>>::finished, this, [this, w, repo] {
        const QList<OversizedFile> big = w->result();
        w->deleteLater();
        if (big.isEmpty()) { doPush(); return; }
        // 分组：工作区现存 vs 已进历史（删除文件对后者无效，必须重写历史）
        QStringList curList, histList;
        for (const auto &f : big) {
            const QString row = QStringLiteral("<li><code>%1</code> — %2 MB</li>")
                                    .arg(f.path.toHtmlEscaped(),
                                         QString::number(f.sizeMb, 'f', 1));
            if (f.source == QLatin1String("current")) curList << row;
            else histList << row;
        }
        QString body = QStringLiteral("<b>%1</b><br>%2")
                           .arg(i18n::t("oversized_title"), i18n::t("oversized_body"));
        if (!curList.isEmpty())
            body += QStringLiteral("<br><br><b>%3</b><ul style='margin:6px 0 6px 18px;'>%4</ul>%5")
                        .arg(i18n::t("oversized_cur_group"), curList.join(QString()),
                             i18n::t("oversized_cur_hint"));
        if (!histList.isEmpty())
            body += QStringLiteral("<br><br><b style='color:#e5534b;'>%3</b>"
                                   "<ul style='margin:6px 0 6px 18px;'>%4</ul>%5")
                        .arg(i18n::t("oversized_hist_group"), histList.join(QString()),
                             i18n::t("oversized_hist_hint"));
        QMessageBox box(this);
        box.setWindowTitle(i18n::t("oversized_title"));
        box.setIcon(QMessageBox::Warning);
        box.setTextFormat(Qt::RichText);
        box.setText(body);
        // 历史大文件：给"重写历史清除"一键方案；否则只有 仍要推送/取消
        QPushButton *purgeBtn = nullptr;
        QString softAnchor;   // 非空=可走软回退（大文件仅在最近未推送提交中）
        if (!histList.isEmpty()) {
            // 判断条件：所有历史大文件的首个提交都在远程已含提交之后
            //（即大文件只存在于未推送的提交里 → 软回退即可，无需重写历史）
            QString upstreamBase;
            try { upstreamBase = git()->run({ "merge-base", "HEAD", "@{u}" }, repo, false); }
            catch (...) {}
            // 没有上游（首次推送）时 HEAD 上的一切都还没到远程，软回退同样安全，
            // 所以基准是"可以软回退"，再由下面的循环把不安全的情况排除掉
            bool allRecent = true;
            for (const auto &f : big) {
                if (f.source != QLatin1String("history")) continue;
                // isAncestorOrEqual(a,b) = "a 是 b 的祖先"：首提是远程基点的祖先（或就是它）
                // 说明该大文件早已推送到远程 → 软回退会改写已发布历史，只能重写历史
                if (f.firstCommit.isEmpty()
                    || (!upstreamBase.isEmpty()
                        && git()->isAncestorOrEqual(repo, f.firstCommit, upstreamBase))) {
                    allRecent = false;
                    break;
                }
            }
            if (allRecent) {
                // 锚点取所有大文件 firstCommit 中"最早"的：
                // 回退必须落在最远那个引入点之前，才能覆盖全部大文件
                for (const auto &f : big) {
                    if (f.source != QLatin1String("history")) continue;
                    if (softAnchor.isEmpty()
                        || git()->isAncestorOrEqual(repo, f.firstCommit, softAnchor))
                        softAnchor = f.firstCommit;
                }
                if (softAnchor.isEmpty()) allRecent = false;   // 理论不可达（firstCommit 均非空才走到这）
            }
            purgeBtn = box.addButton(i18n::t(softAnchor.isEmpty() ? "oversized_purge_btn"
                                                                  : "oversized_soft_btn"),
                                     QMessageBox::DestructiveRole);
        }
        auto *anywayBtn = box.addButton(i18n::t("push_anyway"), QMessageBox::YesRole);
        box.addButton(QMessageBox::Cancel);
        box.exec();
        const QAbstractButton *clicked = box.clickedButton();
        if (clicked == purgeBtn) {
            if (!softAnchor.isEmpty()) {
                // 软回退：确认文案更温和（不重写历史，不产生 force push）
                if (QMessageBox::question(this, i18n::t("oversized_soft_btn"),
                                          i18n::t("oversized_soft_confirm")) == QMessageBox::Yes)
                    softResetPurgeAndPush(repo, big, softAnchor);
            } else {
                purgeOversizedHistory(repo, big);
            }
            return;
        }
        if (clicked == anywayBtn) doPush();
    });
    // 工作函数必须自己兜住异常：QFuture::result() 在任务抛出时会重新抛出，
    // 而异常从槽里逃逸等于 std::terminate（git 起不来/超时就会走到这里）
    w->setFuture(QtConcurrent::run([repo]() -> QList<OversizedFile> {
        try { return git()->findOversizedFiles(repo); }
        catch (...) { return {}; }   // 扫不了就放行，push 自己会报真正的错
    }));
}

// 软回退清理（大文件仅在最近未推送提交中）：回退到锚点父提交→剔除大文件→
// 之后的改动重新打包为一个提交→自动继续推送。秒级完成，不改更早历史。
void MainWindow::softResetPurgeAndPush(const QString &repo, const QList<OversizedFile> &big,
                                        const QString &anchor) {
    QList<OversizedFile> hist;
    for (const auto &f : big)
        if (f.source == QLatin1String("history")) hist << f;
    m_statusLabel->setText("⏳ " + i18n::t("oversized_purging"));
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err, repo] {
        w->deleteLater();
        m_commitBtn->setEnabled(true);
        m_commitPushBtn->setEnabled(true);
        if (!w->result()) {
            m_statusLabel->setText("❌ " + i18n::t("oversized_purge_failed"));
            QMessageBox::critical(this, i18n::t("oversized_soft_btn"), *err);
            return;
        }
        m_statusLabel->setText("✅ " + i18n::t("oversized_purged"));
        m_historyLoadedFor.clear();
        refreshStatus();
        push();   // 干净了，自动回到推送流程
    });
    w->setFuture(QtConcurrent::run([repo, hist, anchor, err]() -> bool {
        try {
            const QString e = git()->softResetPurge(repo, hist, anchor);
            *err = e;
            return e.isEmpty();
        } catch (const std::exception &e) {
            *err = QString::fromUtf8(e.what());
            return false;
        }
    }));
}

// 一键重写历史剥除大文件：filter-branch + reflog/gc 回收，完成后重扫并继续推送
void MainWindow::purgeOversizedHistory(const QString &repo, const QList<OversizedFile> &big) {
    QList<OversizedFile> hist;
    QStringList paths;
    for (const auto &f : big)
        if (f.source == QLatin1String("history")) { hist << f; paths << f.path; }
    if (hist.isEmpty()) { doPush(); return; }
    // 强确认：重写不可逆、提交哈希全变、已推送的仓库会分叉
    QMessageBox box(this);
    box.setWindowTitle(i18n::t("oversized_purge_btn"));
    box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(i18n::t("oversized_purge_confirm").arg(paths.join(", ")));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;

    m_statusLabel->setText("⏳ " + i18n::t("oversized_purging"));
    m_commitBtn->setEnabled(false);
    m_commitPushBtn->setEnabled(false);
    auto err = std::make_shared<QString>();
    auto *w = new QFutureWatcher<bool>(this);
    connect(w, &QFutureWatcher<bool>::finished, this, [this, w, err, repo] {
        w->deleteLater();
        m_commitBtn->setEnabled(true);
        m_commitPushBtn->setEnabled(true);
        if (!w->result()) {
            m_statusLabel->setText("❌ " + i18n::t("oversized_purge_failed"));
            QMessageBox::critical(this, i18n::t("oversized_purge_btn"), *err);
            return;
        }
        m_statusLabel->setText("✅ " + i18n::t("oversized_purged"));
        m_historyLoadedFor.clear();
        refreshStatus();
        // 清理完成自动回到推送流程（此时应无大文件，直接进入确认/doPush）
        push();
    });
    w->setFuture(QtConcurrent::run([repo, hist, err]() -> bool {
        try {
            const QString e = git()->purgeFilesFromHistory(repo, hist);
            *err = e;
            return e.isEmpty();
        } catch (const std::exception &e) {   // 别让异常逃到 QFuture::result()
            *err = QString::fromUtf8(e.what());
            return false;
        }
    }));
}

// 推送三阶段：① fetch 检测远程分叉 → ② 需要时引导变基整合 → ③ 真正推送。
// 之前直接 push，远程有新提交（网页端改动/其他设备）时被拒，用户只看到 git 原文。
void MainWindow::doPush() {
    if (m_pushProcess) { delete m_pushProcess; m_pushProcess = nullptr; }
    const Account a = acct()->currentAccount();
    // git 起不来时 finished 永不触发，进度弹窗会永久挂在屏幕上（无任何反馈）
    if (git()->gitPath().isEmpty()) {
        m_statusLabel->setText("\u274c " + i18n::t("push_failed"));
        QMessageBox::critical(this, i18n::t("push_failed"), i18n::t("git_not_found"));
        return;
    }

    m_progressDlg = new ProgressDialog(i18n::t("pushing"), this);
    m_progressDlg->show();
    m_progressDlg->raise();
    m_progressDlg->activateWindow();

    // ── 阶段①：fetch（失败如离线则忽略，交给 push 自行报错）──
    auto *fetch = new QProcess(this);
    fetch->setProcessEnvironment(GitService::askpassEnv(a.token, a.username));
    fetch->setWorkingDirectory(m_currentFile);
    connect(fetch, &QProcess::errorOccurred, this, [this](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart) return;   // 起不来才处理，其余交给 finished
        if (m_progressDlg) { m_progressDlg->deleteLater(); m_progressDlg = nullptr; }
        m_statusLabel->setText("\u274c " + i18n::t("push_failed"));
        QMessageBox::critical(this, i18n::t("push_failed"), i18n::t("git_not_found"));
    });
    connect(fetch, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, fetch, a](int code, QProcess::ExitStatus) {
        fetch->deleteLater();
        if (code != 0) { startPushProcess(a); return; }
        // ── 阶段②：统计远程领先的提交数 ──
        auto *cnt = new QProcess(this);
        cnt->setProcessEnvironment(GitService::askpassEnv(a.token, a.username));
        cnt->setWorkingDirectory(m_currentFile);
        connect(cnt, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                [this, cnt, a](int, QProcess::ExitStatus) {
            cnt->deleteLater();
            const int behind = QString::fromUtf8(cnt->readAllStandardOutput())
                                   .trimmed().toInt();
            if (behind <= 0) { startPushProcess(a); return; }
            // ── 阶段②b：远程有新提交，引导变基整合 ──
            if (m_progressDlg) { m_progressDlg->deleteLater(); m_progressDlg = nullptr; }
            QMessageBox box(this);
            box.setWindowTitle(i18n::t("pushing"));
            box.setIcon(QMessageBox::Warning);
            box.setText(i18n::t("remote_ahead_msg").arg(behind));
            auto *rebaseBtn = box.addButton(i18n::t("btn_rebase_push"), QMessageBox::YesRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() != rebaseBtn) {
                onPushFailed(i18n::t("push_cancelled_by_user"));
                return;
            }
            auto *reb = new QProcess(this);
            reb->setProcessEnvironment(GitService::askpassEnv(a.token, a.username));
            reb->setWorkingDirectory(m_currentFile);
            connect(reb, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
                    [this, reb, a](int code, QProcess::ExitStatus) {
                const QString reErr = QString::fromUtf8(reb->readAllStandardError());
                reb->deleteLater();
                if (code != 0) {
                    if (m_progressDlg) { m_progressDlg->deleteLater(); m_progressDlg = nullptr; }
                    // 变基失败后仓库停在 rebase 中间态：不提供出口的话，
                    // 之后的提交/切分支全会失败，用户只能去命令行自救
                    QMessageBox box(this);
                    box.setWindowTitle(i18n::t("rebase_failed"));
                    box.setIcon(QMessageBox::Warning);
                    box.setText(i18n::t("rebase_failed") + "\n\n" + reErr.left(1500)
                                + "\n\n" + i18n::t("rebase_abort_hint"));
                    auto *abortBtn = box.addButton(i18n::t("rebase_abort_btn"), QMessageBox::AcceptRole);
                    box.addButton(QMessageBox::Cancel);
                    box.exec();
                    if (box.clickedButton() == abortBtn) {
                        try {
                            git()->run({ "rebase", "--abort" }, m_currentFile, true, 60000);
                            refreshStatus();
                            onPushFailed(i18n::t("rebase_failed") + "\n\n"
                                         + i18n::t("rebase_aborted"));
                        } catch (const std::exception &e) {
                            onPushFailed(QString::fromUtf8(e.what()));
                        }
                        return;
                    }
                    onPushFailed(i18n::t("rebase_failed") + "\n\n" + reErr);
                    return;
                }
                startPushProcess(a);
            });
            reb->start(git()->gitPath(), { "pull", "--rebase" });
        });
        cnt->start(git()->gitPath(), { "rev-list", "--count", "HEAD..@{u}" });
    });
    fetch->start(git()->gitPath(), { "fetch", "--progress", "origin" });
}

// 阶段③：真正执行 push（进度弹窗/实时日志/失败诊断）
void MainWindow::startPushProcess(const Account &a) {
    if (m_pushProcess) { delete m_pushProcess; m_pushProcess = nullptr; }
    if (m_progressDlg) { m_progressDlg->deleteLater(); m_progressDlg = nullptr; }

    m_pushProcess = new QProcess(this);
    m_pushProcess->setProcessEnvironment(GitService::askpassEnv(a.token, a.username));
    m_pushProcess->setWorkingDirectory(m_currentFile);
    // 进度弹窗：实时展示推送百分比/阶段，卡住检测
    m_progressDlg = new ProgressDialog(i18n::t("pushing"), this);
    m_progressDlg->show();
    m_progressDlg->raise();
    m_progressDlg->activateWindow();
    // 一律用捕获的 proc：m_pushProcess 可能已被下一轮推送替换，用成员会读到错误的进程
    auto *proc = m_pushProcess;
    connect(proc, &QProcess::readyReadStandardError, this, [this, proc] {
        // git --progress 的进度行用 \r 原地刷新（一"行"里叠几十次更新），
        // 只按 \n 切会把它们串成一条，百分比取到的还是块里最早的值
        const QStringList lines = QString::fromUtf8(proc->readAllStandardError())
                                      .replace(QLatin1Char('\r'), QLatin1Char('\n'))
                                      .split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &l : lines) onPushProgressLine(l.trimmed());
    });
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError err) {
        if (err != QProcess::FailedToStart || proc != m_pushProcess) return;
        if (m_progressDlg) { m_progressDlg->deleteLater(); m_progressDlg = nullptr; }
        m_statusLabel->setText("\u274c " + i18n::t("push_failed"));
        QMessageBox::critical(this, i18n::t("push_failed"), i18n::t("git_not_found"));
    });
    connect(proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            [this, proc](int code, QProcess::ExitStatus) {
        if (proc != m_pushProcess) return;   // 过期进程，忽略
        if (code == 0) onPushFinished();
        else onPushFailed(QString::fromUtf8(proc->readAllStandardError()));
    });
    // credential.helper 置空：强制走 askpass（应用内 Token）；-u 首推自动建立上游跟踪
    proc->start(git()->gitPath(),
                { "-c", "http.version=HTTP/1.1", "-c", "credential.helper=",
                  "push", "-u", "origin", "--progress" });
}

// ─────────────── push 进度槽 ───────────────
void MainWindow::onPushProgressLine(const QString &line) {
    m_statusLabel->setText("\u23f3 " + i18n::t("pushing"));
    if (m_progressDlg) m_progressDlg->appendLine(line);
}

void MainWindow::onPushFinished() {
    if (m_progressDlg) { m_progressDlg->finishOk(i18n::t("push_success")); m_progressDlg = nullptr; }
    m_statusLabel->setText("\u2705 " + i18n::t("push_success"));
    // 提交并推送链路：push 完成是最后一环，必须恢复提交按钮，否则永久禁用
    m_commitBtn->setEnabled(true);
    m_commitPushBtn->setEnabled(true);
    refreshStatus(); refreshHistory();
}

void MainWindow::onPushFailed(const QString &err) {
    // 兜底翻译：极端时序下仍可能被拒（fetch 后远程又变），把 git 原文换成行动指引
    QString friendly = err;
    if (err.contains(QLatin1String("fetch first"), Qt::CaseInsensitive)
        || err.contains(QLatin1String("[rejected]"))
        || err.contains(QLatin1String("non-fast-forward"), Qt::CaseInsensitive))
        friendly = i18n::t("push_fetch_first") + QStringLiteral("\n\n") + err;
    if (m_progressDlg) { m_progressDlg->finishFail(friendly, GitService::diagnoseNetwork()); m_progressDlg = nullptr; }
    m_statusLabel->setText("\u274c " + i18n::t("push_failed"));
    m_commitBtn->setEnabled(true);
    m_commitPushBtn->setEnabled(true);
}
void MainWindow::deleteSelectedFile() {
    auto *item = m_changeTree->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    if (QMessageBox::question(this, i18n::t("confirm_delete"),
                              i18n::t("delete_q").arg(path)) != QMessageBox::Yes) return;
    try { git()->deleteFile(m_currentFile, path); refreshStatus(); refreshHistory(); }
    catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("delete_failed"), e.what()); }
}

void MainWindow::restoreSelectedFile() {
    auto *item = m_changeTree->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QString path = item->data(0, Qt::UserRole).toString();
    if (path.isEmpty()) return;
    // 未跟踪文件没有"修改前状态"可恢复（git restore 会直接报 pathspec 不匹配），
    // 放弃它的修改只有一种含义：把文件删掉
    if (item->data(0, Qt::UserRole + 1).toInt() == int(GitFileStatus::Untracked)) {
        if (QMessageBox::question(this, i18n::t("discard_changes"),
                                  i18n::t("discard_untracked_q").arg(path)) != QMessageBox::Yes) return;
        if (!QFile::remove(QDir(m_currentFile).filePath(path)))
            QMessageBox::critical(this, i18n::t("delete_failed"), path);
        refreshStatus();
        return;
    }
    if (QMessageBox::question(this, i18n::t("discard_changes"),
                              i18n::t("discard_q").arg(path)) != QMessageBox::Yes) return;
    try { git()->restore(m_currentFile, path); refreshStatus(); }
    catch (const std::exception &e) { QMessageBox::critical(this, i18n::t("restore_failed"), e.what()); }
}

void MainWindow::switchBranch(const QString &name) {
    // "(detached)" 是分离头指针时我们塞进下拉框的展示项，不是真分支
    if (name.isEmpty() || name == QLatin1String("HEAD") || name == QLatin1String("(detached)")
        || m_currentFile.isEmpty())
        return;
    try {
        git()->switchBranch(m_currentFile, name);
        refreshStatus(); refreshHistory();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("switch_failed"), e.what());
        // 切换失败（未提交改动、分支不存在等）时下拉框已经被用户改成目标分支了，
        // 不移回来它就会一直显示一个并没有生效的分支
        m_branchCombo->blockSignals(true);
        m_branchCombo->setCurrentText(m_currentBranch);
        m_branchCombo->blockSignals(false);
    }
}

void MainWindow::createBranchDialog() {
    if (m_currentFile.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("new_branch_t"),
                                               i18n::t("branch_name"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    // git 失败会抛异常：槽函数里逃逸的异常会直接终止进程（重名分支是常见误操作）
    try {
        git()->createBranch(m_currentFile, name.trimmed());
        m_statusLabel->setText("✅ " + i18n::t("branch_created").arg(name.trimmed()));
        refreshBranches();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("switch_failed"), e.what());
    }
}

void MainWindow::stashSave() {
    if (m_currentFile.isEmpty()) return;
    bool ok = false;
    const QString msg = QInputDialog::getText(this, i18n::t("menu.stash_save"),
                                              i18n::t("stash_hint"), QLineEdit::Normal, {}, &ok);
    if (!ok) return;
    try {
        const auto r = git()->stashSave(m_currentFile, msg);
        // "No local changes to save" 退出码是 0：以前这里会照报"已暂存"
        if (r.nothing) {
            m_statusLabel->setText("ℹ " + i18n::t("stash_nothing"));
            return;
        }
        if (!r.ok) {
            m_statusLabel->setText("❌ " + i18n::t("stash_failed"));
            QMessageBox::critical(this, i18n::t("stash_failed"), r.message);
            return;
        }
        m_statusLabel->setText("✅ " + i18n::t("stash_ok"));
        refreshStatus();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("stash_failed"), e.what());
    }
}

void MainWindow::stashPop() {
    if (m_currentFile.isEmpty()) return;
    try {
        const auto r = git()->stashPop(m_currentFile);
        if (!r.ok) {
            // 弹出遇冲突时改动已带标记进工作区、stash 未被删除：
            // 以前这里无条件报"✅ 已弹出"，用户以为成功、一提交才发现全是冲突
            m_statusLabel->setText(r.conflict ? "⚠ " + i18n::t("stash_conflict")
                                              : "❌ " + i18n::t("pop_failed"));
            QMessageBox::warning(this, r.conflict ? i18n::t("stash_conflict") : i18n::t("pop_failed"),
                                 r.message);
            refreshStatus();
            return;
        }
        m_statusLabel->setText("✅ " + i18n::t("stash_popped_t"));
        refreshStatus();
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("pop_failed"), e.what());
    }
}

void MainWindow::showStashList() {
    if (m_currentFile.isEmpty()) return;
    QDialog dlg(this);
    dlg.setWindowTitle(i18n::t("stash_list_t"));
    dlg.setMinimumSize(520, 380);
    auto *layout = new QVBoxLayout(&dlg);
    layout->addWidget(new QLabel(i18n::t("stash_del_hint")));
    auto *list = new QListWidget;
    layout->addWidget(list, 1);
    const std::function<void()> reload = [this, list] {
        list->clear();
        for (const auto &s : git()->stashList(m_currentFile))
            list->addItem(s.ref + "  " + s.subject);
    };
    reload();
    // 文案承诺"双击可删除"，这里把行为补齐（此前提示与实现不一致，双击无任何反应）
    if (list->count() == 0) {
        list->addItem(i18n::t("no_stash"));
    } else {
        connect(list, &QListWidget::itemDoubleClicked, &dlg,
                [this, list, reload](QListWidgetItem *it) {
            const QString ref = it->text().section(' ', 0, 0);
            if (QMessageBox::question(this, i18n::t("delete_stash"),
                                      i18n::t("delete_q").arg(ref)) != QMessageBox::Yes) return;
            try {
                git()->stashDrop(m_currentFile, ref);
                reload();
            } catch (const std::exception &e) {
                QMessageBox::critical(this, i18n::t("delete_stash"), e.what());
            }
        });
    }
    auto *closeBtn = new QPushButton(i18n::t("close"));
    connect(closeBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    layout->addWidget(closeBtn, 0, Qt::AlignRight);
    dlg.exec();
    refreshStatus();
}

void MainWindow::createTagDialog() {
    if (m_currentFile.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, i18n::t("menu.tag_create"),
                                               i18n::t("tag_name"), QLineEdit::Normal, {}, &ok);
    if (!ok || name.trimmed().isEmpty()) return;
    try {
        git()->createTag(m_currentFile, name.trimmed());
        m_statusLabel->setText("✅ " + i18n::t("tag_created"));
    } catch (const std::exception &e) {
        QMessageBox::critical(this, i18n::t("tag_created"), e.what());
    }
}

void MainWindow::showTagList() {
    if (m_currentFile.isEmpty()) return;
    QDialog d(this);
    d.setWindowTitle(i18n::t("tag_list_t"));
    d.setMinimumSize(420, 380);
    auto *v = new QVBoxLayout(&d);
    auto *list = new QListWidget;
    list->setFont(QFont("Consolas", 10));
    auto *lbl = new QLabel;
    lbl->setStyleSheet(QString("color:%1;").arg(theme::textMuted()));
    // 与 Stash 列表同款：文案承诺"双击可删除"，行为就得真的在
    // （此前 GitService::deleteTag 完全没有入口，标签只能建不能删）
    const std::function<void()> reload = [this, list, lbl] {
        list->clear();
        const QStringList tags = git()->tags(m_currentFile);
        if (tags.isEmpty()) list->addItem(i18n::t("no_tags"));
        else list->addItems(tags);
        lbl->setText(QStringLiteral("%1: %2").arg(i18n::t("tag_list_t")).arg(tags.size()));
    };
    reload();
    v->addWidget(lbl);
    v->addWidget(list, 1);
    v->addWidget(new QLabel(i18n::t("tag_del_hint")));
    connect(list, &QListWidget::itemDoubleClicked, &d, [this, list, reload](QListWidgetItem *it) {
        const QString name = it->text();
        if (name.isEmpty() || name == i18n::t("no_tags")) return;
        if (QMessageBox::question(this, i18n::t("delete_tag"),
                                  i18n::t("delete_tag_q").arg(name)) != QMessageBox::Yes) return;
        try {
            git()->deleteTag(m_currentFile, name);
            reload();
            m_statusLabel->setText("✅ " + i18n::t("deleted") + ": " + name);
        } catch (const std::exception &e) {
            QMessageBox::critical(this, i18n::t("delete_tag"), e.what());
        }
    });
    auto *closeBtn = new QPushButton(i18n::t("close"));
    connect(closeBtn, &QPushButton::clicked, &d, &QDialog::accept);
    v->addWidget(closeBtn, 0, Qt::AlignRight);
    d.exec();
}

void MainWindow::revertToCommit() {
    auto *item = m_historyList->currentItem();
    if (!item || m_currentFile.isEmpty()) return;
    const QVariantMap m = item->data(Qt::UserRole).toMap();
    const QString hash = m.value("hash").toString();
    if (hash.isEmpty()) return;
    // 硬重置危险操作：红色富文本警告（后果逐条列出）+ 默认按钮为"否"
    QMessageBox box(this);
    box.setWindowTitle(i18n::t("revert_here"));
    box.setIcon(QMessageBox::Warning);
    box.setTextFormat(Qt::RichText);
    box.setText(i18n::t("reset_hard_confirm").arg(hash.left(10)));
    box.setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    box.setDefaultButton(QMessageBox::No);
    if (box.exec() != QMessageBox::Yes) return;
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
        } else if (e->type() == QEvent::Resize) {
            // "适应窗口"=1.0 是按控件尺寸算的：窗口/分隔条变化后必须重算，
            // 否则图片会停留在旧比例（放大后拖小窗口就会溢出）。
            // 但缩放一张大图不便宜，而拖拽窗口会连续产生大量 Resize 事件，
            // 所以做 60ms 去抖：停下来之后再算一次，别让拖拽变成幻灯片
            if (m_imageFitTimer) m_imageFitTimer->start(60);
            else applyImageZoom();
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

// 拖拽：文件夹 = 打开项目（未开仓库时）/ 递归合并进仓库（已开仓库）；文件 = 复制/替换并暂存
void MainWindow::dragEnterEvent(QDragEnterEvent *e) {
    if (!e->mimeData()->hasUrls()) return;
    for (const QUrl &u : e->mimeData()->urls()) {
        const QFileInfo fi(u.toLocalFile());
        if (fi.isDir() || fi.isFile()) {
            e->acceptProposedAction();
            if (!m_hoverStatusSaved) {   // 首次进入时记住原文案，供离开/松手后还原
                m_statusBeforeHover = m_statusLabel->text();
                m_hoverStatusSaved = true;
            }
            m_statusLabel->setText("\U0001F4C2 " + i18n::t("drag_drop_hint"));
            return;
        }
    }
}

// 拖到窗口后又拖走/取消：把状态栏还原，否则提示会一直挂到下一次操作
void MainWindow::dragLeaveEvent(QDragLeaveEvent *e) {
    QMainWindow::dragLeaveEvent(e);
    if (!m_hoverStatusSaved) return;
    m_statusLabel->setText(m_statusBeforeHover);
    m_hoverStatusSaved = false;
}

// 询问"目标已存在，是否替换"。调用方维护 apply-all 状态（本次拖拽内记住选择）
// 目标已存在时询问"是否替换"。整次拖拽**只问一次**：第一次的选择就作为本次
// 全部同名文件的策略（由调用方记进 mode），因此只需要"替换 / 跳过"两个选项 ——
// 再多出"全部替换/全部跳过"就与它们完全等价了
enum class DropReplace { Ask, Yes, No };

DropReplace askReplace(QWidget *parent, const QString &name) {
    QMessageBox box(parent);
    box.setWindowTitle(i18n::t("drop_replace_title"));
    box.setIcon(QMessageBox::Question);
    box.setText(i18n::t("drop_replace_msg").arg(name));
    auto *yes = box.addButton(i18n::t("drop_replace_yes"), QMessageBox::YesRole);
    box.addButton(i18n::t("drop_replace_no"), QMessageBox::NoRole);
    box.exec();
    // 关窗/按 Esc 一律按"跳过"处理（不覆盖是更安全的一侧）
    return box.clickedButton() == yes ? DropReplace::Yes : DropReplace::No;
}

// Windows QFile::copy 不能覆盖已存在文件；替换 = 删旧 + 拷新。目标目录不存在时自动创建
bool replaceOrCopyFile(const QString &src, const QString &dst) {
    const QString dstDir = QFileInfo(dst).absolutePath();
    if (!QFileInfo::exists(dstDir) && !QDir().mkpath(dstDir)) return false;
    if (QFile::exists(dst) && !QFile::remove(dst)) return false;
    if (QFile::copy(src, dst)) return true;
    return false;
}

void MainWindow::dropEvent(QDropEvent *e) {
    if (!e->mimeData()->hasUrls()) return;
    e->acceptProposedAction();
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls()) {
        const QString local = u.toLocalFile();
        if (!local.isEmpty()) paths << local;
    }
    importIntoRepo(paths, {});   // 落在窗口空白处 = 仓库根
}

// 把外部文件/文件夹导入仓库的 baseRel 目录（空串=仓库根）。
// 窗口拖放与"拖到文件树某个目录节点上"共用这一份实现
void MainWindow::importIntoRepo(const QStringList &paths, const QString &baseRel) {
    QStringList dirs, files;
    for (const QString &local : paths) {
        if (QFileInfo(local).isDir()) dirs << local;
        else files << local;
    }
    if (m_currentFile.isEmpty()) {
        // 没打开仓库：拖文件夹 = 打开项目（老行为）
        if (files.isEmpty() && !dirs.isEmpty()) { openRepo(dirs.first()); return; }
        QMessageBox::information(this, i18n::t("hint"), i18n::t("no_project"));
        return;
    }

    const QDir root(m_currentFile);
    const QString rootAbs = root.absolutePath() + '/';
    QStringList staged, skipped, moved;
    DropReplace mode = DropReplace::Ask;   // 本次拖拽内对"已存在"的统一选择
    // 目标目录前缀：拖到子目录时所有落点都要带上它
    auto withBase = [&baseRel](const QString &rel) {
        if (baseRel.isEmpty()) return rel;
        return rel.isEmpty() ? baseRel : baseRel + QLatin1Char('/') + rel;
    };

    // ── ① 先只枚举（纯目录遍历，不读文件内容）：列出 (源文件, 目标相对路径) ──
    // 目录里有多少文件，枚举完成前是不知道的；大目录上这一步本身就要几秒，
    // 所以只要拖了文件夹就先亮出忙碌进度窗，并周期性处理事件，别让界面假死。
    // （只拖文件时 files 已是平坦列表，枚举是瞬时的，无需打扰用户）
    std::unique_ptr<QProgressDialog> prog;
    if (!dirs.isEmpty()) {
        prog = std::make_unique<QProgressDialog>(i18n::t("import_scanning"), QString(), 0, 0, this);
        prog->setWindowTitle(i18n::t("import_title"));
        prog->setWindowModality(Qt::ApplicationModal);
        prog->setMinimumDuration(0);
        prog->setCancelButton(nullptr);   // 枚举阶段不做取消，避免留下半途状态
        prog->show();
    }
    struct Plan { QString src, relInBase; };
    QList<Plan> plan;
    qint64 totalBytes = 0;
    int scanned = 0;
    auto tick = [&] {
        if (prog && ++scanned % 500 == 0)
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
    };
    for (const QString &f : files) {
        const QFileInfo fi(f);
        plan.append({ f, fi.fileName() });
        totalBytes += fi.size();
        tick();
    }
    for (const QString &d : dirs) {
        const QString folderName = QFileInfo(d).fileName();
        QDirIterator it(d, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            plan.append({ abs, folderName + QLatin1Char('/') + QDir(d).relativeFilePath(abs) });
            totalBytes += it.fileInfo().size();
            tick();
        }
    }
    if (prog) { prog->close(); prog.reset(); }
    if (plan.isEmpty()) {
        m_statusLabel->setText("⚠ " + i18n::t("dropped_none"));
        return;
    }

    // ── ② 量大的才弹可取消的进度窗（小批量不打扰）。QProgressDialog 的 setValue
    //    会处理事件，窗口保持响应、可以中途取消，不会变成"无响应" ──
    if (plan.size() > 50 || totalBytes > 64LL * 1024 * 1024) {
        prog = std::make_unique<QProgressDialog>(
            i18n::t("importing").arg(plan.size()), i18n::t("cancel"), 0, int(plan.size()), this);
        prog->setWindowTitle(i18n::t("import_title"));
        prog->setWindowModality(Qt::ApplicationModal);
        prog->setMinimumDuration(0);
        prog->setAutoClose(false);
        prog->setAutoReset(false);
        prog->show();
    }

    // 单文件处理：仓库内的不做移动；仓库外的复制/替换到目标目录
    auto handleFile = [&](const QString &absSrc, const QString &relInBase) {
        const QFileInfo fi(absSrc);
        QString rel;
        if (fi.absoluteFilePath().startsWith(rootAbs, Qt::CaseInsensitive)) {
            // 仓库内部的文件：本程序不做"移动"，直接按原位置暂存。
            // 指定了目标目录（拖到某个目录节点上）却没生效的话，必须说明，否则
            // 用户看到的是"拖了但什么也没发生"
            const QString origin = root.relativeFilePath(fi.absoluteFilePath());
            if (origin == QLatin1String(".git") || origin.startsWith(".git/") || origin.isEmpty()) {
                skipped << fi.fileName();
                return;
            }
            if (!baseRel.isEmpty() && root.filePath(origin) != root.filePath(relInBase)) {
                moved << fi.fileName();
                return;
            }
            rel = origin;
        } else {
            const QString relDst = withBase(relInBase);
            // 拖入的文件夹可能自带 .git（如整个 clone 目录），其内部文件不进仓库
            if (relDst == QLatin1String(".git") || relDst.contains("/.git/")
                || relDst.startsWith(".git/")) {
                skipped << fi.fileName();
                return;
            }
            const QString dest = root.filePath(relDst);
            if (QFile::exists(dest)) {
                // 同名文件：整次拖拽只问一次。第一次的选择（替换或跳过）就是本次
                // 全部同名文件的处理方式，后面不再逐个打扰
                // 进度窗是 ApplicationModal，同名询问挂到它下面，模态链才正常
                if (mode == DropReplace::Ask)
                    mode = askReplace(prog ? static_cast<QWidget *>(prog.get()) : this, relDst);
                if (mode == DropReplace::No) {
                    skipped << fi.fileName();
                    return;
                }
            }
            if (!replaceOrCopyFile(absSrc, dest)) { skipped << fi.fileName(); return; }
            rel = relDst;
        }
        if (!rel.isEmpty() && rel != ".") staged << rel;
    };

    // ── ③ 按计划逐项落地，同步更新进度；用户取消就停下（已复制的仍然暂存，
    //    避免留下"文件已落盘但没进暂存区"的中间状态）。
    //    plan 里已经包含"直接拖入的文件"和"文件夹里的全部内容"，这里只走一遍 ──
    // 定位优先落在拖入的文件夹本身，这样能看到整个文件夹；
    // 没有文件夹（只拖了文件）时用循环里第一个成功导入的文件
    QString revealWanted;
    for (const QString &d : dirs) {
        if (!revealWanted.isEmpty()) break;
        revealWanted = withBase(QFileInfo(d).fileName());
    }

    int done = 0;
    // 没有进度窗时（小批量）用等待光标表示"正在干活"，别让窗口看起来像卡死了
    if (!prog) QApplication::setOverrideCursor(Qt::WaitCursor);
    for (const Plan &item : plan) {
        if (prog) {
            if (prog->wasCanceled()) break;
            prog->setValue(done);
            prog->setLabelText(QFileInfo(item.src).fileName());
            ++done;
        }
        const int before = staged.size();
        handleFile(item.src, item.relInBase);
        if (staged.size() > before && revealWanted.isEmpty()) revealWanted = staged.last();
    }
    if (!prog) QApplication::restoreOverrideCursor();
    if (prog) prog->close();
    if (!revealWanted.isEmpty()) m_revealPath = revealWanted;

    if (staged.isEmpty()) {
        m_statusLabel->setText("⚠ " + i18n::t("dropped_none"));
        return;
    }
    try {
        git()->add(m_currentFile, staged);
        markDirsExpanded(baseRel);
        refreshStatus();
        QString msg = i18n::t("dropped_staged").arg(staged.size());
        if (!skipped.isEmpty())
            msg += QStringLiteral("  (%1: %2)").arg(i18n::t("dropped_skipped"),
                                                    skipped.join(", "));
        if (!moved.isEmpty())
            msg += QStringLiteral("  (%1: %2)").arg(i18n::t("drop_move_unsupported"),
                                                    moved.join(", "));
        m_statusLabel->setText("✅ " + msg);
    } catch (const std::exception &ex) {
        QMessageBox::critical(this, i18n::t("error"), ex.what());
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
        revealPendingItem();   // 这一层加载完，刚建的文件可能已经可以定位了
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
    connect(input, &QLineEdit::returnPressed, &d, [this, input, list, &d] {
        list->clear();
        const QString q = input->text().trimmed();
        if (q.isEmpty()) return;
        // git grep 在大仓库上要跑好几秒：放后台线程，别把界面冻住
        list->addItem(i18n::t("searching"));
        input->setEnabled(false);
        const QString repo = m_currentFile;
        auto *w = new QFutureWatcher<QStringList>(&d);   // 挂在对话框上，随它一起销毁
        connect(w, &QFutureWatcher<QStringList>::finished, &d, [list, input, w] {
            w->deleteLater();
            input->setEnabled(true);
            list->clear();
            const QStringList out = w->result();          // 工作函数已兜住异常
            if (out.isEmpty()) list->addItem(i18n::t("no_results"));
            else list->addItems(out);
        });
        w->setFuture(QtConcurrent::run([repo, q]() -> QStringList {
            try {
                // -e 必须加：否则以 "-" 开头的搜索词会被 git 当成选项（unknown switch）
                const QString out = git()->run({ "grep", "-n", "-I", "-e", q }, repo, false);
                return out.split('\n', Qt::SkipEmptyParts);
            } catch (const std::exception &e) {
                return { QString::fromUtf8(e.what()) };
            }
        }));
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
        theme::applyToApp();   // 全局 QSS + 调色板
        // 以下几处是构造时设的内联样式，全局 QSS 管不到，必须逐个重刷，
        // 否则切主题后 Diff/分支图/终端会停留在旧配色
        m_titleBar->applyTheme();
        m_editorPanel->applyTheme();
        m_diffEdit->setStyleSheet(monoReadOnlyQss("QPlainTextEdit"));
        m_graphEdit->setStyleSheet(monoReadOnlyQss("QTextEdit"));
        if (m_terminal) m_terminal->applyTheme();
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
    // 复用 AboutDialog：内联版只有两行英文占位文案，功能说明/技术栈/作者全丢失
    AboutDialog dlg(this);
    dlg.exec();
}

void MainWindow::retranslateUi() {
    auto &a = m_actions;
    a.connectMenu->setTitle(i18n::t("menu.connect"));
    a.connectRepos->setText(i18n::t("repo_search_ph"));
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
    m_newFolderBtn->setText("+ " + i18n::t("new_folder"));
    m_newBranchBtn->setText("+ " + i18n::t("new_branch"));
    m_commitBtn->setText(i18n::t("commit_btn"));
    m_commitPushBtn->setText(i18n::t("commit_push_btn"));
    m_commitInput->setPlaceholderText(i18n::t("commit_placeholder"));
    m_changesTitle->setText("  \U0001F4DD " + i18n::t("changes"));
    m_changeTree->setHeaderLabels({ i18n::t("file"), i18n::t("status_col") });
    // 页签可被关闭/移位，只能用 indexOf 定位；写死下标会改错页签或越界告警
    if (m_detailTabs->indexOf(m_editorHost) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_editorHost), i18n::t("editor"));
    if (m_detailTabs->indexOf(m_diffEdit) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_diffEdit), "Diff");
    if (m_detailTabs->indexOf(m_historyGroup) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_historyGroup), i18n::t("history"));
    if (m_detailTabs->indexOf(m_graphEdit) >= 0)
        m_detailTabs->setTabText(m_detailTabs->indexOf(m_graphEdit), i18n::t("tab_graph"));
    m_historyGroup->setTitle(i18n::t("commit_history"));
    if (m_repoNameLabel->text().startsWith("\U0001F4C1") == false)
        m_repoNameLabel->setText(i18n::t("no_project"));
    m_statusLabel->setText(i18n::t("ready"));
    if (m_terminal) m_terminal->retranslate();
    updateConnectTitle();   // 语言切换后刷新账户区（含 Token 倒计时文案）
}
