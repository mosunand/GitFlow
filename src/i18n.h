#pragma once
#include <QString>

namespace i18n {
// 语言: "zh" / "en"
void setLang(const QString &lang);
QString lang();
QString t(const QString &key);
} // namespace i18n
