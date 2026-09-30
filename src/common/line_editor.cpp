/// @file common/line_editor.cpp
/// @brief LineEditor + EditorHistory 的实现。

#include "common/line_editor.hpp"

#include <cstdlib>      // std::getenv
#include <stdexcept>    // std::runtime_error
#include <unistd.h>     // isatty
#include <iostream>

// ---------------- LineEditor ----------------

/// 构造：存下 ifd/ofd/prompt，tty 时 EditStart 进入编辑。
///
/// tty（交互终端）  → 走 linenoise 行编辑（start()）。
/// 非 tty（管道/重定向）→ 不 start()（enableRawMode 会失败）；
///                        feed() 改用 std::getline 逐行读。
///
/// start() 失败时抛异常，构造传播；editing_ 仍为 false，析构不 Stop。
LineEditor::LineEditor(int ifd, int ofd, const char* prompt)
    : editing_(false),
      ifd_(ifd), ofd_(ofd), prompt_(prompt),
      tty_(isatty(ifd) != 0) {

    if (tty_) {
        start();
    }
}

/// 析构：EditStop（若还在编辑），恢复终端。
LineEditor::~LineEditor() {
    stop();   // 幂等
}

/// 喂一个事件：读一行或让 linenoise 处理按键。
/// tty  → 走 linenoise（More / Line / Eof）。
/// 非 tty → std::getline 读一行（Line / Eof）。
LineEditor::FeedResult LineEditor::feed() {
    // 非 tty：直接读一行，无行编辑 / 历史。
    if (!tty_) {
        std::string line;
        if (!std::getline(std::cin, line)) {
            return FeedResult::Eof;       // EOF
        }
        line_ = line;
        return FeedResult::Line;
    }

    // tty：走 linenoise 行编辑
    char* res = linenoiseEditFeed(&state_);

    // 还在编辑（未回车）：linenoiseEditMore 是哨兵指针，不能 free。
    if (res == linenoiseEditMore) {
        return FeedResult::More;
    }

    // EOF（Ctrl+D）/ Ctrl+C：linenoise 返回 nullptr。
    if (res == nullptr) {
        return FeedResult::Eof;
    }

    // 拿到整行：转成 std::string 存 line_，释放 malloc 串。
    line_ = res;
    linenoiseFree(res);
    return FeedResult::Line;
}

/// 取整行。
std::string LineEditor::line() const {
    return line_;
}

/// 手动退出编辑；幂等。
void LineEditor::stop() {
    if (!editing_) return;

    // 非 tty：没进过 linenoise，无需恢复终端。
    if (tty_) {
        linenoiseEditStop(&state_); // 恢复 raw mode / 关闭 bracketed paste
    }
    editing_ = false;
}

/// 手动进入编辑；幂等。
void LineEditor::start() {
    if (editing_) return;   // 已在编辑，无需重复

    // 非 tty：不走 linenoise（enableRawMode 会失败），
    // 只标记"在编辑"，feed() 走 std::getline。
    if (!tty_) {
        editing_ = true;
        return;
    }

    if (linenoiseEditStart(&state_, ifd_, ofd_,
                           buf_, sizeof(buf_), prompt_.c_str()) < 0) {
        throw std::runtime_error("linenoiseEditStart failed");
    }
    editing_ = true;
}

// ---------------- EditorHistory ----------------

/// 构造：设内存历史上限 + 从文件加载。
EditorHistory::EditorHistory(int max_len, const char* file)
    : path_(resolvePath(file)) {      // 拼完整路径

    // 设上限（超出淘汰最旧）。
    linenoiseHistorySetMaxLen(max_len);

    // 从文件加载（文件不存在时返回 -1，视为空历史，不报错）。
    linenoiseHistoryLoad(path_.c_str());
}

/// 析构：保存历史到文件（全量覆盖）。
EditorHistory::~EditorHistory() {
    linenoiseHistorySave(path_.c_str());
}

/// 加一行到内存历史。
void EditorHistory::add(const char* line) {
    linenoiseHistoryAdd(line);
}

/// 历史文件完整路径：$HOME + file；无 HOME 兜底当前目录。
std::string EditorHistory::resolvePath(const char* file) {
    // file 以 '/' 开头（如 "/.netdict_history"）：直接拼到家目录。
    const char* home = std::getenv("HOME");
    return home ? std::string(home) + file
                : std::string(".") + file;   // 无 HOME：当前目录
}