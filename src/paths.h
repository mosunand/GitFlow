#pragma once
#include <QString>
#include <QDir>
#include <QCoreApplication>

namespace paths {

// 可写数据根：开发=项目根；部署=exe 所在目录
inline QString appRoot() {
#ifdef GF_PORTABLE
    return QCoreApplication::applicationDirPath();
#else
    return QCoreApplication::applicationDirPath();
#endif
}

// 只读资源根（Qt 资源系统 :/icon/...）
inline QString iconDir() { return ":/icon"; }

inline QString dataDir() {
    QDir d(appRoot() + "/data");
    if (!d.exists()) d.mkpath(".");
    return d.path();
}

inline QString accountsDir() {
    QDir d(dataDir() + "/accounts");
    if (!d.exists()) d.mkpath(".");
    return d.path();
}

inline QString settingsFile() { return dataDir() + "/settings.json"; }

} // namespace paths
