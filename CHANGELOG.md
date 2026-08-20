# Changelog

## 2.0.0

- 使用 C++20、原生 Win32、Direct2D 和 Windowless RichEdit 完整重写。
- 发布改为单个静态 CRT `DesktopNote.exe`，不再需要 .NET 8 Runtime。
- 引入统一 `ApplicationController`，消除多窗口分别保存导致的覆盖风险。
- 新增 schema v2 数据格式、v1 `data.dat` 自动迁移、原子保存和备份恢复。
- 支持 Per-Monitor V2 DPI、Explorer 重启恢复、单实例和桌面嵌入失败回退。
- 工具栏改为紧凑半透明双行布局，背景、字色和边框均可从色板直接选择。
- CI 增加状态与 Windowless RichEdit/Direct2D 原生测试、单 EXE 包装、5 MiB 体积门槛和 `v2.*` Release 发布。

## 1.x

历史版本基于 WPF/.NET 8；旧 GitHub Release 资产保留供回退使用。
