/// @file common/cmd.hpp
/// @brief 交互框架：从 stdin 读命令，调 proto::decode* 解析，分发给回调。
/// 被 cli / ser 共用；只管「读 → 解析 → 分发」，不涉及具体业务。