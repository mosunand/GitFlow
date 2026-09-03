#include "codeeditor.h"
#include "highlighter.h"
#include "i18n.h"
#include "theme.h"
#include "settings.h"
#include <QLabel>
#include <QTimer>
#include <QPushButton>
#include <QPainter>
#include <QContextMenuEvent>
#include <QMenu>
#include <QScrollBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QKeyEvent>
#include <QEvent>
#include <QScrollBar>

// ─────────────────── CodeEditor ───────────────────
namespace {
class LineNumberArea : public QWidget {
public:
    CodeEditor *editor;
    LineNumberArea(CodeEditor *ed) : QWidget(ed), editor(ed) {}
    QSize sizeHint() const override { return {editor->lineNumberAreaWidth(), 0}; }
    void paintEvent(QPaintEvent *e) override { editor->lineNumberAreaPaintEvent(e); }
};
} // namespace

CodeEditor::CodeEditor(QWidget *parent) : QPlainTextEdit(parent) {
    setLineWrapMode(QPlainTextEdit::WidgetWidth);   // 长行自动折行铺到下一行，放大后不截断
    m_area = new LineNumberArea(this);
    connect(this, &QPlainTextEdit::blockCountChanged, this, [this](int) { updateAreaWidth(0); });
    connect(this, &QPlainTextEdit::updateRequest, this, [this](const QRect &r, int dy) {
        updateArea(r, dy);
    });
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, [this] { highlightCurrentLine(); });
    updateAreaWidth(0);
    highlightCurrentLine();
}

int CodeEditor::lineNumberAreaWidth() const {
    int digits = 2;
    for (int max = qMax(1, blockCount()); max >= 10; max /= 10) ++digits;
    return 14 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits;
}

void CodeEditor::updateAreaWidth(int) { setViewportMargins(lineNumberAreaWidth(), 0, 0, 0); }

void CodeEditor::updateArea(const QRect &rect, int dy) {
    if (dy) m_area->scroll(0, dy);
    else m_area->update(0, rect.y(), m_area->width(), rect.height());
    if (rect.contains(viewport()->rect())) updateAreaWidth(0);
}

void CodeEditor::resizeEvent(QResizeEvent *e) {
    QPlainTextEdit::resizeEvent(e);
    const QRect cr = contentsRect();
    m_area->setGeometry(cr.left(), cr.top(), lineNumberAreaWidth(), cr.height());
}

