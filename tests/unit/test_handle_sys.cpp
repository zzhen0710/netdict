/// test_handle_sys.cpp — 管理终端 handler 冒烟测试
///
/// 通过 friend class TestServer 访问 Server 的 private doXxx。
/// 能直接断言的：doAdd / doDel / doUpdate（改库后用 dict_.query 反查验证）。
/// 只输出到 stdout 的（doView / doList / doNum / doStat / doHelp）：只测"不崩"。

#include "ser/ser.hpp"
#include "common/logger.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

/// 测试桥：转发到 Server 的 private 方法（friend）。
/// 本身不含逻辑，只是把 private 方法暴露成 public 静态函数，
/// 供 main 里的测试调用。
class TestServer {
public:
    static void doAdd   (Server& s, const proto::Msg& m) { s.doAdd(m); }
    static void doDel   (Server& s, const proto::Msg& m) { s.doDel(m); }
    static void doUpdate(Server& s, const proto::Msg& m) { s.doUpdate(m); }
    static void doView  (Server& s, const proto::Msg& m) { s.doView(m); }
    static void doList  (Server& s, const proto::Msg& m) { s.doList(m); }
    static void doNum   (Server& s, const proto::Msg& m) { s.doNum(m); }
    static void doStat  (Server& s, const proto::Msg& m) { s.doStat(m); }
    static void doLog   (Server& s, const proto::Msg& m) { s.doLog(m); }
};

/// 造 Msg 的范式小工具（cmd 由调用方补，这里只填 args）。
/// 用法：mkMsg({"word", "n.", "释义"})，省去手动构造 Msg 和 push_back。
/// @param args  花括号里的若干字符串，会被拷进 Msg.args
/// @return      填好 args 的 Msg（cmd 字段仍为空，由调用方设置）
static proto::Msg mkMsg(std::initializer_list<std::string> args) {
    proto::Msg m;
    // initializer_list 只是只读视图，这里用 assign 把内容真正拷进 m.args，
    // 避免它随表达式结束而失效
    m.args.assign(args.begin(), args.end());

    return m;
}

int main() {
    // 两个仓储都用内存库：互不干扰，跑完即弃
    DictRepo dict(":memory:");
    UsrRepo  usr(":memory:");
    Server   srv(dict, usr, "127.0.0.1", 0);   // port 0：内核随机，避免占用真实端口

    // ==================== doAdd + query 验证 ====================
    // 加词后回查，确认 pos / mean 都写进去了
    {
        auto m = mkMsg({"testword", "n.", "测试词"});
        TestServer::doAdd(srv, m);

        std::vector<Meaning> out;
        assert(dict.query("testword", out) == status::Query::Ok);
        assert(out.size() == 1);
        assert(out[0].pos  == "n.");
        assert(out[0].mean == "测试词");
    }
    std::cout << "[OK] doAdd\n";

    // ==================== doUpdate ====================
    // 改词后回查，确认旧释义被替换（不是追加）
    {
        auto m = mkMsg({"testword", "v.", "改后释义"});
        TestServer::doUpdate(srv, m);

        std::vector<Meaning> out;
        assert(dict.query("testword", out) == status::Query::Ok);
        assert(out[0].pos  == "v.");
        assert(out[0].mean == "改后释义");
    }
    std::cout << "[OK] doUpdate\n";

    // ==================== doDel ====================
    // 删词后回查，应查不到
    {
        auto m = mkMsg({"testword"});
        TestServer::doDel(srv, m);

        std::vector<Meaning> out;
        assert(dict.query("testword", out) == status::Query::NotFound);
    }
    std::cout << "[OK] doDel\n";

    // ==================== doLog：改 logger::g_level ====================
    // 直接断言全局日志等级被改到预期值
    {
        auto m = mkMsg({"warn"});
        TestServer::doLog(srv, m);
        assert(logger::g_level == logger::Level::Warn);

        // 再切回 info，确认可反复设置
        auto m2 = mkMsg({"info"});
        TestServer::doLog(srv, m2);
        assert(logger::g_level == logger::Level::Info);
    }
    std::cout << "[OK] doLog\n";

    // ==================== 只输出型：只测"不崩" ====================
    // 这些函数只往 stdout 打，没有可断言的返回值，
    // 能跑过不崩、不抛异常即算通过
    {
        TestServer::doNum (srv, mkMsg({}));
        TestServer::doView(srv, mkMsg({"nonexist"}));   // not_found
        TestServer::doList(srv, mkMsg({"a*"}));         // 空结果
        TestServer::doStat(srv, mkMsg({"nobody"}));     // 不存在用户
    }
    std::cout << "[OK] doNum/doView/doList/doStat (不崩)\n";

    std::cout << "ALL OK\n";
    return 0;
}