# GitFlow Pro

![platform](https://img.shields.io/badge/platform-Windows%2010%2F11-blue)
![lang](https://img.shields.io/badge/C%2B%2B-17%20%2F%20Qt%206-00599C)
![license](https://img.shields.io/badge/license-MIT-green)

> 让版本控制回归简单 —— 一款以 **GitHub / Gitee** 为核心的轻量桌面 Git 客户端。

GitFlow Pro 用 **C++17 + Qt 6** 打造：单进程、免安装、启动秒开。把日常 Git 操作、
代码编辑、仓库管理、**在编辑器里直接运行代码**、Release 发布整合进同一个窗口，
不背命令行也能完成绝大部分版本控制工作。

## ✨ 功能特性

- **完整 Git 工作流**：暂存 / 提交 / 提交并推送 / 拉取（`--ff-only`）/ 分支切换与新建 / 标签 / Stash / 硬回档
- **可视化推送**：实时进度窗口（百分比 + 逐行日志 + 卡住检测），失败自动网络诊断
- **100MB 大文件预检**：推送前自动扫描全部历史对象，超限文件列清单确认（单进程 `cat-file --batch-check`，秒级完成）
- **仓库文件树**：懒加载子目录、记住展开状态、右键菜单（Diff / Blame / 放弃更改 / 删除）
- **代码编辑器**：语法高亮 / 行号 / 当前行高亮 / `Ctrl+F` 实时计数查找 / 字号缩放 / 图片预览（缩放）
- **▶ 在编辑器里运行代码**：`Ctrl+R` 或右键"运行"，自动从 PATH 查找解释器——
  Python / Node / ts-node / **C/C++（g++ 编译运行）** / Java（javac+java）/ HTML（浏览器打开）；
  找不到解释器给出配置提示；未保存修改会先提醒"保存并运行 / 仍然运行"
- **内嵌终端**：真正的 Git Bash，支持管道、重定向、`&&`、命令历史（↑↓）；运行代码前自动清屏，手动命令不清屏
- **提交历史 + 全分支分支图**：ASCII 图谱带颜色，最近 300 条
- **仓库面板**：我的仓库 / URL 或用户名搜索 / 克隆（自动归类目录）/ Fork / 新建仓库
- **Release 发布**：正式版 / **预发布（Pre-release）**、多附件直传（GitHub & Gitee 均支持）
- **多平台多账户**：GitHub / Gitee 双支持，Token 用 Windows CNG **AES-256-GCM 加密**本地存储，一键切换
- **自动适配系统代理**：环境变量 / 注册表 / 常见本地端口，浏览器能上 GitHub 客户端就能上
- **个性化**：深色 / 浅色主题、中英文界面，即时切换

## 📥 下载

前往 [**Releases**](https://github.com/mosunand/GitFlow/releases) 下载：

| 文件 | 说明 |
|------|------|
| `GitFlowPro-Setup-v1.0.0.exe` | 安装包（推荐，中文向导，免管理员权限） |
| `GitFlowPro-v3.0-Windows.zip` | 便携版，解压即用 |
| `GitFlow-1.0.0-Source.zip` | 源码包（含构建说明） |

> 📖 **完整使用文档**（20 章：从安装到 Release 发布、FAQ、注意事项）见 [docs/USER_MANUAL.md](docs/USER_MANUAL.md)。

## 🚀 快速上手

1. 安装并启动（前提：本机已装 [Git](https://git-scm.com) 并加入 PATH）
2. **设置 → 连接用户**：粘贴 GitHub / Gitee Token（Token 加密保存，只存本地）
3. **打开项目**（`Ctrl+O`）选择仓库，或用仓库面板直接搜索并克隆
4. 改代码 → `Ctrl+S` 保存 → `Ctrl+R` 直接运行 → 提交框写信息 → **提交并推送**

## 🛠️ 从源码构建

**环境**：Windows 10/11 ・ CMake ≥ 3.21 ・ Qt 6.8.3（MinGW 64-bit）・ MinGW-w64 GCC 13

```bash
git clone https://github.com/mosunand/GitFlow.git
cd GitFlow
# 修改 CMakeLists.txt 中 Qt 路径为本机位置后：
cmake -S . -B build -G "MinGW Makefiles" -D CMAKE_PREFIX_PATH="D:/Qt/6.8.3/mingw_64"
cmake --build build --config Release -j
# 产物：build/bin/GitFlowPro.exe（windeployqt 自动部署运行库）
```

## 📁 项目结构

```
├── src/
│   ├── main.cpp            入口
│   ├── ui/                 MainWindow / TitleBar / CodeEditor / TerminalPanel
│   │                       ReleaseDialog / RepoPanelDialog / SettingsDialog ...
│   ├── services/           GitService(git CLI) / GitHubService / GiteeService
│   │                       AccountService(Token 加密存储)
│   ├── theme.cpp           深/浅主题与全局 QSS
│   ├── i18n.cpp            中英文案表
│   ├── crypto.cpp          AES-256-GCM（Windows CNG，零外部依赖）
│   └── proxy.cpp           系统代理探测（TTL 缓存）
├── resources/              图标与 Qt 资源
└── CMakeLists.txt
```

## 🔒 安全说明

- Token 使用 **AES-256-GCM**（Windows CNG）加密，**每个账户独立随机密钥**，仅存本地 `data/` 目录
- 软件不收集、不上传任何数据；网络请求仅发往 GitHub / Gitee 官方 API
- 请勿将 `data/` 目录分享给他人

## ⚠️ 已知不足

- 不支持删除远程仓库、修改仓库公开/私有（请到网页端操作）
- 单文件 >100MB 无法推送（平台限制，建议 Git LFS）
- 暂无可视化合并冲突工具、暂不支持 SSH 认证（HTTPS + Token）

## 🤝 参与贡献

Issue、PR、Star 都欢迎！发现问题请到
[Issues](https://github.com/mosunand/GitFlow/issues) 提交，附上复现步骤。

---

**作者**：[mosunand](https://github.com/mosunand) ・ 📮 moshuai1013@outlook.com

本软件按"现状"提供，使用前请自行备份重要数据。
