# Release

## 本地验收

```powershell
cmake --preset msvc-release
cmake --build --preset msvc-release --parallel
ctest --preset msvc-release
```

随后确认：

- `DesktopNote.exe` 不超过 5 MiB。
- `dumpbin /dependents DesktopNote.exe` 只列出 Windows 系统 DLL。
- 发布目录只包含 `DesktopNote.exe`，不包含 PDB 或相邻 DLL。
- 在 Windows 10 22H2 和 Windows 11 x64 上验证托盘、中文输入、多显示器/混合 DPI、置顶、桌面嵌入、鼠标穿透和位置锁定。
- 强制结束进程后，最后一次已触发自动保存的内容能够恢复。

## 发布

推送 `v2.*` 标签会运行 Windows Release 构建、测试和体积检查，并把单个 `DesktopNote.exe` 上传到对应 GitHub Release。未配置证书时产物保持未签名；签名应在上传前插入 CI 的打包步骤。
