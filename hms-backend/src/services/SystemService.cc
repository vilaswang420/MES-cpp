#include "services/SystemService.hh"

#include <drogon/drogon.h>

#include <map>

#include "common/SqlParam.hh"
#include "services/RbacService.hh"
#include "utils/CpuOffload.hh"
#include "utils/CryptoUtils.hh"

namespace hms::SystemService {

namespace {

constexpr const char* kDefaultPassword = "Hms@123456"; // 重置密码默认值 (强制首登修改由前端提示)

std::string optStr(const drogon::orm::Row& row, const char* field) {
    return row[field].isNull() ? std::string() : row[field].as<std::string>();
}

// 失效持有指定角色用户的权限缓存
void invalidateRoleUsers(int64_t roleId) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT user_id FROM sys_user_roles WHERE role_id = $1",
        [](const drogon::orm::Result& r) {
            for (const auto& row : r)
                RbacService::invalidateUserPerm(row["user_id"].as<int64_t>());
        },
        [](const drogon::orm::DrogonDbException& e) {
            LOG_ERROR << "invalidate role users failed: " << e.base().what();
        },
        SqlArg(roleId));
}

nlohmann::json parseJsonField(const std::string& s) {
    try {
        return nlohmann::json::parse(s);
    } catch (...) {
        return nlohmann::json::array();
    }
}

} // namespace

// ============ 用户 ============

void listUsers(int page, int pageSize, const std::string& keyword, int status, JsonCb onOk,
               ErrCb onErr) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;

    // keyword 走 $1 占位; status/分页为数值安全拼接 (libpq 参数个数必须与占位符严格一致)
    std::string where = "WHERE u.deleted = FALSE";
    bool hasKw = !keyword.empty();
    if (hasKw)
        where += " AND (u.username ILIKE $1 OR u.real_name ILIKE $1 OR u.employee_no ILIKE $1)";
    if (status >= 0)
        where += " AND u.status = " + std::to_string(status);

    std::string listSql =
        "SELECT u.id, u.username, u.real_name, u.employee_no, u.email, u.phone, u.gender, "
        "u.status, u.dept_id, COALESCE(d.dept_name,'') AS dept_name, u.last_login_at, "
        "u.created_at, "
        "(SELECT COALESCE(json_agg(r.role_code), '[]'::json) FROM sys_user_roles ur "
        " JOIN sys_roles r ON r.id = ur.role_id WHERE ur.user_id = u.id) AS roles "
        "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id " +
        where + " ORDER BY u.id LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM sys_users u " + where;

    auto rowHandler = [page, pageSize, countSql, hasKw, keyword, onOk,
                       onErr](const drogon::orm::Result& r) {
        nlohmann::json listArr = nlohmann::json::array();
        for (const auto& row : r) {
            listArr.push_back({
                {"id", row["id"].as<int64_t>()},
                {"username", row["username"].as<std::string>()},
                {"real_name", row["real_name"].as<std::string>()},
                {"employee_no", optStr(row, "employee_no")},
                {"email", optStr(row, "email")},
                {"phone", optStr(row, "phone")},
                {"gender", row["gender"].as<int>()},
                {"status", row["status"].as<int>()},
                {"dept_id", row["dept_id"].isNull() ? 0 : row["dept_id"].as<int64_t>()},
                {"dept_name", row["dept_name"].as<std::string>()},
                {"last_login_at", optStr(row, "last_login_at")},
                {"created_at", optStr(row, "created_at")},
                {"roles", parseJsonField(optStr(row, "roles"))},
            });
        }
        auto db2 = drogon::app().getDbClient();
        auto countOk = [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
            onOk({{"list", listArr},
                  {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        auto countErr = [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); };
        if (hasKw)
            db2->execSqlAsync(countSql, countOk, countErr, "%" + keyword + "%");
        else
            db2->execSqlAsync(countSql, countOk, countErr);
    };
    auto rowErr = [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); };

    auto db = drogon::app().getDbClient();
    if (hasKw)
        db->execSqlAsync(listSql, rowHandler, rowErr, "%" + keyword + "%");
    else
        db->execSqlAsync(listSql, rowHandler, rowErr);
}

void getUser(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT u.id, u.username, u.real_name, u.employee_no, u.email, u.phone, u.gender, "
        "u.status, u.dept_id, COALESCE(d.dept_name,'') AS dept_name, u.created_at "
        "FROM sys_users u LEFT JOIN sys_departments d ON d.id = u.dept_id "
        "WHERE u.id = $1 AND u.deleted = FALSE",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty()) {
                onOk(nullptr); // controller 转 404
                return;
            }
            auto row = r[0];
            nlohmann::json data = {
                {"id", id},
                {"username", row["username"].as<std::string>()},
                {"real_name", row["real_name"].as<std::string>()},
                {"employee_no", optStr(row, "employee_no")},
                {"email", optStr(row, "email")},
                {"phone", optStr(row, "phone")},
                {"gender", row["gender"].as<int>()},
                {"status", row["status"].as<int>()},
                {"dept_id", row["dept_id"].isNull() ? 0 : row["dept_id"].as<int64_t>()},
                {"dept_name", row["dept_name"].as<std::string>()},
                {"created_at", optStr(row, "created_at")},
            };
            // 附带角色 id 列表供前端回显
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "SELECT role_id FROM sys_user_roles WHERE user_id = $1",
                [data, onOk](const drogon::orm::Result& rr) mutable {
                    auto ids = nlohmann::json::array();
                    for (const auto& x : rr)
                        ids.push_back(x["role_id"].as<int64_t>());
                    data["role_ids"] = ids;
                    onOk(data);
                },
                [data, onOk](const drogon::orm::DrogonDbException&) { onOk(data); }, SqlArg(id));
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

