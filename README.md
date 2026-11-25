# MdReader

面向 Windows 的轻量 Markdown 编辑器。解压后运行 MdReader.exe，可直接拖入 UTF-8 Markdown 文件。使用 Win32、Direct2D、DirectWrite 和 md4c，仅依赖 Windows 自带组件。

![MdReader 界面示意](docs/screenshot.png)

## 日常使用

- **预览编辑**。点击段落会显示可编辑的 Markdown 标记，正文保留渲染后的字号。移开光标或按 Esc 隐藏标记。预览保留原始换行和连续空行，独占一行的 `<br />` 显示为空行。
- **源码模式**。Ctrl+E 切换，两个模式共用编辑内容，支持中文输入、撤销重做和 Ctrl+S 保存。
- **多文件**。标签过多时，用右侧下拉菜单切换。再次启动会恢复上次查看的文件和位置。
- **文件同步**。打开的文件在外部保存后通常一秒左右刷新，并尽量保留滚动位置。本地有未保存修改时保留编辑内容，发生冲突会在保存时提示。
- **导航**。点击网页、文档标题锚点或相对路径 Markdown 链接即可跳转。按住 Ctrl 点击链接可进入编辑。Ctrl+J 打开大纲。
- **Markdown**。支持标题、列表、任务列表、引用、代码块、表格、分隔线和本地图片。粘贴图片会存到 Markdown 文件同级的 attachment 目录。

| 快捷键 | 功能 | 快捷键 | 功能 |
| --- | --- | --- | --- |
| Ctrl+N / Ctrl+W | 新建 / 关闭标签 | Ctrl+Tab | 切换标签 |
| Ctrl+O / Ctrl+S | 打开 / 保存 | Ctrl+E | 预览 / 源码 |
| Ctrl+F | 查找 | F3 / Shift+F3 | 下一个 / 上一个匹配 |
| Ctrl+B / Ctrl+I | 加粗 / 斜体 | Ctrl+K | 插入链接 |
| Ctrl+Z / Ctrl+Y | 撤销 / 重做 | Esc | 退出当前编辑或查找 |

## Windows x64 便携版

解压 MdReader-win64.zip 后双击 MdReader.exe。本地打包结果在 dist，可通过项目的 Releases 页面获取已发布版本。
[GitHub Releases](https://github.com/cjbbt001/mdReader/releases)

命令行可接收 Markdown 文件路径。--dark 和 --light 可强制指定主题。需要关联 .md 文件时可运行 MdReader.exe --register，再通过 Windows 设置选择默认应用。--unregister 移除该注册。

## 1.1.0 实测

Windows 11 23H2，x64 Release。空白文件和随包示例各启动 5 次，取中位数。内存为窗口可见后约 1 秒的工作集。

| 项目 | 结果 |
| --- | ---: |
| 单文件程序 | 1.41 MiB |
| 空白文件窗口可见 | 58.7 ms |
| 示例文件窗口可见 | 61.2 ms |
| 空白文件工作集 | 44.8 MiB |
| 示例文件工作集 | 47.2 MiB |

排版先覆盖视口，再分批处理其余内容。图片使用 WIC 按需解码，文档同步仅在检测到磁盘变更后读取内容。含大量图片的文档会占用更多内存。

## 构建

准备 MinGW-w64 的 gcc、g++ 和 windres，也可使用便携版 w64devkit。构建脚本会查找 .tools/w64devkit/w64devkit/bin。

~~~sh
sh build.sh
~~~

程序生成在 build/MdReader.exe。使用 Visual Studio 2022 时可通过 CMake 构建。

~~~powershell
cmake -S . -B build/msvc -G "Visual Studio 17 2022" -A x64
cmake --build build/msvc --config Release
~~~

tools/bench.c 可测量窗口可见时间与内存。使用独立样本文件，运行前关闭 MdReader。

~~~powershell
gcc -O2 -municode tools/bench.c -o build/bench.exe -lpsapi
build/bench.exe build/MdReader.exe samples/sample.md
~~~

## 代码目录

| 文件 | 职责 |
| --- | --- |
| src/main.cpp、src/chrome.cpp | 窗口、标签、查找、设置与文件刷新调度 |
| src/files.cpp | UTF-8 读写、文件版本检查和原子保存 |
| src/editor.cpp、src/preview.cpp、src/source.cpp | 编辑缓冲区、预览编辑和源码视图 |
| src/doc.cpp、src/layout.cpp、src/graphics.cpp | Markdown 解析、排版和绘制 |
| src/images.cpp、src/highlight.cpp | 本地图片和代码着色 |
| third_party/md4c | md4c 0.5.2，MIT 许可 |

## 已知限制

- 源码模式使用横向滚动。按住 Shift 滚轮可横向移动。
- 撤销历史按文档快照保存，总量约 16 MiB，至少保留一步。
- 图片仅加载本地文件。远程图片不会下载。
- 跨编辑器保存时，连续普通空行的数量取决于对方编辑器的 Markdown 规则。
