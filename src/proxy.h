#pragma once
#include <QString>

namespace proxy {

// 依次检测：环境变量 → Windows 注册表代理 → PAC → 常见本地端口探测。
// 返回形如 "http://127.0.0.1:10808"，无则空串。
QString detectSystemProxy();

// 是否检测到可用代理
bool hasProxy();

} // namespace proxy
