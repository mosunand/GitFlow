#include "manualdialog.h"
#include "theme.h"
#include "i18n.h"
#include <QTextBrowser>
#include <QPushButton>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFile>
#include <QDebug>

namespace {
QString styleTag() {
    const QString text = theme::text();
    const QString dim = theme::textDim();
    const QString muted = theme::textMuted();
    const QString accent = theme::accent();
    return QString(
        // 通用：颜色跟随主题；正文用 13px，避免浏览器默认 16px 太大
        "body { color:%1; font-family:'Segoe UI','Microsoft YaHei',sans-serif; font-size:13px; line-height:1.7; }"
        "h1 { color:%2; font-size:26px; margin:8px 0 4px 0; }"
        "h2 { color:%2; font-size:17px; margin:22px 0 8px 0; padding-bottom:4px;"
        "     border-bottom:1px solid %3; }"
        "h3 { color:%2; font-size:14px; margin:14px 0 6px 0; }"
        "code { background:%4; border-radius:4px; padding:1px 6px; font-family:Consolas,monospace;"
        "       font-size:12px; color:%2; }"
        "li { margin:3px 0; }"
        "table { border-collapse:collapse; margin:8px 0; }"
        "th { background:%4; color:%2; font-weight:bold; padding:6px 12px; text-align:left;"
        "     border:1px solid %3; }"
        "td { padding:6px 12px; border:1px solid %3; color:%1; }"
        "b { color:%2; }"
        "a { color:%5; }"
        "hr { border:none; border-top:1px solid %3; margin:16px 0; }"
        ".muted { color:%6; font-size:12px; }"
        ".kbd { background:%4; border:1px solid %3; border-bottom-width:2px; border-radius:4px;"
        "       padding:1px 6px; font-family:Consolas,monospace; font-size:12px; color:%2; }")
        .arg(text, theme::text(), theme::border(), theme::bgButton(), accent, muted);
}

QString htmlBody() {
    // 使用 QStringLiteral 直接拼 HTML，内容即说明书
    const QString s =
    "<h1>\U0001F680 GitFlow <span class='muted'>v1.0.0</span></h1>"
    "<p style='color:%1;'>一款以 <b>GitHub / Gitee</b> 为核心的跨平台桌面 Git 客户端。"
    "用 C++ + Qt 6 实现，体积小、启动快，把日常 Git 操作、代码编辑、仓库管理和发布流程"
    "整合到一个窗口里。</p>"
    "<hr>"
    "<h2>\u2705 它适合什么人</h2>"
    "<ul>"
    "<li><b>开发者</b>：日常提交、分支、拉取推送，不想背 Git 命令行</li>"
    "<li><b>GitHub / Gitee 玩家</b>：多账户、多平台，Token 本地加密存储，无需来回输密码</li>"
    "<li><b>习惯看界面的用户</b>：可视化文件改动、提交历史、分支图，比敲命令直观</li>"
    "</ul>"
    "<h2>\U0001F4D6 快速开始</h2>"
    "<ol>"
    "<li>点击工具栏 <b>打开项目</b> 或 <b>Ctrl+O</b> 选择本地 Git 仓库目录</li>"
    "<li>左侧为仓库文件树，右侧为 <b>编辑 / Diff / 历史 / 分支图</b> 页签</li>"
    "<li>在中部填写提交信息，点 <b>提交</b> 或 <b>提交并推送</b></li>"
    "<li>点 <b>连接</b> 菜单可添加 GitHub / Gitee 账户（Token 加密存储）</li>"
    "</ol>"
    "<h2>\U0001F4BB 主要功能</h2>"
    "<ul>"
    "<li><b>本地仓库</b>：文件树（懒加载子目录）、代码编辑器（语法高亮 / 行号 / Ctrl+F 查找）、图片预览（Ctrl+滚轮缩放）</li>"
    "<li><b>Git 工作流</b>：暂存 / 提交 / 提交并推送 / 拉取 / 推送（带进度条与卡住检测）/ 分支切换 / 新建与删除分支 / Stash / 标签 / 回档</li>"
    "<li><b>历史与图</b>：提交历史列表、全分支提交图谱（带颜色）</li>"
    "<li><b>远程平台</b>：我的仓库 / 按用户名或 URL 搜索 / 克隆 / Fork / 新建仓库</li>"
    "<li><b>Release 发布</b>：正式版 / 预发布，支持添加附件直传</li>"
    "<li><b>大文件预检</b>：推送前扫描所有超限（&gt;100MB）文件</li>"
    "<li><b>代码运行</b>：编辑器中 <b>Ctrl+R</b> 或右键「运行」，自动从 PATH 找解释器执行 Python / Node / C / C++ / Java 等，并在内嵌终端查看输出</li>"
    "<li><b>内嵌终端</b>：可直接输入 Git 命令，支持命令历史（上下键）</li>"
    "<li><b>个性化</b>：深色 / 浅色主题，中英文界面</li>"
    "</ul>"
    "<h2>\U0001F3C6 优点</h2>"
    "<ul>"
    "<li><b>轻量</b>：单个可执行文件，免安装，绿色便携，启动秒开</li>"
    "<li><b>多平台多账户</b>：GitHub 与 Gitee 双支持，账户互不影响</li>"
    "<li><b>Token 安全</b>：使用 Windows CNG AES-256-GCM 随机密钥加密，仅存本地，绝不外传</li>"
    "<li><b>自动适配系统代理</b>：浏览器能访问 GitHub，客户端就能用</li>"
    "<li><b>可视化推送</b>：传输进度、实时日志、失败自动网络诊断，卡住有提示</li>"
    "<li><b>大文件预检</b>：避免推到一半才发现超限失败</li>"
    "<li><b>代码即运行</b>：内置编辑器改完代码直接跑，内置终端看输出</li>"
    "</ul>"
    "<h2>\U000026A0 不足之处</h2>"
    "<ul>"
    "<li><b>不支持删除远程仓库 / 修改公开/私有</b>：敏感操作请到 GitHub / Gitee 网页端</li>"
    "<li><b>超 100MB 文件无法推送</b>（平台限制）：建议 Git LFS 或移除后推送</li>"
    "<li><b>可视化合并冲突</b>尚未支持，冲突解决目前仅命令行辅助</li>"
    "<li><b>推送大仓库时</b>需等待；网络波动可能失败，请检查代理</li>"
    "<li><b>暂不支持 SSH 密钥</b>，远程推送使用 HTTPS + Token</li>"
    "</ul>"
    "<h2>\U0001F6A7 注意事项</h2>"
    "<ul>"
    "<li>使用前请<b>自行备份重要数据</b>，软件按「现状」提供</li>"
    "<li>Token 需有对应仓库权限（repo / gitee 全量权限），否则功能受限</li>"
    "<li>首次使用请到 <b>设置</b> 里确认 git.exe 路径存在</li>"
    "<li>运行代码需系统已配置对应解释器环境变量（PATH）</li>"
    "</ul>"
    "<h2>\U0001F4A1 快捷键速查</h2>"
    "<table>"
    "<tr><th>快捷键</th><th>功能</th></tr>"
    "<tr><td><span class='kbd'>Ctrl+O</span></td><td>打开项目</td></tr>"
    "<tr><td><span class='kbd'>Ctrl+S</span></td><td>保存当前文件</td></tr>"
    "<tr><td><span class='kbd'>Ctrl+F</span></td><td>在编辑器中查找</td></tr>"
    "<tr><td><span class='kbd'>Ctrl+R</span></td><td>运行当前代码文件</td></tr>"
    "<tr><td><span class='kbd'>Ctrl+`</span></td><td>开关内嵌终端</td></tr>"
    "<tr><td><span class='kbd'>F5</span></td><td>刷新仓库状态</td></tr>"
    "<tr><td><span class='kbd'>Ctrl+Q</span></td><td>退出程序</td></tr>"
    "</table>"
    "<p class='muted'>本说明会随版本迭代更新。如有问题欢迎到开源仓库提 Issue。</p>";
    return s.arg(theme::text());
}
} // namespace

ManualDialog::ManualDialog(QWidget *parent) : QDialog(parent) {
    setWindowTitle(i18n::t("manual_title"));
    setMinimumSize(760, 680);
    resize(820, 720);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(10);

    auto *browser = new QTextBrowser;
    browser->setOpenExternalLinks(true);
    browser->setStyleSheet(QString(
        "QTextBrowser{background:%1;color:%2;border:1px solid %3;border-radius:8px;}"
        "QTextBrowser QScrollBar:vertical{background:transparent;width:10px;}"
        "QTextBrowser QScrollBar::handle:vertical{background:%4;border-radius:5px;min-height:24px;}")
        .arg(theme::bg(), theme::text(), theme::border(), theme::borderLight()));
    browser->document()->setDefaultStyleSheet(styleTag());
    browser->setHtml(htmlBody());
    layout->addWidget(browser, 1);

    auto *btnRow = new QHBoxLayout;
    btnRow->addStretch(1);
    auto *closeBtn = new QPushButton(i18n::t("close"));
    closeBtn->setFixedWidth(100);
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);
}