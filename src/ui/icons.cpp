#include "icons.h"
#include <QPainter>
#include <QPixmap>
#include <QHash>
#include <QSet>

namespace icons {
namespace {
QHash<QString, QIcon> cache;

QString iconForExt(const QString &ext) {
    static const QHash<QString, QString> lang {
        {".py", "python.png"}, {".pyw", "python.png"},
        {".c", "c.png"}, {".h", "c.png"},
        {".cpp", "cpp.png"}, {".cxx", "cpp.png"}, {".hpp", "cpp.png"},
        {".java", "java.png"},
        {".html", "code.png"}, {".htm", "code.png"},
        {".rs", "rust.png"},
        {".js", "js.png"}, {".ts", "js.png"},
        {".css", "css.png"}, {".scss", "css.png"},
    };
    static const QSet<QString> images { ".png", ".jpg", ".jpeg", ".bmp", ".gif",
                                        ".webp", ".svg", ".ico", ".tiff" };
    if (auto it = lang.constFind(ext); it != lang.constEnd()) return *it;
    if (images.contains(ext)) return QStringLiteral("img.png");
    return QStringLiteral("code.png");
}
} // namespace

QIcon appIcon() { return QIcon(":/icon/logo.ico"); }

QIcon folderIcon() {
    static QIcon icon;
    if (icon.isNull()) {
        QPixmap pm(16, 16);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QColor(0, 0, 0, 0));
        p.setBrush(QColor("#dcb67a"));
        p.drawRoundedRect(1, 4, 14, 10, 2, 2);
        p.setBrush(QColor("#e8c48f"));
        p.drawRoundedRect(1, 2, 6, 5, 1, 1);
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon fileIcon(const QString &fileName) {
    const QString ext = fileName.section('.', -1).toLower();
    const QString key = iconForExt(ext.isEmpty() ? fileName : '.' + ext);
    if (!cache.contains(key))
        cache.insert(key, QIcon(":/icon/" + key));
    return cache.value(key);
}
} // namespace icons
