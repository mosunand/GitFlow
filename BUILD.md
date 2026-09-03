# 从源码构建 GitFlow Pro

## 环境要求
- Windows 10 / 11
- CMake >= 3.21
- Qt 6.8.3（MinGW 64-bit）或更高 Qt 6 版本
- MinGW-w64（GCC 13 已验证）

## 构建步骤
```bash
cmake -S . -B build -G "MinGW Makefiles" -D CMAKE_PREFIX_PATH="你的Qt安装目录/mingw_64"
cmake --build build --config Release -j
```
构建完成后 `build/bin/` 下会自动由 windeployqt 部署 Qt 运行库，双击 `GitFlowPro.exe` 运行。

## 发布前瘦身（可选）
windeployqt 会带入一些本程序用不到的组件，可手动删除以减小体积：
- `Qt6Pdf.dll` + `imageformats/qpdf.dll`（PDF 图片预览，本软件用不到）
- `Qt6Svg.dll`、`iconengines/qsvgicon.dll`、`imageformats/qsvg.dll`（无 SVG 资源）
- `translations/` 里除 `qtbase_zh_CN.qm`、`qt_zh_CN.qm` 外的所有 .qm
- `generic/`、`networkinformation/`、`tls/qcertonlybackend.dll`

## 目录结构
- `src/`            C++ 源码（ui/ 界面，services/ Git 与平台 API）
- `resources/`      图标与 Qt 资源
- `app.rc`          Windows 资源（应用图标）
- `docs/`           用户手册
- `CMakeLists.txt`  构建脚本

## 注意
- `CMakeLists.txt` 第 13 行的 Qt 路径按本机安装位置修改
- 需要 `Qt6::Widgets Qt6::Network Qt6::Concurrent` 组件
