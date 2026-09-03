#include "proxy.h"
#include <QNetworkProxy>
#include <QElapsedTimer>
#include <QSocketNotifier>
#include <WinSock2.h>
#include <Ws2tcpip.h>
#include <windows.h>
#include <algorithm>

namespace proxy {
namespace {

QString normalize(const QString &hp) {
    QString s = hp.trimmed();
    if (s.endsWith('/')) s.chop(1);
    if (!s.contains(QLatin1String("://")))
        s.prepend(QLatin1String("http://"));
    return s;
}

// 用 TCP 连接测试本地代理端口是否存活（短超时，避免卡住 UI）
bool portAlive(const QString &host, quint16 port, int timeoutMs = 80) {
    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *res = nullptr;
    if (getaddrinfo(host.toUtf8().constData(), std::to_string(port).c_str(), &hints, &res) != 0)
        return false;
    SOCKET sock = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    if (sock == INVALID_SOCKET) { freeaddrinfo(res); return false; }
    u_long nonblock = 1;
    ioctlsocket(sock, FIONBIO, &nonblock);
    ::connect(sock, res->ai_addr, int(res->ai_addrlen));
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(sock, &wset);
    timeval tv {0, timeoutMs * 1000};
    bool alive = select(0, nullptr, &wset, nullptr, &tv) > 0;
    if (alive) {
        int soErr = 0, len = sizeof(soErr);
        getsockopt(sock, SOL_SOCKET, SO_ERROR, reinterpret_cast<char *>(&soErr), &len);
        alive = (soErr == 0);
    }
    closesocket(sock);
    freeaddrinfo(res);
    return alive;
}

QString tryCommonPorts() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return {};
    QString found;
    for (quint16 port : {quint16(7890), quint16(7897), quint16(10809),
                         quint16(10808), quint16(1080)}) {
        if (portAlive(QStringLiteral("127.0.0.1"), port)) {
            found = QStringLiteral("http://127.0.0.1:%1").arg(port);
            break;
        }
    }
    WSACleanup();
    return found;
}
} // namespace

QString detectImpl() {
    // 1) 环境变量
    for (const char *var : {"HTTPS_PROXY", "https_proxy", "HTTP_PROXY",
                            "http_proxy", "ALL_PROXY", "all_proxy"}) {
        const QString v = qEnvironmentVariable(var).trimmed();
        if (!v.isEmpty()) return normalize(v);
    }
    // 2) Windows 注册表
    HKEY hkey;
    if (RegOpenKeyExW(HKEY_CURRENT_USER,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Internet Settings",
                      0, KEY_READ, &hkey) == ERROR_SUCCESS) {
        DWORD enable = 0, size = sizeof(DWORD);
        if (RegQueryValueExW(hkey, L"ProxyEnable", nullptr, nullptr,
                             (LPBYTE)&enable, &size) == ERROR_SUCCESS && enable) {
            wchar_t buf[512] = {};
            size = sizeof(buf);
            if (RegQueryValueExW(hkey, L"ProxyServer", nullptr, nullptr,
                                 (LPBYTE)buf, &size) == ERROR_SUCCESS) {
                QString server = QString::fromWCharArray(buf);
                if (server.contains('=')) {
                    // "ftp=...;http=...;https=..." 形式
                    for (const QString &part : server.split(';')) {
                        if (part.startsWith("http=") || part.startsWith("https=")) {
                            server = part.section('=', 1);
                            break;
                        }
                    }
                }
                RegCloseKey(hkey);
                if (!server.isEmpty()) return normalize(server);
            } else {
                RegCloseKey(hkey);
            }
        } else {
            RegCloseKey(hkey);
        }
    }
    // 3) 常见本地代理端口
    return tryCommonPorts();
}

bool hasProxy() { return !detectSystemProxy().isEmpty(); }

QString detectSystemProxy() {
    // TTL 缓存：避免每次 new RestService 都同步扫端口卡住 UI
    static QString cached;
    static QElapsedTimer last;
    if (last.isValid() && last.elapsed() < 60000) return cached;
    cached = detectImpl();
    last.restart();
    return cached;
}

} // namespace proxy
