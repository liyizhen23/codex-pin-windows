# Codex Pin for Windows

为 Windows 版 Codex Desktop 添加一个轻量的置顶按钮，让 Codex 窗口可以保持在其他窗口上方。

这个项目采用原生 Win32 C++ 实现。按钮会跟随 Codex 标题栏、自动适配标题栏明暗配色，并与 Codex 主窗口建立原生 owner 关系，因此 Codex 最小化、隐藏或关闭时，按钮会同步响应，不会留下一个延迟消失的悬浮控件。

> 这不是 OpenAI 官方插件，也不会修改、注入或重新打包 Codex 的签名 MSIX。它是一个仅在本机运行的窗口辅助程序。

## 功能

- 点击图钉切换“始终置顶”状态
- `Ctrl + Alt + P` 快速切换置顶
- 自动跟随 Codex 窗口的位置、显示、隐藏、最小化与关闭
- 从标题栏取色，适配 Codex 的浅色/深色界面
- 原生事件驱动，不使用高频定时轮询
- 单实例运行，支持开机自动启动
- 右键图钉可退出辅助程序

## 系统要求

- Windows 10/11 x64
- 从 Microsoft Store/MSIX 安装的 Codex Desktop
- 安装与使用不需要管理员权限

## 安装

1. 在仓库的 **Releases** 页面下载 `CodexPin-Windows-v1.0.0.zip`。
2. 解压全部文件。
3. 在解压目录打开 PowerShell，运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Install-CodexPin.ps1
```

安装器会把程序复制到 `%LOCALAPPDATA%\CodexPin`，创建当前用户的开机启动快捷方式，并立即启动。因为当前发布文件未进行代码签名，Windows SmartScreen 可能会显示提示；请只从本仓库的 Release 下载。

## 使用

- 单击标题栏右侧的图钉：切换 Codex 窗口置顶。
- 图钉处于选中状态：Codex 当前保持置顶。
- 再次单击或按 `Ctrl + Alt + P`：取消置顶。
- 右键图钉并选择退出：关闭 Codex Pin，不关闭 Codex。

## 卸载

在安装包解压目录运行：

```powershell
powershell -ExecutionPolicy Bypass -File .\Uninstall-CodexPin.ps1
```

卸载脚本会先列出将关闭和删除的项目，并要求输入 `Y` 确认。它只删除 `%LOCALAPPDATA%\CodexPin\CodexPin.exe` 与 `Codex Pin.lnk` 启动项。

## 从源码构建

需要安装 Visual Studio 2022 Build Tools，并包含“使用 C++ 的桌面开发”工具集。然后在仓库根目录运行：

```powershell
.\build.ps1
```

产物位于 `dist\CodexPin.exe`。构建采用 `/MT`，发布程序不依赖额外安装 Visual C++ 运行库。

## 实现说明

Codex Pin 通过 `SetWinEventHook` 监听窗口创建、前台切换、位置变化、显示/隐藏和最小化事件；使用 `SetWindowPos(HWND_TOPMOST)` 切换 Codex 主窗口层级。图钉本身是一个透明、不可激活的 Win32 layered window，并以 Codex 主窗口作为 owner。

程序仅检查本机窗口与进程路径，不读取对话内容，不联网，也不收集遥测。若 Codex 将来改变 Windows 包名、进程名或窗口实现，窗口识别逻辑可能需要同步调整。

## 性能

在开发机的一次空闲测试中，程序私有内存约 1.3 MB、工作集约 7.6 MB，3 秒采样内 CPU 增量为 0 ms。实际数据会因系统版本与运行环境不同而变化。

## 许可

Copyright (c) 2026 liyizhen23. All rights reserved. 未经许可不得再分发或用于衍生发布。