void createUser(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto username = body.value("username", "");
    auto realName = body.value("real_name", "");
    if (username.empty() || realName.empty())
        return onErr(400, "username 与 real_name 必填");
    auto password = body.value("password", std::string(kDefaultPassword));
    // bcrypt 为 CPU 密集计算, 卸载到工作线程后再发起入库
    offloadCpu([password] { return CryptoUtils::hashPassword(password); },
               [body, username, realName, onOk, onErr](std::string hash) mutable {
        auto db = drogon::app().getDbClient();
        db->execSqlAsync(
            "INSERT INTO sys_users (dept_id, username, password_hash, real_name, employee_no, "
            "email, phone, gender, status) VALUES (NULLIF($1::bigint,0),$2,$3,$4,$5,$6,$7,$8::int,1) "
            "RETURNING id",
            [body, onOk, onErr](const drogon::orm::Result& r) {
                auto id = r[0]["id"].as<int64_t>();
                // 可选角色分配
                if (body.contains("role_ids") && body["role_ids"].is_array() &&
                    !body["role_ids"].empty()) {
                    assignRoles(
                        id, body["role_ids"].get<std::vector<int64_t>>(),
                        [id, onOk](const nlohmann::json&) { onOk({{"id", id}, {"created", true}}); },
                        onErr);
                    return;
                }
                onOk({{"id", id}, {"created", true}});
            },
            [onErr](const drogon::orm::DrogonDbException& e) {
                auto msg = std::string(e.base().what());
                if (msg.find("unique") != std::string::npos ||
                    msg.find("duplicate") != std::string::npos)
                    onErr(409, "用户名或工号已存在");
                else
                    onErr(500, msg);
            },
            SqlArg(body.value("dept_id", (int64_t)0)), username, hash, realName,
            body.value("employee_no", ""), body.value("email", ""), body.value("phone", ""),
            SqlArg(body.value("gender", 0)));
    });
}

void updateUser(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    std::vector<std::string> sets;
    std::vector<std::string> cols;
    if (body.contains("real_name"))
        cols.push_back("real_name");
    if (body.contains("employee_no"))
        cols.push_back("employee_no");
    if (body.contains("email"))
        cols.push_back("email");
    if (body.contains("phone"))
        cols.push_back("phone");
    if (body.contains("gender"))
        cols.push_back("gender");
    if (body.contains("dept_id"))
        cols.push_back("dept_id");
    if (cols.empty())
        return onErr(400, "无可更新字段");

    // 逐字段单独 UPDATE (参数数量动态, 避免动态拼串)
    auto db = drogon::app().getDbClient();
    auto trans = db->newTransaction();
    // COMMIT 完成后才响应, 避免写后读不一致
    trans->setCommitCallback([onOk, onErr, id](bool committed) {
        if (!committed) {
            onErr(500, "事务提交失败");
            return;
        }
        onOk({{"id", id}, {"updated", true}});
    });
    for (const auto& col : cols) {
        std::string sql = "UPDATE sys_users SET " + col +
                          " = $1, updated_at = NOW() "
                          "WHERE id = $2 AND deleted = FALSE";
        if (col == "dept_id")
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(body["dept_id"].get<int64_t>()),
                SqlArg(id));
        else if (col == "gender")
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(body["gender"].get<int>()),
                SqlArg(id));
        else
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, body[col].get<std::string>(),
                SqlArg(id));
    }
    // trans 函数返回时析构 -> 排队 COMMIT (在上述 UPDATE 之后) -> commitCallback 响应
}