void CodeEditor::lineNumberAreaPaintEvent(QPaintEvent *e) {
    QPainter painter(m_area);
    painter.fillRect(e->rect(), QColor(theme::bg()));
    // 当前行行号区跟随高亮（与编辑器当前行底色一致）
    if (hasFocus() && !isReadOnly()) {
        const QRect cur = cursorRect();
        painter.fillRect(QRect(0, cur.top(), m_area->width(), cur.height()),
                         QColor(theme::bgHover()));
    }
    QTextBlock block = firstVisibleBlock();
    int number = block.blockNumber() + 1;
    int top = qRound(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + qRound(blockBoundingRect(block).height());
    const int current = textCursor().blockNumber() + 1;
    while (block.isValid() && top <= e->rect().bottom()) {
        if (block.isVisible() && bottom >= e->rect().top()) {
            QFont f = painter.font();
            if (number == current) {
                f.setBold(true);
                painter.setFont(f);
                painter.setPen(QColor(theme::text()));
            } else {
                f.setBold(false);
                painter.setFont(f);
                painter.setPen(QColor(theme::textMuted()));
            }
            painter.drawText(0, top, m_area->width() - 10, fontMetrics().height(),
                             Qt::AlignRight, QString::number(number));
        }
        block = block.next();
        top = bottom;
        bottom = top + qRound(blockBoundingRect(block).height());
        ++number;
    }
}

void CodeEditor::changeEvent(QEvent *e) {
    // 焦点进出与主题/样式变化时刷新当前行高亮，避免残留旧配色
    switch (e->type()) {
    case QEvent::FocusIn:
    case QEvent::FocusOut:
    case QEvent::PaletteChange:
    case QEvent::StyleChange:
    case QEvent::ThemeChange:
        highlightCurrentLine();
        break;
    default: break;
    }
    QPlainTextEdit::changeEvent(e);
}

void CodeEditor::highlightCurrentLine() {
    QList<QTextEdit::ExtraSelection> sels;
    // 只在编辑器获得焦点时显示当前行，切换标签页时不再出现突兀横带
    if (!isReadOnly() && hasFocus()) {
        QTextEdit::ExtraSelection sel;
        sel.format.setBackground(QColor(theme::bgHover()));
        sel.format.setProperty(QTextFormat::FullWidthSelection, true);
        sel.cursor = textCursor();
        sel.cursor.clearSelection();
        sels.append(sel);
    }
    setExtraSelections(sels);
}

// 编辑器右键菜单：统一 i18n（Undo/Redo/Cut/Copy/Paste/Select All 全部跟随语言设置）
void CodeEditor::contextMenuEvent(QContextMenuEvent *e) {
    QMenu menu;
    menu.addAction(i18n::t("run_code"), this, [this] { emit runRequested(); });
    menu.addSeparator();
    menu.addAction(i18n::t("undo"), this, [this] { undo(); });
    menu.addAction(i18n::t("redo"), this, [this] { redo(); });
    menu.addSeparator();
    menu.addAction(i18n::t("cut"), this, [this] { cut(); });
    menu.addAction(i18n::t("copy"), this, [this] { copy(); });
    menu.addAction(i18n::t("paste"), this, [this] { paste(); });
    menu.addAction(i18n::t("delete_sel"), this, [this] { textCursor().removeSelectedText(); });
    menu.addSeparator();
    menu.addAction(i18n::t("select_all"), this, [this] { selectAll(); });
    menu.exec(e->globalPos());
}

// VSCode 风格缩进参考线：在每个缩进层级画一条竖直淡线
void CodeEditor::paintEvent(QPaintEvent *e) {
    QPlainTextEdit::paintEvent(e);
    const qreal step = tabStopDistance();
    const qreal sw = fontMetrics().horizontalAdvance(QLatin1Char(' '));
    if (step <= 0 || sw <= 0) return;
    const int perLevel = qMax(1, qRound(step / sw));   // 一级缩进的空格数
    QPainter p(viewport());
    p.setPen(QColor(theme::border()));
    QTextBlock block = firstVisibleBlock();
    qreal y = blockBoundingGeometry(block).translated(contentOffset()).top();
    while (block.isValid() && y <= viewport()->height()) {
        if (block.isVisible()) {
            const QString t = block.text();
            int spaces = 0;
            for (const QChar ch : t) {
                if (ch == QLatin1Char(' ')) ++spaces;
                else if (ch == QLatin1Char('\t')) spaces += perLevel;
                else break;
            }
            const int lv = spaces / perLevel;
            const qreal h = blockBoundingRect(block).height();
            for (int i = 1; i <= lv; ++i) {
                const qreal x = contentOffset().x() + i * step - 1;
                p.drawLine(QPointF(x, y), QPointF(x, y + h));
            }
        }
        block = block.next();
        if (!block.isValid()) break;
        y = blockBoundingGeometry(block).translated(contentOffset()).top();
    }
}

// Ctrl+滚轮 调节字体大小
void CodeEditor::wheelEvent(QWheelEvent *e) {
    if (e->modifiers() & Qt::ControlModifier) {
        emit zoomRequested(e->angleDelta().y());
        e->accept();
        return;
    }
    QPlainTextEdit::wheelEvent(e);
}

void CodeEditor::keyPressEvent(QKeyEvent *e) {
    if (e->key() == Qt::Key_F && e->modifiers() & Qt::ControlModifier) { emit findRequested(); return; }
    if (e->key() == Qt::Key_S && e->modifiers() & Qt::ControlModifier) { emit saveRequested(); return; }
    QPlainTextEdit::keyPressEvent(e);
}

// ─────────────────── EditorPanel ───────────────────
EditorPanel::EditorPanel(const QString &text, QWidget *parent) : QWidget(parent) {
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_findBar = new QWidget;
    m_findBar->setStyleSheet(QString(
        "background: %1; border-bottom: 1px solid %2;").arg(theme::editorFindBar(), theme::border()));
    auto *fl = new QHBoxLayout(m_findBar);
    fl->setContentsMargins(6, 4, 6, 4);
    fl->setSpacing(6);

    m_findInput = new QLineEdit;
    m_findInput->setPlaceholderText(i18n::t("find_placeholder"));
    m_findInput->setFixedHeight(26);
    m_findInput->setStyleSheet(QString(
        "QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:0 8px;}"
        "QLineEdit:focus{border-color:%4;}").arg(theme::bgInput(), theme::text(), theme::border(), theme::accent()));
    fl->addWidget(m_findInput, 1);

    m_countLabel = new QLabel;
    m_countLabel->setStyleSheet(QString("color:%1;font-size:12px;").arg(theme::textMuted()));
    fl->addWidget(m_countLabel);

    m_prevBtn = new QPushButton("↑");
    m_nextBtn = new QPushButton("↓");
    for (auto *b : { m_prevBtn, m_nextBtn }) {
        b->setFixedSize(26, 26);
        b->setStyleSheet(QString(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;font-weight:bold;}"
            "QPushButton:hover{background:%4;}").arg(theme::bgButton(), theme::text(), theme::border(), theme::bgHover()));
    }
    connect(m_prevBtn, &QPushButton::clicked, this, [this] {
        if (m_cursors.isEmpty()) return;
        m_current = (m_current - 1 + m_cursors.size()) % m_cursors.size();
        m_editor->setTextCursor(m_cursors[m_current]);
    });
    connect(m_nextBtn, &QPushButton::clicked, this, [this] {
        if (m_cursors.isEmpty()) return;
        m_current = (m_current + 1) % m_cursors.size();
        m_editor->setTextCursor(m_cursors[m_current]);
    });
    fl->addWidget(m_prevBtn);
    fl->addWidget(m_nextBtn);

    m_closeBtn = new QPushButton;
    m_closeBtn->setFixedSize(20, 20);
    m_closeBtn->setCursor(Qt::PointingHandCursor);
    m_closeBtn->setToolTip(i18n::t("close"));
    m_closeBtn->setIcon(QIcon(":/icon/close_dot.png"));
    m_closeBtn->setIconSize(QSize(14, 14));
    m_closeBtn->setStyleSheet("QPushButton{border:none;background:transparent;}");
    m_closeBtn->installEventFilter(this);
    connect(m_closeBtn, &QPushButton::clicked, m_findBar, &QWidget::hide);
    fl->addWidget(m_closeBtn);

    m_findBar->hide();

    // 字号悬浮提示：缩放时显示当前 pt，短暂停留后消失
    m_zoomToast = new QLabel(this);
    m_zoomToast->setAlignment(Qt::AlignCenter);
    m_zoomToast->setStyleSheet(QStringLiteral(
        "QLabel{background:rgba(33,38,45,225);color:#e6edf3;"
        "border:1px solid rgba(139,148,158,120);border-radius:8px;"
        "padding:6px 16px;font-size:13px;font-weight:bold;}"));
    m_zoomToast->hide();
    m_zoomTimer = new QTimer(this);
    m_zoomTimer->setSingleShot(true);
    connect(m_zoomTimer, &QTimer::timeout, m_zoomToast, &QLabel::hide);
    layout->addWidget(m_findBar);

    m_editor = new CodeEditor;
    connect(m_editor, &CodeEditor::zoomRequested, this, [this](int delta) {
        setEditorFontPointSize(m_editor->font().pointSize() + (delta > 0 ? 1 : -1));
    });
    setEditorFontPointSize(12);   // 每次启动都从默认 12pt 开始
    // 字体/字号/制表位/QSS 统一由 setEditorFontPointSize 设置
    connect(m_editor, &CodeEditor::findRequested, this, [this] {
        const QTextCursor c = m_editor->textCursor();
        if (c.hasSelection()) m_findInput->setText(c.selectedText());
        m_findBar->show();
        m_findInput->setFocus();
        m_findInput->selectAll();
    });
    connect(m_editor, &CodeEditor::saveRequested, this, &EditorPanel::saveRequested);
    connect(m_editor, &CodeEditor::runRequested, this, &EditorPanel::runRequested);
    layout->addWidget(m_editor, 1);

    m_editor->setPlainText(text);
    m_highlighter = new Highlighter(m_editor->document());

    connect(m_findInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        m_cursors.clear();
        m_current = -1;
        if (!text.isEmpty()) {
            QTextCursor c = m_editor->document()->find(text, 0);
            while (!c.isNull()) {
                m_cursors.append(c);
                c = m_editor->document()->find(text, c);
            }
            if (!m_cursors.isEmpty()) m_current = 0;
        }
        if (m_cursors.isEmpty())
            m_countLabel->setText(text.isEmpty() ? QString() : i18n::t("no_results"));
        else {
            m_countLabel->setText(QString("%1/%2").arg(m_current + 1).arg(m_cursors.size()));
            m_editor->setTextCursor(m_cursors[m_current]);
        }
    });
}

