#include "highlighter.h"
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QFont>

Highlighter::Highlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {
    const char *kw =
        "\\b(class|def|return|if|elif|else|for|while|import|from|as|try|except|finally|with|"
        "yield|raise|pass|break|continue|lambda|and|or|not|in|is|True|False|None|self|async|"
        "await|var|let|const|function|new|this|typeof|throw|switch|case|default|do|delete|"
        "void|export|static|super|extends|implements|interface|enum|struct|type|using|"
        "package|private|protected|public|require|global)\\b";
    m_rules.append({ kw, 0 });
    m_rules.append({ R"("(?:\\.|[^"\\])*")", 1 });
    m_rules.append({ R"('(?:\\.|[^'\\])*')", 1 });
    m_rules.append({ "#[^\\n]*", 2 });
    m_rules.append({ "//[^\\n]*", 2 });
    m_rules.append({ "/\\*[\\s\\S]*?\\*/", 2 });
    m_rules.append({ "\\b\\d+\\.?\\d*\\b", 3 });
    m_rules.append({ "@\\w+", 4 });
    m_rules.append({
        "\\b(print|len|range|int|str|float|list|dict|set|tuple|type|isinstance|open|format|"
        "super|enumerate|zip|map|filter|sorted|reversed|any|all|min|max|sum|abs|round|"
        "console|console\\.log)\\b", 5 });
}

void Highlighter::highlightBlock(const QString &text) {
    QTextCharFormat kwF, strF, comF, numF, decF, builtF;
    kwF.setForeground(QColor("#569cd6")); kwF.setFontWeight(QFont::Bold);
    strF.setForeground(QColor("#ce9178"));
    comF.setForeground(QColor("#6a9955")); comF.setFontItalic(true);
    numF.setForeground(QColor("#b5cea8"));
    decF.setForeground(QColor("#dcdcaa"));
    builtF.setForeground(QColor("#dcdcaa"));
    const QTextCharFormat *fmts[] = { &kwF, &strF, &comF, &numF, &decF, &builtF };
    for (const Rule &r : m_rules) {
        auto it = QRegularExpression(r.pattern).globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            setFormat(m.capturedStart(), m.capturedLength(), *fmts[r.kind]);
        }
    }
}
