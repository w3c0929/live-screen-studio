# LiveZoom for Windows 0.2.3

修复放大后再次按中键缩回时，鼠标指针消失的问题。

## 修复

- **缩回时恢复系统光标**：先隐藏放大窗口，再调用 `MagShowSystemCursor(TRUE)`；
  部分显卡/鼠标驱动会把放大镜的光标隐藏状态保留下来，因此在确认光标曾被隐藏时
  自动重载当前光标方案（等同于切换到安全桌面后恢复光标的路径）。
- **延迟重试**：缩回后 0.15s 起再重试 3 次，覆盖驱动异步生效的情况。
- **兜底恢复**：如果系统仍报告光标不可见，会调用 `ShowCursor(TRUE)` 并刷新光标位置。
- 日志会记录 `cursor restore: wasHidden=... showing=...`，便于排查驱动差异。

0.2.2 的笔迹/点击高 DPI 对齐修复保持不变。

## 安装

- 便携版：`LiveZoom-0.2.3-windows-x64-portable.zip`
- 安装包：`LiveZoom-0.2.3-windows-x64-setup.exe`

系统要求：Windows 10 2004+ 或 Windows 11，x64。

下载后请核对 `SHA256SUMS.txt`。
