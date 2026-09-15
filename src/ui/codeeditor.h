#pragma once
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QWidget>

class QLabel;
class QPushButton;
class Highlighter;

// 带行号的代码编辑器：Ctrl+F 查找 / Ctrl+S 保存
class CodeEditor : public QPlainTextEdit {
    Q_OBJECT
public:
    explicit CodeEditor(QWidget *parent = nullptr);
    void lineNumberAreaPaintEvent(QPaintEvent *e);
    int lineNumberAreaWidth() const;
    void refreshCurrentLine() { highlightCurrentLine(); }

signals:
    void findRequested();
    void saveRequested();
    void zoomRequested(int delta);
    void runRequested();

protected:
    void resizeEvent(QResizeEvent *e) override;
    void keyPressEvent(QKeyEvent *e) override;
    void wheelEvent(QWheelEvent *e) override;
    void paintEvent(QPaintEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;
    void changeEvent(QEvent *e) override;

private slots:
    void updateAreaWidth(int);
    void updateArea(const QRect &, int);
    void highlightCurrentLine();

private:
    QWidget *m_area = nullptr;
};

// 编辑面板：查找栏 + 代码编辑器（供标签页嵌入）
class EditorPanel : public QWidget {
    Q_OBJECT
public:
    explicit EditorPanel(const QString &text, QWidget *parent = nullptr);
    QString text() const;
    void setPlainText(const QString &t);
    QString openPath() const { return m_openPath; }
    void setOpenPath(const QString &p) { m_openPath = p; }
    // 原文件的行尾风格（"\n" 或 "\r\n"）：保存时必须写回同一种
    QString lineEnding() const { return m_lineEnding; }
    void setLineEnding(const QString &eol) { m_lineEnding = eol; }
    bool isModified() const;   // 编辑器内容是否有未保存的修改
    void setModified(bool m);   // 保存后重置未保存标记
    void applyTheme();
    // notify=true：弹字号提示并记住选择（用户操作）；构造/还原时传 false
    void setEditorFontPointSize(int pt, bool notify = true);
    int editorFontPointSize() const;

signals:
    void saveRequested();
    void runRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    void recountFind();   // 按当前查找词重算匹配与计数（查找词变化、文档编辑共用）
    QLabel *m_zoomToast = nullptr;
    QTimer *m_zoomTimer = nullptr;
    QWidget *m_findBar = nullptr;
    QLineEdit *m_findInput = nullptr;
    QLabel *m_countLabel = nullptr;
    QPushButton *m_prevBtn = nullptr, *m_nextBtn = nullptr, *m_closeBtn = nullptr;
    CodeEditor *m_editor = nullptr;
    Highlighter *m_highlighter = nullptr;
    QVector<QTextCursor> m_cursors;
    int m_current = -1;
    QString m_openPath;
    QString m_lineEnding = QStringLiteral("\n");
};
