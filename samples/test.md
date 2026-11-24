# 功能测试

## 代码高亮

```cpp
// 一个最小例子
#include <cstdio>

int main(int argc, char** argv) {
    const char* msg = "hello, world";
    for (int i = 0; i < 3; ++i) {
        printf("%s %d\n", msg, i * 42);
    }
    return 0;
}
```

```python
def fib(n):
    # 递归实现
    if n < 2:
        return n
    return fib(n - 1) + fib(n - 2)
```

## 图片

![示意图](demo.png)

## 主题

点工具条左边的太阳/月亮图标切换深浅色，`--dark` / `--light` 启动参数也可以强制。

## 任务列表

- [x] 标签页
- [x] 源码编辑
- [ ] 遥控器
- [ ] 云同步
