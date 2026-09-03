#pragma once
#include <QString>
#include <QByteArray>

namespace crypto {

// AES-256-GCM 加密（Windows CNG 实现，零外部依赖）。
// 返回 base64 密文；nonce 一并 base64 输出。
QString encrypt(const QString &plain, const QByteArray &key, QByteArray *nonceOut);
QString decrypt(const QString &cipherB64, const QByteArray &key, const QByteArray &nonce);
QByteArray randomBytes(int n);

} // namespace crypto
