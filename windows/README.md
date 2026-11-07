# LiveZoom for Windows

Windows 10/11 x64 版 LiveZoom，使用 C++20 和原生 Win32 / Direct3D 11 / Direct2D / DirectComposition 实现。

## 功能

- 单击鼠标中键：以光标为中心平滑放大（默认 2×），再按一次缩回。
- 按住中键滚动滚轮：调整放大倍数（1.25× 到 4×）。
- 放大后移动鼠标：光标在画面中间时镜头不动，靠近边缘（默认约 8% 处）才平稳跟随；
  设置里的跟随速度越灵敏，触发位置越靠近边缘。
- 放大后左键拖动：画线；按住 Shift 画直线；Ctrl + 左键拖动保留正常拖拽。
- 放大后单击：点击会转发给下层应用，同时显示波纹。笔迹和波纹都跟随放大后的光标，
  在高 DPI 缩放下也不会偏移。
- 右键：撤销一笔；Esc：清除全部笔迹。
- 系统托盘菜单：放大/缩回、启用、设置、退出。
- 设置窗口：默认倍数、跟随速度、放大时画线、画面平滑、排除 OBS 窗口。
- OBS 使用“显示器采集”即可拍到放大后的画面和笔迹，不需要改 OBS 设置。

## 安装

### 便携版

下载 `LiveZoom-<版本>-windows-x64-portable.zip`，解压到任意目录，运行 `livezoom_win.exe`。

### 安装包

下载 `LiveZoom-<版本>-windows-x64-setup.exe`。默认按当前用户安装到
`%LOCALAPPDATA%\Programs\LiveZoom`，不需要管理员权限。安装时可选择桌面快捷方式和开机启动。

## 使用

| 操作 | 效果 |
| --- | --- |
| 中键单击 | 放大 / 缩回 |
| 按住中键 + 滚轮 | 调整倍数 |
| 左键拖动 | 画线（放大时，鼠标跟着笔尖移动） |
| Shift + 左键拖动 | 直线 |
| Ctrl + 左键拖动 | 正常拖拽 |
| 左键单击 | 转发点击并显示波纹 |
| 右键 | 撤销一笔 |
| Esc | 清除全部笔迹 |

第一次运行会在 `%APPDATA%\LiveZoom\settings.ini` 创建配置文件。

程序以 GUI 方式运行，不会弹出控制台。诊断日志位于
`%APPDATA%\LiveZoom\livezoom.log`，每次启动会重新写入。

缩回时程序会主动恢复系统光标：先隐藏放大窗口，再调用系统恢复并在必要时重载光标方案，
随后还会重试几次，避免某些显卡/鼠标驱动下出现“放大后缩回看不到指针”的问题。

## OBS

推荐使用 OBS 的 **显示器采集**。LiveZoom 的放大画面由 Windows Magnification API
直接作用于桌面合成，标注层是独立的透明窗口，显示器采集会同时拍到两者。

如果使用 **窗口采集** 抓取 LiveZoom 的放大窗口，笔迹层不会包含在内。请改用显示器采集。

## 从源码构建

要求：

- Windows 10 2004+ 或 Windows 11
- CMake 3.28+
- Ninja（推荐）
- MinGW-w64 GCC 13+（本项目在 GCC 16.2 / UCRT64 下验证）
- 或 Visual Studio 2022 + MSVC（理论支持，未在 CI 中验证）

```bash
cmake -S windows -B windows/build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build windows/build-release -j
ctest --test-dir windows/build-release --output-on-failure
windows/build-release/src/app/livezoom_win.exe
```

打包便携版和安装包（需要 Inno Setup 的 `iscc`）：

```powershell
powershell -ExecutionPolicy Bypass -File windows/scripts/package.ps1 -Version 0.2.0
```

输出在 `windows/dist/`。

## 代码结构

```text
windows/
  src/core/            平台无关算法：镜头、视口、平滑、光标运动、文本行、箭头几何
  src/platform/win/    Magnification 引擎、全局输入钩子
  src/app/             应用层：控制器、标注、DirectComposition 覆盖层、设置、托盘
  tests/               核心算法测试
  poc/                 技术验证程序（magnification / input / overlay / OCR）
  assets/              图标、Inno Setup 脚本
```

## 当前限制 / 路线图

- 当前版本只替换放大画面，不替换系统光标样式；光标由 Magnification API 放大显示。
- 文字识别高亮（框住/划线/荧光笔/聚光灯）尚未接入 Windows 版；`poc/ocr_poc`
  已验证 Windows.Media.Ocr 中文识别可用（2560×1440，51 行，约 243 ms）。
- 自定义光标样式、光标平滑/静止隐藏、应用内开机启动开关、自动更新尚未实现；安装包提供启动项选项。
- 全局低级钩子和光标控制可能被部分安全软件拦截；发布包暂未做代码签名。
- 独占全屏游戏、受 DRM 保护的内容、部分远程桌面场景可能无法放大。