QString EditorPanel::text() const { return m_editor->toPlainText(); }

void EditorPanel::setPlainText(const QString &t) {
    m_cursors.clear();
    m_current = -1;
    m_findInput->clear();
    m_editor->setPlainText(t);
}

void EditorPanel::setEditorFontPointSize(int pt) {
    pt = qBound(10, pt, 16);   // 范围 10~16pt
    QFont f(QStringLiteral("Consolas"), pt);
    m_editor->setFont(f);
    // QSS 的 font-size 优先级高于 setFont，必须同步重建样式表，否则缩放无效
    m_editor->setStyleSheet(QString(
        "QPlainTextEdit{background-color:%1;color:%2;border:none;selection-background-color:%3;"
        "font-family:'Consolas','Courier New',monospace;font-size:%4pt;}")
        .arg(theme::bg(), theme::text(), theme::selection()).arg(pt));
    m_editor->setTabStopDistance(m_editor->fontMetrics().horizontalAdvance(QLatin1Char(' ')) * 4);
    m_editor->refreshCurrentLine();
    if (m_zoomToast) {
        m_zoomToast->setText(QStringLiteral("%1 pt").arg(pt));
        m_zoomToast->adjustSize();
        m_zoomToast->move((width() - m_zoomToast->width()) / 2, 14);
        m_zoomToast->raise();
        m_zoomToast->show();
        m_zoomTimer->start(900);
    }
}

