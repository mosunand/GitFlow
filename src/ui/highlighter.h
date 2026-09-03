#pragma once
#include <QSyntaxHighlighter>

// 轻量多语言语法高亮：关键字/字符串/注释/数字/装饰器/内置函数
class Highlighter : public QSyntaxHighlighter {
    Q_OBJECT
public:
    explicit Highlighter(QTextDocument *doc);

protected:
    void highlightBlock(const QString &text) override;

private:
    struct Rule {
        QString pattern;
        int kind; // 0=keyword 1=string 2=comment 3=number 4=decorator 5=builtin
    };
    QList<Rule> m_rules;
};
