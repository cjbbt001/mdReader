# MdReader

一个极简的 Markdown 阅读器。拖进来就能看，没有启动画面，没有设置面板。

## 为什么做这个

现有方案要么太慢，要么太重。Electron 系的编辑器打开一个 20 KB 的文档要等两秒，内存吃掉 300 MB；单纯想看一遍文档的人，不该为此付出代价。

这个程序只做一件事：把 Markdown 排好版，显示出来。窗口 100 毫秒内出来，全部代码 4700 行。

## 支持的东西

- CommonMark 的段落、标题、列表、引用、代码块
- **粗体**、*斜体*、`行内代码`、~~删除线~~
- [链接](https://example.com)（目前只着色，还不能点）
- GFM 表格、任务列表
- 代码块语法高亮、图片、大纲面板

### 还没做的

1. 链接点击跳转
2. 记住每个文件的滚动位置
3. 表格单元格里的行内样式
4. 从网络加载图片

> 增量布局：先排一屏多一点的量就立刻呈现，剩下的在空闲时继续排。
> 这样即使打开一本 500 页的书，第一帧也不会等全文排完。

```cpp
// 布局是按需推进的，不是一次性排完
void layoutStep(App& app, float untilY, int budgetMs) {
    while (L.next < app.doc.blocks.size()) {
        Item it = layoutBlock(app, app.doc.blocks[L.next], L.contentH, cx, cw);
        L.items.push_back(std::move(it));
        if (L.contentH >= untilY) break;
        if (elapsed() >= budgetMs) break;
    }
}
```

## 渲染开销

| 项目 | 数值 | 说明 |
| --- | --- | --- |
| 二进制体积 | 1.4 MB | 静态链接，无运行库依赖 |
| 窗口可见 | 47–78 ms | 内容随后补上 |
| 首帧内容 | ~100 ms | 解析 + 排版 + 绘制 |
| 工作集 | 42 MB | 大头是 Direct2D/DirectWrite |
| 依赖 | 系统 DLL | D2D、DWrite、WIC |

> 表格、引用、列表的缩进都按层级累加，嵌套越深越窄。
> **这一行是粗体**，*这一行是斜体*，`inline code` 用等宽字体。

---

最后一段：滚动滚轮、翻页键、Home / End、空格都能用，`Ctrl+E` 切到源码就能改，`Ctrl+J` 打开大纲。
