# LiveZoom for Windows 0.6.0

放大状态下支持 OCR 文字高亮。

## 新增

放大后把光标放到文字上，按：

- `Ctrl+Shift+1`：框住这一行
- `Ctrl+Shift+2`：给这一行划线
- `Ctrl+Shift+3`：荧光笔涂亮这一行
- `Ctrl+Shift+4`：聚光灯（再按一次取消）

高亮使用 Windows.Media.Ocr 中文/英文识别，在当前放大画面上定位光标所在行；
高亮的样式、颜色、粗细沿用设置里的笔迹选项，右键和 Esc 同样可以撤销/清除。

## 说明

- 快捷键目前是固定的 Ctrl+Shift+1..4，尚未提供录制界面。
- 需要系统安装中文 OCR 语言包（中文 Windows 默认包含）；识别通常约 0.5–1 秒。
- 安装包和便携包包含离线 OCR 运行时，体积约 70–80 MB。
- 0.5.0 的光标样式/平滑/静止隐藏、0.3.0 的笔迹与点击效果保持不变。

## 安装

- 便携版：`LiveZoom-0.6.0-windows-x64-portable.zip`
- 安装包：`LiveZoom-0.6.0-windows-x64-setup.exe`

系统要求：Windows 10 2004+ 或 Windows 11，x64。

下载后请核对 `SHA256SUMS.txt`。
