#pragma once

#include <string>

// fail-closed 权限路由注册表 (计划任务 10)。
// 所有受保护路由必须在此集中注册 (path 模式 + HTTP 方法 -> 权限码);
// 未注册路由一律 403。CI 门禁 scripts/check_perm_mapping.py 扫描
// Controller 的 ADD_METHOD_TO 声明并与本表比对, 缺失即构建失败。
//
// path 模式支持 {param} 通配段, 与 Drogon 路由的 {id} 占位符一致, 例如:
//   "/api/v1/system/users/{id}/reset-password"
namespace hms::PermRoutes {

// 初始化注册表 (main.cc 启动时调用一次)
void init();

// 查询路由所需权限码; 返回空串表示未注册 (fail-closed 拒绝)
std::string getPermission(const std::string& path, const std::string& method);

// 是否公开路径 (无需认证): login/captcha/healthz
bool isPublicPath(const std::string& path, const std::string& method);

} // namespace hms::PermRoutes
