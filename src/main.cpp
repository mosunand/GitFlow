#include <QApplication>
#include <QMainWindow>
#include <QTranslator>
#include "theme.h"
#include "settings.h"
#include "i18n.h"
#include "paths.h"
#include "ui/mainwindow.h"

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("GitFlow");
    app.setOrganizationName("GitFlow");

    settings::ensureStorageLayout();
    i18n::setLang(settings::language());
    theme::setTheme(settings::theme());
    theme::applyToApp();

    // Qt 自带文案（对话框按钮等）随界面语言切换；兼容 qtbase_xx 与 qt_xx 两种命名
    QTranslator qtTr;
    if (i18n::lang() != QLatin1String("en")) {
        const QString dir = paths::appRoot() + "/translations";
        if (!qtTr.load(QStringLiteral("qtbase_%1.qm").arg(i18n::lang()), dir))
            qtTr.load(QStringLiteral("qt_%1.qm").arg(i18n::lang()), dir);
    }
    app.installTranslator(&qtTr);

    MainWindow w;
    w.show();
    return app.exec();
}
