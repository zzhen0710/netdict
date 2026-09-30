# linenoise

- **来源**：https://github.com/antirez/linenoise
- **许可**：BSD-2-Clause
- **版本**：master @ a473823（拉取于 2026-09-28）
- **用途**：为 cli / 管理终端提供行编辑 + 历史（`↑/↓` 翻）

## 本项目用法

**统一经 `common/line_editor.{hpp,cpp}` 封装**，不直接调 linenoise：

- **`LineEditor`**：编辑会话 RAII（构造 `EditStart`，析构 `EditStop`）。
  - `feed()` → `More` / `Line` / `Eof`
  - `stop()` / `start()`：手动切换（打印前后）
  - **tty 适配**：`isatty` 假时**不走 linenoise**，`feed` 走 `std::getline`
- **`EditorHistory`**：历史 RAII（构造 `SetMaxLen` + `Load`，析构 `Save`）。
  - `add(line)`

**cli** 用 `netdict> ` 提示符；**管理终端**同（`ser`）。

## 本地改动

无。