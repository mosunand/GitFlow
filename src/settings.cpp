#include "settings.h"
#include "paths.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QFile>
#include <QDir>

namespace settings {
namespace {
QJsonObject load() {
    QFile f(paths::settingsFile());
    if (!f.open(QIODevice::ReadOnly)) return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}
void save(const QJsonObject &o) {
    QFile f(paths::settingsFile());
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
}
} // namespace

QString get(const QString &key, const QString &fallback) {
    return load().value(key).toString(fallback);
}
void set(const QString &key, const QString &value) {
    QJsonObject o = load();
    o.insert(key, value);
    save(o);
}

QString gitPath() { return get("git_path"); }
void setGitPath(const QString &v) { set("git_path", v); }

QString storageRoot() { return get("storage_root", "D:/GitFlow"); }
void setStorageRoot(const QString &v) { set("storage_root", v); }

QString theme() { return get("theme", "light"); }
void setTheme(const QString &v) { set("theme", v); }

QString language() { return get("language", "zh"); }
int editorFontSize() { return get("editor_font_size", "12").toInt(); }
void setEditorFontSize(int v) { set("editor_font_size", QString::number(v)); }
void setLanguage(const QString &v) { set("language", v); }

QString lastProject() { return get("last_project"); }
void setLastProject(const QString &v) { set("last_project", v); }

void ensureStorageLayout() {
    const QString root = storageRoot();
    for (const QString &platform : {QStringLiteral("github"), QStringLiteral("gitee")})
        for (const QString &sub : {QStringLiteral("users"), QStringLiteral("downloads")})
            QDir(root + "/" + platform + "/" + sub).mkpath(".");
}
} // namespace settings
