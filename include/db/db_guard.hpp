/// @file db/db_guard.hpp
/// @brief RAII 封装：自动管理 sqlite3* 与 sqlite3_stmt* 的生命周期。
/// 禁止拷贝；只被 db/ 与 ser/ 使用。