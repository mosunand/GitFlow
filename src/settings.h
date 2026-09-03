#pragma once
#include <QString>

namespace settings {

// 读写 data/settings.json；set* 自动落盘
QString get(const QString &key, const QString &fallback = {});
void set(const QString &key, const QString &value);

QString gitPath();
void setGitPath(const QString &v);

QString storageRoot();           // 默认 "D:/GitFlow"
void setStorageRoot(const QString &v);

QString theme();                 // "dark"/"light"
void setTheme(const QString &v);

QString language();
int editorFontSize();
void setEditorFontSize(int v);              // "zh"/"en"
void setLanguage(const QString &v);

QString lastProject();
void setLastProject(const QString &v);

void ensureStorageLayout();      // <root>/<platform>/{users,downloads}

} // namespace settings
