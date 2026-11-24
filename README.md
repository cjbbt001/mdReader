# MD Reader

Windows 上的极简 Markdown 阅读器：只读、零配置、双击即阅。Win32 + Direct2D + DirectWrite + md4c，C++20，无框架。

![screenshot](docs/screenshot.png)

大纲面板（`Ctrl+B` 或右上角按钮），点击跳转，滚动时自动跟随：

![outline](docs/outline.png)

## 构建

不需要 Visual Studio。装一个 MinGW-w64 并把 `g++`/`gcc`/`windres` 放进 PATH（本机用的是便携版 [w64devkit](https://github.com/skeeto/w64devkit)，解压到 `.tools/w64devkit` 即可，`build.sh` 会自动找），然后：

```sh
sh build.sh
```

产物是 `build/mdreader.exe`，约 1.1 MB，静态链接，不依赖 VC 运行库。

用 MSVC 的话：`cmake -B build-msvc -G "Visual Studio 17 2022" && cmake --build build-msvc --config Release`。

## 用法

```
mdreader.exe <文件.md>
mdreader.exe <文件.md> --dark     强制深色
mdreader.exe <文件.md> --light    强制浅色
```

不带参数时显示空白提示页。拖放文件到窗口也可以打开，Ctrl+O 打开对话框。

按键：滚动滚轮 / PageUp / PageDown / Space / Home / End / 方向键。`Ctrl+B` 开关大纲，`Ctrl+O` 打开文件。滚动条是自己画的，可以拖。

`MDREADER_TRACE=1` 启动时会把各阶段耗时写到 `%TEMP%/mdreader.trace`。

## 现在做到哪一步

已经能用的：

- CommonMark 段落、标题、有序/无序列表（含嵌套）、引用、围栏代码块、分隔线
- 行内：粗体、斜体、删除线、行内代码、链接（着色，未接点击）
- GFM 表格、任务列表（任务标记显示为普通列表项）
- 视口优先的增量排版：先排完当前视口就出帧，剩下的在空闲时继续
- 窗口缩放后重新排版，滚动位置按内容重新锚定
- 跟随系统深浅色（`AppsUseLightTheme`）
- 大纲面板：按标题层级缩进，点击跳转，滚动时跟随高亮，鼠标滚轮单独滚动面板

还没做的：

- 图片（现在只走 alt 文本）
- 代码块语法高亮
- Ctrl+F 查找、链接点击跳转
- 源码/编辑模式（下一轮）
- 文件关联、单实例（双击第二个文件会开新窗口）
- 记住每个文件的滚动位置

## 实测数据

在这台机器上（Windows 11 23H2，2048×1152 @125%）：

| 项目 | 数值 |
| --- | --- |
| 二进制体积 | 1,181,696 字节 |
| 窗口可见 | 47–78 ms |
| 首帧内容 | ~100 ms |
| 小文档内存 | 42 MB 工作集 / 30 MB 提交 |
| 90 KB 文档（2801 个块） | 67 MB 工作集 / 95 MB 提交 |
| 单帧绘制（1080×900） | 2–5 ms |
| 单帧绘制（1900×1060） | ~3 ms |
| 源码 | 约 1500 行 |

启动拆解：`CreateWindowExW` ~35 ms（窗口类 + 主题初始化），`ShowWindow` ~50 ms（首次 DWM 合成），D2D 渲染目标 ~8 ms，解析 + 首次排版 < 5 ms。

## 两个刻意的取舍

**渲染用软件后端**（`D2D1_RENDER_TARGET_TYPE_SOFTWARE`）。硬件渲染目标在这台机器上要 127 ms 建一次 D3D 设备，内容要等到 240 ms 才出现；软件后端 8 ms，单帧 2–5 ms，对纯文本完全够用。4K 窗口下大约 12 ms/帧，仍然可用。

**没有 `CoInitializeEx`**。D2D、DirectWrite、`GetOpenFileNameW` 都不要求它；加上它本身要 30 ms，而不加时 `CreateWindowExW` 内部同样要付这笔钱，净开销一样，所以只在真正需要 COM 的地方（打开文件对话框）懒初始化。

内存的大头是 Direct2D/DirectWrite 本身（空窗口 30 MB 提交）。要压到 15 MB 级别只能换成 GDI 绘制 + DirectWrite 排版。

## 结构

```
src/doc.{h,cpp}        md4c → Block 树（块 + 行内 run）
src/layout.{h,cpp}     Block → Item（IDWriteTextLayout + 位置），增量排版
src/graphics.cpp       D2D 设备、画笔、文本格式、绘制
src/main.cpp           窗口、输入、文件打开、主题
third_party/md4c/      MIT，Markdown 解析（v0.5.2）
tools/bench.c          启动时间/内存测量小工具
```

排版层不碰窗口状态：`layoutStep(app, untilY, budgetMs)` 只依赖 `App::vw`、文档和文本格式。换渲染后端（比如改成 GDI）时这部分可以整体保留。

## 踩过的坑（都修了）

- **深色标题栏不生效**。`DwmSetWindowAttribute` 返回 S_OK 但没反应，真正原因是进程导入表里没有 dwmapi.dll（代码里没有直接调它的符号，只有 `GetProcAddress`），`GetModuleHandleW(L"dwmapi.dll")` 拿到 NULL，调用被静默跳过。改用 `LoadLibraryW` 后正常。
- **经典滚动条没有深色版本**。`SetWindowTheme(hwnd, L"DarkMode_Explorer")` 对 `WS_VSCROLL` 无效，uxtheme 的 `SetPreferredAppMode` / `AllowDarkModeForWindow` 序号函数在这台机器上也没效果。改成自绘滚动条，顺便解决了浅色主题下的宽度不一致。
- **后台排版把首帧饿死了**。原来用 `PostMessage` 续排，消息队列里排队的 160 个续排任务挡在 `WM_PAINT` 前面，90 KB 文档首帧要 1 秒。改成 `WM_TIMER`（最低优先级，队列空了才来）后 111 ms。

## 还没解决

- **整篇文档的 `IDWriteTextLayout` 常驻内存**，90 KB 文档 2801 个块，提交内存 95 MB。修法是给 `Item` 记块索引，绘制时按需重建、离开视口两个屏幕就 `reset()`，高度缓存起来；预计能压到 35 MB 左右。前提是块数据（`Doc::blocks`）一直在内存里，约 90 KB 文档 × 若干倍。
- 表格单元格只保留纯文本，单元格内的行内样式会丢。
