#include "highlighter.h"
#include <QRegularExpression>
#include <QTextCharFormat>
#include <QFont>
#include <QPair>
#include <QVector>

namespace {
// 格式对象只构造一次：原来在 highlightBlock 里每行都重建 6 个 QTextCharFormat
// （含 QColor 解析），大文件滚动时是纯浪费
struct Formats {
    QTextCharFormat kw, str, com, num, dec, built;
    Formats() {
        kw.setForeground(QColor("#569cd6")); kw.setFontWeight(QFont::Bold);
        str.setForeground(QColor("#ce9178"));
        com.setForeground(QColor("#6a9955")); com.setFontItalic(true);
        num.setForeground(QColor("#b5cea8"));
        dec.setForeground(QColor("#dcdcaa"));
        built.setForeground(QColor("#dcdcaa"));
    }
};
const Formats &formats() { static const Formats f; return f; }

const char *kKeywords =
    "\\b(class|def|return|if|elif|else|for|while|import|from|as|try|except|finally|with|"
    "yield|raise|pass|break|continue|lambda|and|or|not|in|is|True|False|None|self|async|"
    "await|var|let|const|function|new|this|typeof|throw|switch|case|default|do|delete|"
    "void|export|static|super|extends|implements|interface|enum|struct|type|using|"
    "package|private|protected|public|require|global)\\b";

const char *kBuiltins =
    "\\b(print|len|range|int|str|float|list|dict|set|tuple|type|isinstance|open|format|"
    "super|enumerate|zip|map|filter|sorted|reversed|any|all|min|max|sum|abs|round|"
    "console|console\\.log)\\b";
} // namespace

Highlighter::Highlighter(QTextDocument *doc) : QSyntaxHighlighter(doc) {
    // 只保留"正则扫一遍就能定"的规则；字符串与注释需要按出现顺序扫描（见 highlightBlock）
    m_rules.append({ QRegularExpression(QString::fromLatin1(kKeywords)), 0 });
    m_rules.append({ QRegularExpression(QStringLiteral("\\b\\d+\\.?\\d*\\b")), 3 });
    m_rules.append({ QRegularExpression(QStringLiteral("@\\w+")), 4 });
    m_rules.append({ QRegularExpression(QString::fromLatin1(kBuiltins)), 5 });
}

void Highlighter::highlightBlock(const QString &text) {
    const Formats &f = formats();
    const int n = text.size();

    // ① 按出现顺序扫描字符串与注释，先碰到谁就按谁着色、并占住这段区间。
    //    原实现是对每条规则各扫一遍再让后面的规则覆盖前面的，于是
    //    "http://example.com" 里的 // 会被当成注释着色，color:"#fff" 同理
    QVector<QPair<int, int>> spans;   // 已着色的 [起, 止) 区间
    int i = 0;
    while (i < n) {
        const QChar c = text.at(i);
        // 行注释：Python 的 # 与 C 系的 //
        if (c == QLatin1Char('#')
            || (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('/'))) {
            setFormat(i, n - i, f.com);
            spans.append({ i, n });
            break;                     // 行尾都是注释，无需继续
        }
        // 块注释（仅同一行内闭合，与原先的逐行实现行为一致）
        if (c == QLatin1Char('/') && i + 1 < n && text.at(i + 1) == QLatin1Char('*')) {
            const int end = text.indexOf(QLatin1String("*/"), i + 2);
            const int last = (end < 0) ? n : end + 2;
            setFormat(i, last - i, f.com);
            spans.append({ i, last });
            i = last;
            continue;
        }
        // 字符串：支持反斜杠转义，未闭合时一直到行尾
        if (c == QLatin1Char('"') || c == QLatin1Char('\'')) {
            int j = i + 1;
            while (j < n) {
                if (text.at(j) == QLatin1Char('\\')) { j += 2; continue; }
                if (text.at(j) == c) { ++j; break; }
                ++j;
            }
            const int last = qMin(j, n);
            setFormat(i, last - i, f.str);
            spans.append({ i, last });
            i = last;
            continue;
        }
        ++i;
    }

    // ② 关键字/数字/装饰器/内置函数只在这两类区间之外着色
    const QTextCharFormat *fmts[] = { &f.kw, &f.str, &f.com, &f.num, &f.dec, &f.built };
    auto covered = [&spans](int pos) {
        for (const auto &s : spans)
            if (pos >= s.first && pos < s.second) return true;
        return false;
    };
    for (const Rule &r : m_rules) {
        auto it = r.re.globalMatch(text);
        while (it.hasNext()) {
            const auto m = it.next();
            if (covered(m.capturedStart())) continue;
            setFormat(m.capturedStart(), m.capturedLength(), *fmts[r.kind]);
        }
    }
}
