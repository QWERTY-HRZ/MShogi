# MShogi (将棋)

[![License: GPL v3](https://img.shields.io/badge/License-GPLv3-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)

[English](README.md) | **简体中文**

MShogi 是一款基于 C++ 和 Qt 框架开发的跨平台桌面端将棋程序。它拥有完整的规则引擎、精美的原木质感 UI 以及多种对局辅助功能，致力于提供沉浸式的对局体验。

## 📸 界面预览

![将棋对局](assets/1.png)

![界面展示](assets/2.png)

## ✨ 程序功能简介

* **完整的规则引擎**：精准实现将棋规则，包括棋子移动、吃子、打入与状态管理。
* **交互式 UI**：支持直观的鼠标拖拽落子，并能实时计算并高亮显示选中棋子的合法移动位置。全面支持高分屏自适应 (High-DPI) 与抗锯齿渲染。
* **对局与记谱系统**：实时记录行棋历史与棋谱（Game History），方便对局复盘。
* **专业对局钟**：内置先手/后手独立计时器，支持自定义基础对局时长与步时奖励。
* **对局控制**：支持悔棋、暂停/继续、认输以及重新开始游戏。内置防误触机制，严格限制非当前回合的操作。

## 🛠️ 如何编译

### 环境依赖

* **C++ 编译器**：GCC, Clang 或 MSVC (建议支持 C++17)。
* **Qt 框架**：Qt 5.12+ (需要包含 `Widgets` 和 `Gui` 模块)。
* **构建工具**：CMake 或 qmake。

### 编译步骤

1. 将项目克隆到本地：`git clone https://github.com/QWERTY-HRZ/MShogi.git`
2. 打开 Qt Creator，选择 文件 -> 打开文件或项目...。
3. 选择项目根目录下的 .pro 文件 (qmake) 或 CMakeLists.txt 文件 (CMake)。
4. 选择对应的 Qt 构建套件 (Kit) 完成配置。
5. 点击左下角的 构建 (Ctrl+B)，随后点击 运行 (Ctrl+R) 即可体验。
