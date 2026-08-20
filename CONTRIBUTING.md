# Contributing

请基于最新 `main` 创建功能分支，并保持修改范围小而明确。提交前至少运行：

```powershell
cmake --preset msvc-debug
cmake --build --preset msvc-debug --parallel
ctest --preset msvc-debug
```

涉及发布行为、优化选项或资源的修改还应运行 Release 构建，并确认 `DesktopNote.exe` 不超过 5 MiB、没有非系统运行时 DLL 依赖。涉及窗口交互时，请在 Windows 10 22H2 或 Windows 11 x64 上做真实界面验证。

请勿提交 `build`、`dist`、PDB、日志或本地数据文件。第三方源码必须固定版本，并同时提交许可证与来源说明。
