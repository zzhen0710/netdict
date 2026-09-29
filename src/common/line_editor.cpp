/// @file common/line_editor.cpp
/// @brief LineEditor + EditorHistory 的实现。

#include "common/line_editor.hpp"

#include <cstdlib>      // std::getenv
#include <stdexcept>    // std::runtime_error

// ---------------- LineEditor ----------------

/// 构造：存下 ifd/ofd/prompt，EditStart 进入编辑。
LineEditor::LineEditor(int ifd, int ofd, const char* prompt)
    : editing_(false),
      ifd_(ifd), ofd_(ofd), prompt_(prompt) {

    // 复用 start()：设 editing_ + EditStart。
    // 失败时 start() 抛异常，构造传播；editing_ 仍为 false，析构不 Stop。
    start();
}

/// 析构：EditStop（若还在编辑），恢复终端。
LineEditor::~LineEditor() {
    stop();   // 幂等
}

/// 喂一个事件：让 linenoise 处理内部输入。
LineEditor::FeedResult LineEditor::feed() {
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

/// 手动退出编辑（回正常模式）；幂等。
void LineEditor::stop() {
    if (editing_) {
        linenoiseEditStop(&state_);   // 恢复 raw mode / 关闭 bracketed paste
        editing_ = false;
    }
}

/// 手动进入编辑；幂等。
void LineEditor::start() {
    if (editing_) return;   // 已在编辑，无需重复

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