bool EditorPanel::isModified() const { return m_editor && m_editor->document()->isModified(); }

void EditorPanel::setModified(bool m) {
    if (m_editor) m_editor->document()->setModified(m);
}

int EditorPanel::editorFontPointSize() const { return m_editor->font().pointSize(); }

// 主题切换后重刷内联样式
void EditorPanel::applyTheme() {
    m_editor->setStyleSheet(QString(
        "QPlainTextEdit{background-color:%1;color:%2;border:none;selection-background-color:%3;"
        "font-family:'Consolas','Courier New',monospace;font-size:%4pt;}")
        .arg(theme::bg(), theme::text(), theme::selection())
        .arg(QString::number(editorFontPointSize())));
    m_findBar->setStyleSheet(QString(
        "background: %1; border-bottom: 1px solid %2;").arg(theme::editorFindBar(), theme::border()));
    m_findInput->setStyleSheet(QString(
        "QLineEdit{background:%1;color:%2;border:1px solid %3;border-radius:6px;padding:0 8px;}"
        "QLineEdit:focus{border-color:%4;}")
        .arg(theme::bgInput(), theme::text(), theme::border(), theme::accent()));
    m_countLabel->setStyleSheet(QString("color:%1;font-size:12px;").arg(theme::textMuted()));
    for (auto *b : { m_prevBtn, m_nextBtn })
        b->setStyleSheet(QString(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;font-weight:bold;}"
            "QPushButton:hover{background:%4;}")
            .arg(theme::bgButton(), theme::text(), theme::border(), theme::bgHover()));
    m_editor->refreshCurrentLine();
}

bool EditorPanel::eventFilter(QObject *obj, QEvent *e) {
    if (obj == m_closeBtn) {
        if (e->type() == QEvent::HoverEnter)
            m_closeBtn->setIcon(QIcon(":/icon/close_dot_hover.png"));
        else if (e->type() == QEvent::HoverLeave)
            m_closeBtn->setIcon(QIcon(":/icon/close_dot.png"));
    }
    return QWidget::eventFilter(obj, e);
}