void deleteUser(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE sys_users SET deleted = TRUE, updated_at = NOW() WHERE id = $1",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.affectedRows() == 0) {
                onOk(nullptr); // controller 转 404
                return;
            }
            RbacService::invalidateUserPerm(id);
            onOk({{"id", id}, {"deleted", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

void resetPassword(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto newPwd = body.value("new_password", std::string(kDefaultPassword));
    if (newPwd.size() < 8)
        return onErr(400, "密码长度至少 8 位");
    // bcrypt 为 CPU 密集计算, 卸载到工作线程后再发起更新
    offloadCpu([newPwd] { return CryptoUtils::hashPassword(newPwd); },
               [id, onOk, onErr](std::string hash) mutable {
        auto db = drogon::app().getDbClient();
        db->execSqlAsync(
            "UPDATE sys_users SET password_hash = $1, login_fail_count = 0, "
            "status = CASE WHEN status = 2 THEN 1 ELSE status END, updated_at = NOW() "
            "WHERE id = $2 AND deleted = FALSE",
            [id, onOk](const drogon::orm::Result& r) {
                if (r.affectedRows() == 0) {
                    onOk(nullptr);
                    return;
                }
                onOk({{"id", id}, {"reset", true}});
            },
            [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
            hash, SqlArg(id));
    });
}

void setUserStatus(int64_t id, int status, JsonCb onOk, ErrCb onErr) {
    if (status != 0 && status != 1)
        return onErr(400, "status 仅允许 0(禁用)/1(启用)");
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE sys_users SET status = $1::int, login_fail_count = CASE WHEN $1::int = 1 THEN 0 "
        "ELSE login_fail_count END, updated_at = NOW() WHERE id = $2 AND deleted = FALSE",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.affectedRows() == 0) {
                onOk(nullptr);
                return;
            }
            onOk({{"id", id}, {"updated", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(status), SqlArg(id));
}

void assignRoles(int64_t userId, const std::vector<int64_t>& roleIds, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    auto trans = db->newTransaction();
    trans->execSqlAsync(
        "DELETE FROM sys_user_roles WHERE user_id = $1",
        [trans, userId, roleIds, onOk, onErr](const drogon::orm::Result&) {
            for (auto rid : roleIds)
                trans->execSqlAsync(
                    "INSERT INTO sys_user_roles (user_id, role_id) VALUES ($1,$2) "
                    "ON CONFLICT DO NOTHING",
                    [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                    SqlArg(userId), SqlArg(rid));
            // COMMIT 完成后再响应: 否则调用方紧接着登录会读不到新授权
            trans->setCommitCallback([onOk, onErr, userId, roleIds](bool committed) {
                if (!committed) {
                    onErr(500, "事务提交失败");
                    return;
                }
                RbacService::invalidateUserPerm(userId);
                onOk({{"user_id", userId}, {"assigned", roleIds.size()}});
            });
            // 回调结束后捕获释放 -> 析构排队 COMMIT (在 INSERT 之后)
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(userId));
}

// ============ 角色 ============

void listRoles(JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, role_code, role_name, description, data_scope, sort_order, status, "
        "created_at FROM sys_roles WHERE deleted = FALSE ORDER BY sort_order, id",
        [onOk](const drogon::orm::Result& r) {
            auto listArr = nlohmann::json::array();
            for (const auto& row : r)
                listArr.push_back({
                    {"id", row["id"].as<int64_t>()},
                    {"role_code", row["role_code"].as<std::string>()},
                    {"role_name", row["role_name"].as<std::string>()},
                    {"description", optStr(row, "description")},
                    {"data_scope", row["data_scope"].as<int>()},
                    {"sort_order", row["sort_order"].as<int>()},
                    {"status", row["status"].as<int>()},
                    {"created_at", optStr(row, "created_at")},
                });
            onOk({{"list", listArr}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

void getRole(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, role_code, role_name, description, data_scope, sort_order, status "
        "FROM sys_roles WHERE id = $1 AND deleted = FALSE",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.empty()) {
                onOk(nullptr);
                return;
            }
            auto row = r[0];
            nlohmann::json data = {
                {"id", id},
                {"role_code", row["role_code"].as<std::string>()},
                {"role_name", row["role_name"].as<std::string>()},
                {"description", optStr(row, "description")},
                {"data_scope", row["data_scope"].as<int>()},
                {"sort_order", row["sort_order"].as<int>()},
                {"status", row["status"].as<int>()},
            };
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "SELECT permission_id FROM sys_role_permissions WHERE role_id = $1",
                [data, onOk](const drogon::orm::Result& rr) mutable {
                    auto ids = nlohmann::json::array();
                    for (const auto& x : rr)
                        ids.push_back(x["permission_id"].as<int64_t>());
                    data["permission_ids"] = ids;
                    onOk(data);
                },
                [data, onOk](const drogon::orm::DrogonDbException&) { onOk(data); }, SqlArg(id));
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

void createRole(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto code = body.value("role_code", "");
    auto name = body.value("role_name", "");
    if (code.empty() || name.empty())
        return onErr(400, "role_code 与 role_name 必填");
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "INSERT INTO sys_roles (role_code, role_name, description, data_scope, sort_order, "
        "status) VALUES ($1,$2,$3,$4,$5,1) RETURNING id",
        [onOk](const drogon::orm::Result& r) {
            onOk({{"id", r[0]["id"].as<int64_t>()}, {"created", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) {
            auto msg = std::string(e.base().what());
            onErr(msg.find("duplicate") != std::string::npos ? 409 : 500,
                  msg.find("duplicate") != std::string::npos ? "角色编码已存在" : msg);
        },
        code, name, body.value("description", ""), SqlArg(body.value("data_scope", 1)),
        SqlArg(body.value("sort_order", 0)));
}

void updateRole(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    // 简化: 单字段动态更新循环 (同 updateUser)
    std::vector<std::string> cols = {"role_name", "description", "sort_order", "status"};
    bool any = false;
    for (const auto& col : cols)
        if (body.contains(col))
            any = true;
    if (!any)
        return onErr(400, "无可更新字段");
    auto trans = db->newTransaction();
    trans->setCommitCallback([onOk, onErr, id](bool committed) {
        if (!committed) {
            onErr(500, "事务提交失败");
            return;
        }
        onOk({{"id", id}, {"updated", true}});
    });
    for (const auto& col : cols) {
        if (!body.contains(col))
            continue;
        any = true;
        std::string sql = "UPDATE sys_roles SET " + col +
                          " = $1, updated_at = NOW() "
                          "WHERE id = $2 AND deleted = FALSE";
        if (col == "sort_order" || col == "status")
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(body[col].get<int>()),
                SqlArg(id));
        else
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, body[col].get<std::string>(),
                SqlArg(id));
    }
    invalidateRoleUsers(id); // 角色变更影响授权缓存
}

void deleteRole(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE sys_roles SET deleted = TRUE, updated_at = NOW() WHERE id = $1",
        [id, onOk](const drogon::orm::Result& r) {
            if (r.affectedRows() == 0) {
                onOk(nullptr);
                return;
            }
            invalidateRoleUsers(id);
            onOk({{"id", id}, {"deleted", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

void assignPermissions(int64_t roleId, const std::vector<int64_t>& permIds, JsonCb onOk,
                       ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    auto trans = db->newTransaction();
    trans->execSqlAsync(
        "DELETE FROM sys_role_permissions WHERE role_id = $1",
        [trans, roleId, permIds, onOk, onErr](const drogon::orm::Result&) {
            for (auto pid : permIds)
                trans->execSqlAsync(
                    "INSERT INTO sys_role_permissions (role_id, permission_id) VALUES ($1,$2) "
                    "ON CONFLICT DO NOTHING",
                    [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                    SqlArg(roleId), SqlArg(pid));
            // COMMIT 完成后再响应, 避免后续读不到新授权
            trans->setCommitCallback([onOk, onErr, roleId, permIds](bool committed) {
                if (!committed) {
                    onErr(500, "事务提交失败");
                    return;
                }
                invalidateRoleUsers(roleId);
                onOk({{"role_id", roleId}, {"assigned", permIds.size()}});
            });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(roleId));
}

void updateDataScope(int64_t roleId, int dataScope, const std::vector<int64_t>& deptIds,
                     JsonCb onOk, ErrCb onErr) {
    if (dataScope < 1 || dataScope > 5)
        return onErr(400, "data_scope 仅允许 1-5");
    if (dataScope == 5 && deptIds.empty())
        return onErr(400, "自定义数据范围必须指定部门集合");
    auto db = drogon::app().getDbClient();
    auto trans = db->newTransaction();
    trans->execSqlAsync(
        "UPDATE sys_roles SET data_scope = $1, updated_at = NOW() WHERE id = $2",
        [trans, roleId, dataScope, deptIds, onOk, onErr](const drogon::orm::Result&) {
            if (dataScope == 5) {
                trans->execSqlAsync(
                    "DELETE FROM sys_role_dept_scope WHERE role_id = $1",
                    [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                    SqlArg(roleId));
                for (auto did : deptIds)
                    trans->execSqlAsync(
                        "INSERT INTO sys_role_dept_scope (role_id, dept_id) VALUES ($1,$2)",
                        [](const drogon::orm::Result&) {},
                        [](const drogon::orm::DrogonDbException&) {}, SqlArg(roleId), SqlArg(did));
            } else {
                // 非自定义档位清空历史部门集合
                trans->execSqlAsync(
                    "DELETE FROM sys_role_dept_scope WHERE role_id = $1",
                    [](const drogon::orm::Result&) {}, [](const drogon::orm::DrogonDbException&) {},
                    SqlArg(roleId));
            }
            // COMMIT 完成后再响应, 避免后续读不到新数据范围
            trans->setCommitCallback([onOk, onErr, roleId, dataScope](bool committed) {
                if (!committed) {
                    onErr(500, "事务提交失败");
                    return;
                }
                invalidateRoleUsers(roleId);
                onOk({{"role_id", roleId}, {"data_scope", dataScope}});
            });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(dataScope), SqlArg(roleId));
}

// ============ 权限树 / 部门 ============

void permissionTree(JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, parent_id, perm_code, perm_name, perm_type, path, method, icon, "
        "sort_order, visible, status FROM sys_permissions ORDER BY sort_order, id",
        [onOk](const drogon::orm::Result& r) {
            std::map<int64_t, nlohmann::json> nodes;
            std::vector<std::pair<int64_t, int64_t>> edges; // (child, parent)
            for (const auto& row : r) {
                auto id = row["id"].as<int64_t>();
                nodes[id] = {
                    {"id", id},
                    {"perm_code", row["perm_code"].as<std::string>()},
                    {"perm_name", row["perm_name"].as<std::string>()},
                    {"perm_type", row["perm_type"].as<int>()},
                    {"path", optStr(row, "path")},
                    {"method", optStr(row, "method")},
                    {"icon", optStr(row, "icon")},
                    {"sort_order", row["sort_order"].as<int>()},
                    {"visible", row["visible"].as<bool>()},
                    {"status", row["status"].as<int>()},
                    {"children", nlohmann::json::array()},
                };
                edges.emplace_back(id,
                                   row["parent_id"].isNull() ? 0 : row["parent_id"].as<int64_t>());
            }
            auto tree = nlohmann::json::array();
            for (const auto& [child, parent] : edges) {
                if (parent == 0 || !nodes.count(parent))
                    tree.push_back(nodes[child]);
                else
                    nodes[parent]["children"].push_back(nodes[child]);
            }
            onOk({{"tree", tree}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

void deptTree(JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, parent_id, dept_code, dept_name, sort_order, leader_id, phone, status "
        "FROM sys_departments WHERE deleted = FALSE ORDER BY sort_order, id",
        [onOk](const drogon::orm::Result& r) {
            std::map<int64_t, nlohmann::json> nodes;
            std::vector<std::pair<int64_t, int64_t>> edges;
            for (const auto& row : r) {
                auto id = row["id"].as<int64_t>();
                nodes[id] = {
                    {"id", id},
                    {"dept_code", row["dept_code"].as<std::string>()},
                    {"dept_name", row["dept_name"].as<std::string>()},
                    {"sort_order", row["sort_order"].as<int>()},
                    {"leader_id", row["leader_id"].isNull() ? 0 : row["leader_id"].as<int64_t>()},
                    {"phone", optStr(row, "phone")},
                    {"status", row["status"].as<int>()},
                    {"children", nlohmann::json::array()},
                };
                edges.emplace_back(id,
                                   row["parent_id"].isNull() ? 0 : row["parent_id"].as<int64_t>());
            }
            auto tree = nlohmann::json::array();
            for (const auto& [child, parent] : edges) {
                if (parent == 0 || !nodes.count(parent))
                    tree.push_back(nodes[child]);
                else
                    nodes[parent]["children"].push_back(nodes[child]);
            }
            onOk({{"tree", tree}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

void createDept(const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto code = body.value("dept_code", "");
    auto name = body.value("dept_name", "");
    if (code.empty() || name.empty())
        return onErr(400, "dept_code 与 dept_name 必填");
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "INSERT INTO sys_departments (parent_id, dept_code, dept_name, sort_order, leader_id, "
        "phone, status) VALUES (NULLIF($1::bigint,0),$2,$3,$4::int,NULLIF($5::bigint,0),$6,1) RETURNING id",
        [onOk](const drogon::orm::Result& r) {
            onOk({{"id", r[0]["id"].as<int64_t>()}, {"created", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) {
            auto msg = std::string(e.base().what());
            onErr(msg.find("duplicate") != std::string::npos ? 409 : 500,
                  msg.find("duplicate") != std::string::npos ? "部门编码已存在" : msg);
        },
        SqlArg(body.value("parent_id", (int64_t)0)), code, name,
        SqlArg(body.value("sort_order", 0)), SqlArg(body.value("leader_id", (int64_t)0)),
        body.value("phone", ""));
}

void updateDept(int64_t id, const nlohmann::json& body, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    std::vector<std::string> cols = {"dept_name", "sort_order", "leader_id", "phone", "status"};
    bool any = false;
    for (const auto& col : cols)
        if (body.contains(col))
            any = true;
    if (!any)
        return onErr(400, "无可更新字段");
    auto trans = db->newTransaction();
    trans->setCommitCallback([onOk, onErr, id](bool committed) {
        if (!committed) {
            onErr(500, "事务提交失败");
            return;
        }
        onOk({{"id", id}, {"updated", true}});
    });
    for (const auto& col : cols) {
        if (!body.contains(col))
            continue;
        any = true;
        std::string sql = "UPDATE sys_departments SET " + col +
                          " = $1, updated_at = NOW() WHERE id = $2 AND deleted = FALSE";
        if (col == "sort_order" || col == "status")
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(body[col].get<int>()),
                SqlArg(id));
        else if (col == "leader_id")
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(body[col].get<int64_t>()),
                SqlArg(id));
        else
            trans->execSqlAsync(
                sql, [](const drogon::orm::Result&) {},
                [](const drogon::orm::DrogonDbException&) {}, body[col].get<std::string>(),
                SqlArg(id));
    }
}

void deleteDept(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    // 存在子部门或用户时禁止删除
    db->execSqlAsync(
        "SELECT (SELECT COUNT(*) FROM sys_departments WHERE parent_id = $1 AND deleted = FALSE)"
        " + (SELECT COUNT(*) FROM sys_users WHERE dept_id = $1 AND deleted = FALSE) AS refs",
        [id, onOk](const drogon::orm::Result& r) {
            if (!r.empty() && r[0]["refs"].as<int64_t>() > 0) {
                onOk(nlohmann::json{{"conflict", true}}); // controller 转 409
                return;
            }
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "UPDATE sys_departments SET deleted = TRUE, updated_at = NOW() WHERE id = $1",
                [id, onOk](const drogon::orm::Result& ur) {
                    if (ur.affectedRows() == 0) {
                        onOk(nullptr);
                        return;
                    }
                    onOk({{"id", id}, {"deleted", true}});
                },
                [](const drogon::orm::DrogonDbException&) {}, SqlArg(id));
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

// ============ 审计与配置 ============

void listAuditLogs(int page, int pageSize, int64_t userId, const std::string& module, JsonCb onOk,
                   ErrCb onErr) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;
    // 数值条件安全拼接; module 字符串走 $1 占位防注入
    std::string where = "WHERE 1=1";
    if (userId > 0)
        where += " AND user_id = " + std::to_string(userId);
    if (!module.empty())
        where += " AND module = $1";
    std::string sql =
        "SELECT id, user_id, username, module, operation, method, request_url, response_code, "
        "ip_address, duration_ms, created_at FROM sys_audit_logs " +
        where + " ORDER BY created_at DESC LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM sys_audit_logs " + where;
    auto db = drogon::app().getDbClient();
    auto handler = [page, pageSize, countSql, module, onOk, onErr](const drogon::orm::Result& r) {
        auto listArr = nlohmann::json::array();
        for (const auto& row : r)
            listArr.push_back({
                {"id", row["id"].as<int64_t>()},
                {"user_id", row["user_id"].isNull() ? 0 : row["user_id"].as<int64_t>()},
                {"username", optStr(row, "username")},
                {"module", optStr(row, "module")},
                {"operation", optStr(row, "operation")},
                {"method", optStr(row, "method")},
                {"request_url", optStr(row, "request_url")},
                {"response_code",
                 row["response_code"].isNull() ? 0 : row["response_code"].as<int>()},
                {"ip_address", optStr(row, "ip_address")},
                {"duration_ms", row["duration_ms"].isNull() ? 0 : row["duration_ms"].as<int>()},
                {"created_at", optStr(row, "created_at")},
            });
        // 补 total (与 listUsers 一致的分页信封)
        auto db2 = drogon::app().getDbClient();
        auto countOk = [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
            onOk({{"list", listArr},
                  {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        auto countErr = [listArr, page, pageSize, onOk](const drogon::orm::DrogonDbException&) {
            // 计数失败不阻断列表返回 (total 降级为列表长度)
            onOk({{"list", listArr},
                  {"total", (int64_t)listArr.size()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        if (!module.empty())
            db2->execSqlAsync(countSql, countOk, countErr, module);
        else
            db2->execSqlAsync(countSql, countOk, countErr);
    };
    auto errHandler = [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); };
    if (!module.empty())
        db->execSqlAsync(sql, handler, errHandler, module);
    else
        db->execSqlAsync(sql, handler, errHandler);
}

void getAuditLog(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT id, user_id, username, module, operation, method, request_url, request_params, "
        "response_code, error_msg, ip_address, user_agent, duration_ms, created_at "
        "FROM sys_audit_logs WHERE id = $1",
        [onOk](const drogon::orm::Result& r) {
            if (r.empty()) {
                onOk(nullptr);
                return;
            }
            auto row = r[0];
            onOk({
                {"id", row["id"].as<int64_t>()},
                {"user_id", row["user_id"].isNull() ? 0 : row["user_id"].as<int64_t>()},
                {"username", optStr(row, "username")},
                {"module", optStr(row, "module")},
                {"operation", optStr(row, "operation")},
                {"method", optStr(row, "method")},
                {"request_url", optStr(row, "request_url")},
                {"request_params", optStr(row, "request_params")},
                {"response_code",
                 row["response_code"].isNull() ? 0 : row["response_code"].as<int>()},
                {"error_msg", optStr(row, "error_msg")},
                {"ip_address", optStr(row, "ip_address")},
                {"user_agent", optStr(row, "user_agent")},
                {"duration_ms", row["duration_ms"].isNull() ? 0 : row["duration_ms"].as<int>()},
                {"created_at", optStr(row, "created_at")},
            });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, SqlArg(id));
}

void listConfigs(JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT config_key, config_value, config_type, description, category, is_system "
        "FROM sys_configs ORDER BY category, config_key",
        [onOk](const drogon::orm::Result& r) {
            auto listArr = nlohmann::json::array();
            for (const auto& row : r)
                listArr.push_back({
                    {"config_key", row["config_key"].as<std::string>()},
                    {"config_value", optStr(row, "config_value")},
                    {"config_type", row["config_type"].as<std::string>()},
                    {"description", optStr(row, "description")},
                    {"category", optStr(row, "category")},
                    {"is_system", row["is_system"].as<bool>()},
                });
            onOk({{"list", listArr}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

void updateConfig(const std::string& key, const std::string& value, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE sys_configs SET config_value = $1, updated_at = NOW() WHERE config_key = $2",
        [key, onOk](const drogon::orm::Result& r) {
            if (r.affectedRows() == 0) {
                onOk(nullptr);
                return;
            }
            onOk({{"config_key", key}, {"updated", true}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); }, value, key);
}

} // namespace hms::SystemService
