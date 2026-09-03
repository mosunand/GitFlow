#pragma once
#include <QDialog>

// 软件说明界面：优势/美中不足/技术栈/作者/开源
class AboutDialog : public QDialog {
    Q_OBJECT
public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
