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
    bool isModified() const;   // 编辑器内容是否有未保存的修改
    void setModified(bool m);   // 保存后重置未保存标记
    void applyTheme();
    void setEditorFontPointSize(int pt);
    int editorFontPointSize() const;

signals:
    void saveRequested();
    void runRequested();

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
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
};
