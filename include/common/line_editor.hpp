/// @file common/line_editor.hpp
/// @brief linenoise 封装，cli / ser 共用：编辑会话（LineEditor）+ 历史（EditorHistory）。

/*
 * linenoise.h — 轻量命令行编辑库
 *
 * 来源：https://github.com/antirez/linenoise
 * 许可：BSD-2-Clause（见源码头注释）
 * 用途：为本项目 cli / ser 提供行编辑 + 历史（上下键翻）
 *
 * 本封装用到的接口：
 *   linenoiseEditStart()   进入非阻塞编辑（raw mode）
 *   linenoiseEditFeed()    喂一个事件；返回整行 / 还在编辑 / EOF
 *   linenoiseEditStop()    退出编辑（恢复终端）
 *   linenoiseEditMore      哨兵：EditFeed 返回它表示"还在编辑"
 *   linenoiseFree()        释放 EditFeed 返回的整行
 *   linenoiseHistorySetMaxLen()  设内存历史条数上限
 *   linenoiseHistoryAdd()        加一行到内存历史（供 ↑/↓）
 *   linenoiseHistoryLoad()       从文件加载历史
 *   linenoiseHistorySave()       保存历史到文件
 *
 * 改动：无（原文件）
 */

#pragma once

#include "linenoise.h"      // struct linenoiseState

#include <string>

/// linenoise 编辑会话的 RAII 封装。
/// 构造进入编辑（raw mode），析构恢复终端；feed() 喂事件。
/// 支持手动 stop() / start()：拿到整行后先 stop()（回正常模式，才能打印），
/// 处理完再 start() 继续读下一行。
class LineEditor {
public:
    /// feed() 的结果
    enum class FeedResult {
        More,   ///< 还在编辑，无整行
        Line,   ///< 拿到整行（line() 取）
        Eof,    ///< EOF（Ctrl+D）/ Ctrl+C
    };

    /// 构造：EditStart，进入 raw mode 编辑。
    /// @param ifd     输入 fd（通常 STDIN_FILENO）
    /// @param ofd     输出 fd（通常 STDOUT_FILENO）
    /// @param prompt  提示符（如 "netdict> "）
    /// @throws std::runtime_error 初始化失败（写 prompt 失败）
    LineEditor(int ifd, int ofd, const char* prompt);

    /// 析构：EditStop（若还在编辑），恢复终端。
    ~LineEditor();

    LineEditor(const LineEditor&) = delete;
    LineEditor& operator=(const LineEditor&) = delete;

    /// 喂一个事件：让 linenoise 处理内部输入。
    /// @return More（继续）/ Line（line() 取整行）/ Eof
    FeedResult feed();

    /// 取当前编辑到的整行（feed() 返回 Line 后有效）。
    std::string line() const;

    /// 手动退出编辑（回正常模式）；幂等。
    /// 用于"拿到整行后要打印/处理，处理完再 start() 继续"。
    void stop();

    /// 手动进入编辑；幂等。
    /// @throws std::runtime_error EditStart 失败
    void start();

private:
    struct linenoiseState state_;      ///< linenoise 编辑状态
    char                  buf_[1024];  ///< 编辑缓冲（linenoise 内部用）
    std::string           line_;       ///< feed() 返回 Line 时的整行
    bool                  editing_;   ///< 是否在编辑（start/stop 据此判重，重复调不做事）
    int                   ifd_;        ///< 输入 fd（start 用）
    int                   ofd_;        ///< 输出 fd（start 用）
    std::string           prompt_;     ///< 提示符（start 用）
    bool                  tty_;        ///< ifd 是否 tty（非 tty 时不做行编辑）
};

/// linenoise 内存历史的 RAII 封装。
/// 构造设上限 + 从文件加载；析构保存到文件。
/// 历史是"进程级"数据：创建一次、用到底，不要每读一行重建。
class EditorHistory {
public:
    /// 构造：设内存历史上限 + 从文件加载（文件不存在则空）。
    /// @param max_len  内存历史最多几条（超出的最旧淘汰）
    /// @param file     历史文件名（如 "/.netdict_history"，拼到家目录）
    EditorHistory(int max_len, const char* file);

    /// 析构：保存历史到文件（全量覆盖）。
    ~EditorHistory();

    EditorHistory(const EditorHistory&) = delete;
    EditorHistory& operator=(const EditorHistory&) = delete;

    /// 加一行到内存历史（供 ↑/↓ 翻）。空行由调用方决定要不要加。
    void add(const char* line);

private:
    /// 历史文件完整路径：$HOME + file；无 HOME 兜底当前目录。
    static std::string resolvePath(const char* file);

    std::string path_;   ///< 历史文件完整路径（析构 Save 用）
};