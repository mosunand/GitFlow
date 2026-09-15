// 调试工具：解密本地账户文件里的 Gitee Token（只输出到 stdout，供 curl 调试用）
#include "crypto.h"
#include <QCoreApplication>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QByteArray>
#include <iostream>

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);
    if (argc < 2) { std::cerr << "usage: token <account.json>\n"; return 1; }
    QFile f(QString::fromLocal8Bit(argv[1]));
    if (!f.open(QIODevice::ReadOnly)) { std::cerr << "cannot open account file\n"; return 1; }
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    const QJsonObject crypto = o.value("crypto").toObject();
    const QByteArray key = QByteArray::fromBase64(crypto.value("key").toString().toUtf8());
    const QByteArray nonce = QByteArray::fromBase64(crypto.value("nonce").toString().toUtf8());
    const QString token = crypto::decrypt(
        o.value("credential").toObject().value("token").toString(), key, nonce);
    if (token.isEmpty()) { std::cerr << "decrypt failed\n"; return 1; }
    std::cout << token.toStdString() << std::endl;
    return 0;
}
