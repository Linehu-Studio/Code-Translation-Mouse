# Code Translation Mouse

把鼠标停在 IDE 里的代码单词上，就会弹出中文翻译。关键字、类型名、变量名（会按驼峰 / 下划线拆词）都可以看。

## 做什么用

读英文代码时不用来回查词。例如：

- 停在 `return` 上 → **返回**
- 停在 `getUserName` 上 → **获取 · 用户 · 名称**
- 停在 `unique_ptr` 上 → **独占指针**
- 停在 `isValid` 上 → **是否 · 有效**

程序在后台运行，托盘里有一个「译」图标。

## 怎么用

1. 运行 `build\CodeTranslationMouse.exe`（编译方法见下方）。
2. 系统托盘出现「译」之后，把鼠标移到代码单词上，浮窗会立即弹出（跟随鼠标实时更新，无需停顿等待）。
3. 浮窗会显示原词、词性和中文。
4. `Ctrl+Alt+T` 开关翻译；右键托盘图标可以暂停或退出。

如果 IDE 是以管理员身份运行的，这个工具也要用管理员身份打开，否则取不到词。

在 **VS Code / Cursor** 里如果取不到词：

- 程序会自动重试（鼠标停住时每 0.15 秒一次，最多 4 次），第一次悬停可能要等 1 秒左右才弹出——编辑器的无障碍文本是收到查询后才构建的
- 一直取不到的话，把设置里的 `Editor: Accessibility Support` 设为 `on`
- 或以 `CodeTranslationMouse.exe --debug` 启动，看控制台里有没有捕获到单词

支持较好的编辑器：Notepad++、Visual Studio、Qt Creator、带无障碍接口的 VS Code / Cursor。Notepad++ 走 Scintilla 消息，一般最稳。

## 编译

需要 Windows，以及 MinGW-w64（本仓库用 MSYS2 的 `g++`）或 Visual Studio。

```bat
build.bat
```

编出来的文件在 `build\CodeTranslationMouse.exe`。

```bat
build\CodeTranslationMouse.exe --self-test
```

可自检词典和拆词是否正常。

用 CMake：

```bat
cmake -S . -B build
cmake --build build --config Release
```

## 自定义词典

词典文件是 UTF-8，每行一条：

```
英文|中文|kind
```

`kind` 可以是 `keyword`、`type`、`word`。

加载顺序：

1. 程序内置的一份基础词
2. 编译进 exe 的 `dictionary.txt`
3. exe 同目录的 `dictionary.txt`（可直接改）
4. `%APPDATA%\CodeTranslationMouse\dictionary.txt`（个人补充，优先）

改完后重新把鼠标停上去即可，不必重启（同目录文件目前是启动时读一次，改完需要重启程序）。

## 项目结构

```
src/main.cpp          入口
src/App.cpp           悬停检测、托盘、热键
src/TextCapture.cpp   从编辑器取词（Scintilla / UI Automation / 编辑框）
src/Translate.cpp     拆标识符并查词典
src/Ui.cpp            浮窗和托盘图标
dictionary.txt        编程词典
```

## 许可

MIT License，© Linehu-Studio
