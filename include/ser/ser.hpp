/// @file ser/ser.hpp
/// @brief 服务器主类：监听端口、接收连接、分发请求、管理生命周期。
/// 依赖 common/ 与 db/；通过线程池并发处理请求。