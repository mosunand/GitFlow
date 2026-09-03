// bcrypt.h 只有在目标系统版本 >= Win7 时才声明 GCM 认证加密接口；
// Qt/MinGW 头可能先行引入 windows.h，故必须最前置并强制覆盖版本宏
#ifdef _WIN32_WINNT
#undef _WIN32_WINNT
#endif
#define _WIN32_WINNT 0x0601
#ifdef NTDDI_VERSION
#undef NTDDI_VERSION
#endif
#define NTDDI_VERSION NTDDI_WIN7
#include "crypto.h"
#include <windows.h>
#include <bcrypt.h>
#include <QByteArray>
#include <QCryptographicHash>
#include <QStringList>

#ifndef NT_SUCCESS
#define NT_SUCCESS(s) ((s) >= 0)
#endif

#pragma comment(lib, "bcrypt.lib")

namespace crypto {
namespace {
constexpr DWORD kNonceLen = 12;

QByteArray b64encode(const QByteArray &raw) { return raw.toBase64(); }
QByteArray b64decode(const QString &b64) { return QByteArray::fromBase64(b64.toUtf8()); }
} // namespace

QByteArray randomBytes(int n) {
    QByteArray out(n, Qt::Uninitialized);
    if (!NT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(out.data()),
                                    ULONG(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG)))
        out.fill(0);
    return out;
}

QString encrypt(const QString &plain, const QByteArray &key, QByteArray *nonceOut) {
    const QByteArray nonce = randomBytes(kNonceLen);
    const QByteArray data = plain.toUtf8();

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                      (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);

    BCRYPT_KEY_HANDLE hKey = nullptr;
    BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                               (PUCHAR)key.data(), ULONG(key.size()), 0);

    QByteArray tag(16, Qt::Uninitialized);
    // GCM 必须通过 BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO 传 nonce 与认证标签
    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = (PUCHAR)nonce.data();
    auth.cbNonce = ULONG(nonce.size());
    auth.pbTag = (PUCHAR)tag.data();
    auth.cbTag = ULONG(tag.size());

    QByteArray cipher(data.size() + 16, Qt::Uninitialized);
    ULONG done = 0;
    // GCM：认证信息走 pPaddingInfo，nonce 同时作为 IV 传入
    const bool ok = NT_SUCCESS(BCryptEncrypt(hKey, (PUCHAR)data.data(), ULONG(data.size()),
                                             &auth,
                                             (PUCHAR)nonce.data(), ULONG(nonce.size()),
                                             (PUCHAR)cipher.data(), ULONG(cipher.size()), &done, 0));
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!ok) return {};

    cipher.resize(int(done));
    if (nonceOut) *nonceOut = nonce;
    // 存储格式: base64(tag) + ":" + base64(cipher)
    return b64encode(tag) + ":" + b64encode(cipher);
}

QString decrypt(const QString &cipherB64, const QByteArray &key, const QByteArray &nonce) {
    const QStringList parts = cipherB64.split(':');
    if (parts.size() != 2) return {};
    const QByteArray tag = b64decode(parts[0]);
    const QByteArray cipher = b64decode(parts[1]);

    BCRYPT_ALG_HANDLE alg = nullptr;
    BCryptOpenAlgorithmProvider(&alg, BCRYPT_AES_ALGORITHM, nullptr, 0);
    BCryptSetProperty(alg, BCRYPT_CHAINING_MODE,
                      (PUCHAR)BCRYPT_CHAIN_MODE_GCM, sizeof(BCRYPT_CHAIN_MODE_GCM), 0);
    BCRYPT_KEY_HANDLE hKey = nullptr;
    BCryptGenerateSymmetricKey(alg, &hKey, nullptr, 0,
                               (PUCHAR)key.data(), ULONG(key.size()), 0);

    BCRYPT_AUTHENTICATED_CIPHER_MODE_INFO auth;
    BCRYPT_INIT_AUTH_MODE_INFO(auth);
    auth.pbNonce = (PUCHAR)nonce.data();
    auth.cbNonce = ULONG(nonce.size());
    auth.pbTag = (PUCHAR)tag.data();
    auth.cbTag = ULONG(tag.size());

    QByteArray plain(cipher.size(), Qt::Uninitialized);
    ULONG done = 0;
    NTSTATUS st = BCryptDecrypt(hKey, (PUCHAR)cipher.data(), ULONG(cipher.size()),
                                &auth,
                                (PUCHAR)nonce.data(), ULONG(nonce.size()),
                                (PUCHAR)plain.data(), ULONG(plain.size()), &done, 0);
    BCryptDestroyKey(hKey);
    BCryptCloseAlgorithmProvider(alg, 0);
    if (!NT_SUCCESS(st)) return {};
    plain.resize(int(done));
    return QString::fromUtf8(plain);
}
} // namespace crypto
