#include "theme.h"
#include <QApplication>
#include <QPalette>
#include <QColor>
#include <QHash>

namespace theme {
namespace {
QString g_theme = QStringLiteral("light");

struct Palette {
    const char *bg, *bgSurface, *bgElevated, *bgTab, *bgHover, *bgButton,
        *bgInput, *border, *borderLight, *text, *textDim, *textMuted,
        *accent, *accentHover, *selection, *findBar;
};

const Palette kDark = {
    "#0d1117", "#161b22", "#000000", "#21262d", "#2a313c", "#21262d",
    "#0d1117", "#30363d", "#3d444d", "#e6edf3", "#c9d1d9", "#8b949e",
    "#3b82f6", "#539bf5", "#264f78", "#161b22"};

const Palette kLight = {
    "#f5f6fa", "#ffffff", "#fafbfc", "#ebedf0", "#e3e6ea", "#eef0f3",
    "#ffffff", "#e1e4e8", "#d0d7de", "#1f2328", "#24292f", "#57606a",
    "#2f6fed", "#1f62e0", "#d0e2ff", "#f5f6fa"};

const Palette &cur() { return g_theme == "dark" ? kDark : kLight; }
} // namespace

void setTheme(const QString &t) { g_theme = t; }
QString theme() { return g_theme; }
bool isDark() { return g_theme == "dark"; }

#define GET(name) QString ret; do { ret = QString::fromLatin1(cur().name); } while(0); return ret
QString bg()          { return cur().bg; }
QString bgSurface()   { return cur().bgSurface; }
QString bgElevated()  { return cur().bgElevated; }
QString bgTab()       { return cur().bgTab; }
QString bgHover()     { return cur().bgHover; }
QString bgButton()    { return cur().bgButton; }
QString bgInput()     { return cur().bgInput; }
QString border()      { return cur().border; }
QString borderLight() { return cur().borderLight; }
QString text()        { return cur().text; }
QString textDim()     { return cur().textDim; }
QString textMuted()   { return cur().textMuted; }
QString accent()      { return cur().accent; }
QString accentHover() { return cur().accentHover; }
QString selection()   { return cur().selection; }
QString editorFindBar() { return cur().findBar; }

QString globalQss() {
    const Palette &c = cur();
    QString q = QStringLiteral(
        "* { font-family: 'Segoe UI','Microsoft YaHei',sans-serif; font-size: 12.5px; }"
        "QMainWindow, QDialog { background-color: %1; }"
        "QMenuBar { background-color: %12; color: %4; border-bottom: 1px solid %7; padding: 2px 6px; }"
        "QMenuBar::item { padding: 5px 10px; border-radius: 6px; }"
        "QMenuBar::item:selected { background-color: %5; color: %3; }"
        "QMenu { background-color: %2; color: %4; border: 1px solid %7; border-radius: 8px; padding: 4px; }"
        "QMenu::item { padding: 6px 24px; border-radius: 5px; }"
        "QMenu::item:selected { background-color: %8; color: white; }"
        "QToolBar { background-color: %2; border: none; border-bottom: 1px solid %7; spacing: 4px; padding: 4px 6px; }"
        "QPushButton { background-color: %6; color: %4; border: 1px solid %9; border-radius: 7px; padding: 5px 10px; outline: none; }"
        "QPushButton:hover { background-color: %5; color: %3; }"
        "QPushButton:pressed { background-color: %8; color: white; }"
        "QPushButton:disabled { color: %10; background-color: %6; }"
        "QLineEdit, QPlainTextEdit, QTextEdit, QComboBox { background-color: %1; color: %3;"
        " border: 1px solid %7; border-radius: 7px; padding: 4px 8px; selection-background-color: %11; }"
        "QLineEdit:focus, QComboBox:focus { border-color: %8; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background-color: %2; color: %4; border: 1px solid %7;"
        " border-radius: 6px; selection-background-color: %8; selection-color: white; }"
        "QListWidget, QTreeWidget { background-color: %2; color: %4; border: 1px solid %7;"
        " border-radius: 8px; padding: 4px; show-decoration-selected: 1; }"
        "QListWidget::item, QTreeWidget::item { padding: 3px 4px; border: none;"
        " border-radius: 0; outline: 0; }"
        "QListWidget::item:selected, QTreeWidget::item:selected { background-color: %11; color: %3; }"
        "QListWidget::item:hover, QTreeWidget::item:hover { background-color: %5; }"
        "QListWidget::item:focus, QTreeWidget::item:focus { outline: none; }"
        "QTreeWidget::branch:selected { background-color: %11; }"
        "QTreeWidget::branch:hover { background-color: %5; }"
        "QGroupBox { background-color: %2; border: 1px solid %7; border-radius: 10px;"
        " margin-top: 10px; padding-top: 6px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 12px; padding: 0 5px; color: %8; font-weight: bold; }"
        "QTabWidget::pane { border: 1px solid %7; border-radius: 8px; background: %1; }"
        "QTabBar::tab { background: transparent; color: %10; padding: 7px 14px;"
        " margin-right: 4px; border-radius: 7px; }"
        "QTabBar::tab:selected { background: %2; color: %3; font-weight: bold; }"
        "QTabBar::tab:hover { background: %5; }"
        "QTabBar::tab:first { margin-left: 4px; }"
        "QTabBar::close-button { image: url(:/icon/close_dot.png); subcontrol-position: right; }"
        "QTabBar::close-button:hover { image: url(:/icon/close_dot_hover.png); }"
        "QTabBar::close-button { image: url(:/icon/close_dot.png); subcontrol-position: right; }"
        "QTabBar::close-button:hover { image: url(:/icon/close_dot_hover.png); }"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 0; }"
        "QScrollBar::handle:vertical { background: %9; border-radius: 5px; min-height: 24px; }"
        "QScrollBar::handle:vertical:hover { background: %10; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 0; }"
        "QScrollBar::handle:horizontal { background: %9; border-radius: 5px; min-width: 24px; }"
        "QStatusBar { background-color: %2; color: %10; border-top: 1px solid %7; }"
        "QProgressBar { background-color: %6; border: none; border-radius: 6px;"
        " text-align: center; color: %10; }"
        "QProgressBar::chunk { background-color: %8; border-radius: 6px; }"
        "QToolTip { background-color: %2; color: %4; border: 1px solid %7; border-radius: 6px; padding: 4px 8px; }"
        "QSplitter::handle { background-color: %7; }"
        "QSplitter::handle:hover { background-color: %8; }"
    );
    return q.arg(c.bg, c.bgSurface, c.text, c.textDim, c.bgHover, c.bgButton,
                 c.border, c.accent, c.borderLight, c.textMuted, c.selection,
                 c.bgElevated);
}

void applyToApp() {
    if (QApplication *app = qobject_cast<QApplication *>(QCoreApplication::instance())) {
        const Palette &c = cur();
        QPalette p;
        p.setColor(QPalette::Window, QColor(c.bgSurface));
        p.setColor(QPalette::WindowText, QColor(c.text));
        p.setColor(QPalette::Base, QColor(c.bg));
        p.setColor(QPalette::AlternateBase, QColor(c.bgTab));
        p.setColor(QPalette::Text, QColor(c.text));
        p.setColor(QPalette::Button, QColor(c.bgTab));
        p.setColor(QPalette::ButtonText, QColor(c.text));
        p.setColor(QPalette::Highlight, QColor(c.accent));
        p.setColor(QPalette::HighlightedText, QColor("#ffffff"));
        p.setColor(QPalette::ToolTipBase, QColor(c.bgSurface));
        p.setColor(QPalette::ToolTipText, QColor(c.text));
        app->setPalette(p);
        app->setStyleSheet(globalQss());
    }
}
} // namespace theme
