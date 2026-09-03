#pragma once
#include <QDialog>

class QTextBrowser;

// 使用说明书：软件介绍 / 快速上手 / 主要功能 / 优点 / 不足 / 注意事项
class ManualDialog : public QDialog {
    Q_OBJECT
public:
    explicit ManualDialog(QWidget *parent = nullptr);
};
