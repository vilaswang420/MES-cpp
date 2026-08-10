#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 数据级权限 SQL 条件注入 (设计文档 5.4 节)。
// 纯函数实现, 可独立单测 (tests/test_data_scope_filter.cc)。
// 多角色合并规则: 登录签发 JWT 前取最宽 (4>3>2>1), 运行时只读 JWT 合成结果。
namespace hms::DataScopeFilter {

inline std::string buildDeptCondition(int dataScope, int64_t userId, int64_t deptId,
                                      const std::string& deptColumn = "dept_id") {
    switch (dataScope) {
    case 1: // 仅本人: 按业务表创建人过滤
        return "created_by = " + std::to_string(userId);
    case 2: // 本部门
        return deptColumn + " = " + std::to_string(deptId);
    case 3: // 本部门及所有子部门 (递归 CTE 下钻任意层级)
        return deptColumn +
               " IN ("
               "WITH RECURSIVE dept_tree(id) AS ("
               "  SELECT id FROM sys_departments WHERE id = " +
               std::to_string(deptId) +
               "  UNION ALL "
               "  SELECT d.id FROM sys_departments d "
               "  JOIN dept_tree t ON d.parent_id = t.id"
               ") SELECT id FROM dept_tree)";
    case 4: // 全部
        return "1=1";
    case 5: // 自定义部门集合
        return deptColumn +
               " IN (SELECT dept_id FROM sys_role_dept_scope "
               "WHERE role_id IN (SELECT role_id FROM sys_user_roles WHERE user_id = " +
               std::to_string(userId) + "))";
    default:
        return "1=0"; // 未知档位一律拒绝 (fail-closed)
    }
}

// 多角色 data_scope 取最宽合并 (优先级 4 > 3 > 2 > 1; 5:自定义单独合并部门集合)
// 返回合成后的档位; 若合成结果为 5, customDeptIds 为各角色自定义部门并集。
inline int mergeDataScope(const std::vector<int>& scopes) {
    int widest = 0;
    for (int s : scopes) {
        if (s == 4)
            return 4; // 任一角色为全部, 合成即为全部
        if (s != 5 && s > widest)
            widest = s;
    }
    // 无 4 时: 存在非自定义档位取最宽; 全部为自定义则保持 5
    return widest > 0 ? widest : (scopes.empty() ? 1 : 5);
}

} // namespace hms::DataScopeFilter
