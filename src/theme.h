#pragma once
#include <QString>

namespace theme {

// 当前主题: "dark" / "light"
void setTheme(const QString &t);
QString theme();
bool isDark();

// 颜色访问（自动跟随当前主题）
QString bg();          // 主内容背景
QString bgSurface();   // 面板背景
QString bgElevated();  // 标题栏背景
QString bgTab();       // 标签背景
QString bgHover();     // 悬停背景
QString bgButton();    // 按钮背景
QString bgInput();     // 输入框背景
QString border();      // 边框
QString borderLight();
QString text();        // 主文字
QString textDim();     // 次要文字
QString textMuted();   // 弱文字
QString accent();      // 主题色
QString accentHover();
QString selection();   // 选中背景
QString editorFindBar();

// 全局 QSS（跟随当前主题），应用到 QApplication
QString globalQss();

// 设置 QApplication 调色板 + 全局 QSS（启动与运行时切换统一入口）
void applyToApp();

} // namespace theme
