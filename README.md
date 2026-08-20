# DesktopNote

DesktopNote 是面向 Windows 10 22H2 和 Windows 11 x64 的原生桌面便签。v2 使用 C++20、Win32、Direct2D 与 Windowless RichEdit 重写，发布物只有一个 `DesktopNote.exe`，无需安装 .NET、WebView2、Qt 或额外运行库。

## 功能

- 富文本编辑：RTF、字体、字号、字色、剪贴板与中文输入法
- 多便签与系统托盘统一管理
- 普通、置顶和 WorkerW 桌面嵌入模式（失败时安全回退）
- 背景透明度与文字透明度分离，透明背景下文字仍保持清晰
- 鼠标穿透、位置锁定、无边框拖动和缩放
- Per-Monitor V2 DPI、多显示器位置恢复
- 编辑后 500 ms 合并自动保存，失焦和退出时强制保存
- 临时文件、磁盘刷新、原子替换和备份恢复
- 命名互斥量单实例；Explorer 重启后恢复托盘与桌面嵌入

## 下载与运行

从 [Releases](../../releases) 下载 `DesktopNote.exe` 后直接运行。程序只依赖 Windows 自带系统 DLL。

数据保存在 `%APPDATA%\DesktopNote\data.json`。首次运行 v2 时，如果同目录只有旧版 `data.dat`，程序会自动迁移，并在 `backups` 目录保留带时间戳的原文件副本。旧文件不会被改写。

## 构建

要求：

- Windows 10/11 x64
- Visual Studio 2022 Build Tools，包含 MSVC v143 和 Windows 10/11 SDK
- CMake 3.24 或更高版本

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release
```

输出位于 `build\msvc-release\Release\DesktopNote.exe`。Release 使用静态 MSVC CRT、全程序优化和死代码移除。

## 项目结构

```text
src/          原生应用、窗口、RichEdit 宿主与数据层
resources/    图标、版本资源和应用清单
tests/        无外部框架的原生回归测试
third_party/  固定版本的 nlohmann/json 单头文件及许可证
```

详细操作见 [USER_GUIDE.md](USER_GUIDE.md)，发布与验收见 [RELEASE.md](RELEASE.md)。

## 许可证

本项目采用 [MIT License](LICENSE)。`nlohmann/json` 的许可信息见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。
