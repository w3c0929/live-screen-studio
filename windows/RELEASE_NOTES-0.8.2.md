移除了“始终使用自定义光标”和 OCR 文字高亮，程序更轻量，非放大状态完全使用系统光标。

## 移除

- 移除“始终使用自定义光标”：非放大状态不再隐藏或替换系统光标。箭头、文本 I 形、
  链接手形、调整大小以及第三方应用自定义的光标全部保持系统原样。放大状态下的
  自定义光标、光标平滑和静止后隐藏不受影响。
- 移除 OCR 文字高亮：框住（Ctrl+Shift+1）、划线（Ctrl+Shift+2）、荧光笔
  （Ctrl+Shift+3）三项功能及其快捷键设置全部删除。
- 移除随包发布的 OCR 运行时和 `OcrService`。便携包和安装包只包含主程序，
  体积从约 73 MB 降到约 1.4 MB。
- settings.ini 中不再写入 `customCursorAlways`、`highlightBox`、`highlightUnderline`、
  `highlightFill` 和未使用的 `blankSystemCursor`；旧配置里的这些键会被忽略。

## 保留

- 光标周围圆形聚光灯（默认 Ctrl+Shift+4，可在设置中录制自定义组合），无光晕。
- 中键放大/缩回、滚轮调倍、边缘跟随与三种跟随速度、跟随边缘百分比。
- 画线、5 种笔迹样式、Shift 直线、Ctrl+拖拽穿透、右键撤销、Esc 清除。
- 点击转发与波纹/光圈效果。
- 开机启动开关。

## 安装

- 便携版：`LiveZoom-0.8.2-windows-x64-portable.zip`
- 安装包：`LiveZoom-0.8.2-windows-x64-setup.exe`

系统要求：Windows 10 2004+ 或 Windows 11，x64。

下载后请核对 `SHA256SUMS.txt`。
