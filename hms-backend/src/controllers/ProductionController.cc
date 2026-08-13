#include <drogon/HttpController.h>

#include <cstdio>
#include <ctime>

#include "controllers/Common.hh"
#include "common/SqlParam.hh"

namespace hms {

// 生产主数据与计划 (计划任务 13/16 / 设计文档 4.6 节)。
// M1 交付: 只读列表 + 主数据创建 (产品/产线/工艺含步骤) + 计划维护;
// 新增路由必须同步 perm_routes.cc 与 002_seed (CI 权限映射门禁)。
class ProductionController : public drogon::HttpController<ProductionController> {
  public:
    METHOD_LIST_BEGIN
    ADD_METHOD_TO(ProductionController::lines, "/api/v1/production/lines", drogon::Get);
    ADD_METHOD_TO(ProductionController::createLine, "/api/v1/production/lines", drogon::Post);
    ADD_METHOD_TO(ProductionController::stations, "/api/v1/production/lines/{1}/stations",
                  drogon::Get);
    ADD_METHOD_TO(ProductionController::processes, "/api/v1/production/processes", drogon::Get);
    ADD_METHOD_TO(ProductionController::createProcess, "/api/v1/production/processes",
                  drogon::Post);
    ADD_METHOD_TO(ProductionController::products, "/api/v1/production/products", drogon::Get);
    ADD_METHOD_TO(ProductionController::createProduct, "/api/v1/production/products", drogon::Post);
    ADD_METHOD_TO(ProductionController::plans, "/api/v1/production/plans", drogon::Get);
    ADD_METHOD_TO(ProductionController::createPlan, "/api/v1/production/plans", drogon::Post);
    METHOD_LIST_END

    void lines(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        drogon::app().getDbClient()->execSqlAsync(
            "SELECT id, line_code, line_name, workshop, location, capacity_per_hour, status "
            "FROM prod_production_lines WHERE deleted = FALSE ORDER BY id",
            [callback, traceId](const drogon::orm::Result& r) {
                auto arr = nlohmann::json::array();
                for (const auto& row : r)
                    arr.push_back({
                        {"id", row["id"].as<int64_t>()},
                        {"line_code", row["line_code"].as<std::string>()},
                        {"line_name", row["line_name"].as<std::string>()},
                        {"workshop",
                         row["workshop"].isNull() ? "" : row["workshop"].as<std::string>()},
                        {"capacity_per_hour", row["capacity_per_hour"].isNull()
                                                  ? 0
                                                  : row["capacity_per_hour"].as<int>()},
                        {"status", row["status"].as<int>()},
                    });
                callback(ApiResponse::success({{"list", arr}}, traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            });
    }

    void stations(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback, int64_t lineId) {
        auto traceId = traceIdOf(req);
        drogon::app().getDbClient()->execSqlAsync(
            "SELECT id, station_code, station_name, station_seq, std_cycle_time, status "
            "FROM prod_workstations WHERE line_id = $1 AND deleted = FALSE ORDER BY station_seq",
            [callback, traceId](const drogon::orm::Result& r) {
                auto arr = nlohmann::json::array();
                for (const auto& row : r)
                    arr.push_back({
                        {"id", row["id"].as<int64_t>()},
                        {"station_code", row["station_code"].as<std::string>()},
                        {"station_name", row["station_name"].as<std::string>()},
                        {"station_seq", row["station_seq"].as<int>()},
                        {"std_cycle_time",
                         row["std_cycle_time"].isNull() ? 0 : row["std_cycle_time"].as<int>()},
                        {"status", row["status"].as<int>()},
                    });
                callback(ApiResponse::success({{"list", arr}}, traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            },
            SqlArg(lineId));
    }

    void processes(const drogon::HttpRequestPtr& req,
                   std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        drogon::app().getDbClient()->execSqlAsync(
            "SELECT pr.id, pr.process_code, pr.process_name, pr.version, pr.total_steps, "
            "pr.status, p.product_name "
            "FROM prod_processes pr LEFT JOIN prod_products p ON p.id = pr.product_id "
            "WHERE pr.status = 1 ORDER BY pr.id",
            [callback, traceId](const drogon::orm::Result& r) {
                auto arr = nlohmann::json::array();
                for (const auto& row : r)
                    arr.push_back({
                        {"id", row["id"].as<int64_t>()},
                        {"process_code", row["process_code"].as<std::string>()},
                        {"process_name", row["process_name"].as<std::string>()},
                        {"version", row["version"].as<std::string>()},
                        {"total_steps", row["total_steps"].as<int>()},
                        {"product_name",
                         row["product_name"].isNull() ? "" : row["product_name"].as<std::string>()},
                    });
                callback(ApiResponse::success({{"list", arr}}, traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            });
    }

    void products(const drogon::HttpRequestPtr& req,
                  std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        drogon::app().getDbClient()->execSqlAsync(
            "SELECT id, product_code, product_name, specification, unit, category, status "
            "FROM prod_products WHERE deleted = FALSE ORDER BY id",
            [callback, traceId](const drogon::orm::Result& r) {
                auto arr = nlohmann::json::array();
                for (const auto& row : r)
                    arr.push_back({
                        {"id", row["id"].as<int64_t>()},
                        {"product_code", row["product_code"].as<std::string>()},
                        {"product_name", row["product_name"].as<std::string>()},
                        {"specification", row["specification"].isNull()
                                              ? ""
                                              : row["specification"].as<std::string>()},
                        {"unit", row["unit"].as<std::string>()},
                        {"category",
                         row["category"].isNull() ? "" : row["category"].as<std::string>()},
                        {"status", row["status"].as<int>()},
                    });
                callback(ApiResponse::success({{"list", arr}}, traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            });
    }

    void plans(const drogon::HttpRequestPtr& req,
               std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        drogon::app().getDbClient()->execSqlAsync(
            "SELECT pl.id, pl.plan_no, pl.plan_date, pl.line_id, l.line_name, pl.shift, "
            "pl.plan_qty, pl.status, pl.created_at "
            "FROM prod_production_plans pl "
            "LEFT JOIN prod_production_lines l ON l.id = pl.line_id "
            "ORDER BY pl.plan_date DESC, pl.id DESC LIMIT 100",
            [callback, traceId](const drogon::orm::Result& r) {
                auto arr = nlohmann::json::array();
                for (const auto& row : r)
                    arr.push_back({
                        {"id", row["id"].as<int64_t>()},
                        {"plan_no", row["plan_no"].as<std::string>()},
                        {"plan_date", row["plan_date"].as<std::string>()},
                        {"line_id", row["line_id"].as<int64_t>()},
                        {"line_name",
                         row["line_name"].isNull() ? "" : row["line_name"].as<std::string>()},
                        {"shift", row["shift"].as<int>()},
                        {"plan_qty", row["plan_qty"].as<int>()},
                        {"status", row["status"].as<int>()},
                        {"created_at", row["created_at"].as<std::string>()},
                    });
                callback(ApiResponse::success({{"list", arr}}, traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            });
    }

    // 创建产线 (任务 13 主数据 CRUD)
    void createLine(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("line_code") || !body.contains("line_name")) {
            callback(ApiResponse::error(400, "line_code/line_name 必填", traceId));
            return;
        }
        auto j = body;
        drogon::app().getDbClient()->execSqlAsync(
            "INSERT INTO prod_production_lines (line_code, line_name, workshop, location, "
            "capacity_per_hour) VALUES ($1,$2,NULLIF($3,''),NULLIF($4,''),NULLIF($5::int,0)) "
            "RETURNING id",
            [callback, traceId](const drogon::orm::Result& r) {
                callback(ApiResponse::success({{"id", r[0]["id"].as<int64_t>()}, {"created", true}},
                                              traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            },
            j["line_code"].get<std::string>(), j["line_name"].get<std::string>(),
            j.value("workshop", ""), j.value("location", ""),
            SqlArg(j.value("capacity_per_hour", 0)));
    }

    // 创建产品 (任务 13)
    void createProduct(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("product_code") || !body.contains("product_name")) {
            callback(ApiResponse::error(400, "product_code/product_name 必填", traceId));
            return;
        }
        auto j = body;
        drogon::app().getDbClient()->execSqlAsync(
            "INSERT INTO prod_products (product_code, product_name, specification, unit, "
            "category) VALUES ($1,$2,NULLIF($3,''),COALESCE(NULLIF($4,''),'PCS'),NULLIF($5,'')) "
            "RETURNING id",
            // 注: 数值绑定参数经 drogon 以二进制发送, 参与表达式 (NULLIF/COALESCE 等)
            // 时 PG 会按字面量推断参数类型导致字节宽度不匹配, 必须显式 cast
            [callback, traceId](const drogon::orm::Result& r) {
                callback(ApiResponse::success({{"id", r[0]["id"].as<int64_t>()}, {"created", true}},
                                              traceId));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            },
            j["product_code"].get<std::string>(), j["product_name"].get<std::string>(),
            j.value("specification", ""), j.value("unit", ""), j.value("category", ""));
    }

    // 创建工艺路线 + 步骤 (事务; E2E "建工艺" 入口)。步骤数组: [{step_seq,step_name,step_code,...}]
    void createProcess(const drogon::HttpRequestPtr& req,
                       std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("process_code") || !body.contains("process_name") ||
            !body.contains("steps") || !body["steps"].is_array() || body["steps"].empty()) {
            callback(ApiResponse::error(400, "process_code/process_name/steps[] 必填", traceId));
            return;
        }
        auto attrs = req->getAttributes();
        int64_t createdBy =
            attrs->find("current_user_id") ? attrs->get<int64_t>("current_user_id") : 0;
        auto j = body;
        auto steps = j["steps"];
        auto db = drogon::app().getDbClient();
        auto trans = db->newTransaction();
        trans->execSqlAsync(
            "INSERT INTO prod_processes (process_code, process_name, product_id, version, "
            "total_steps, status, published_at, created_by) "
            "VALUES ($1,$2,NULLIF($3::bigint,0),$4,$5::int,1,NOW(),$6::bigint) RETURNING id",
            [trans, steps, callback, traceId](const drogon::orm::Result& r) {
                auto processId = r[0]["id"].as<int64_t>();
                // 必须等全部步骤插入并 COMMIT 完成再响应: 否则调用方紧接着建单
                // 会查不到步骤。步骤回调捕获 trans 延长事务生存期,
                // 最后一个回调结束时引用归零 -> 析构排队 COMMIT -> commitCallback 响应
                auto responded = std::make_shared<bool>(false);
                auto stepCount = steps.size();
                trans->setCommitCallback(
                    [responded, processId, stepCount, callback, traceId](bool committed) {
                        if (*responded)
                            return;
                        *responded = true;
                        if (!committed) {
                            callback(ApiResponse::error(500, "工艺路线事务提交失败", traceId));
                            return;
                        }
                        callback(ApiResponse::success(
                            {{"id", processId}, {"steps", stepCount}, {"created", true}},
                            traceId));
                    });
                for (const auto& s : steps)
                    trans->execSqlAsync(
                        "INSERT INTO prod_process_steps (process_id, step_seq, step_name, "
                        "step_code, workstation_type, std_cycle_time, quality_check, is_key_step) "
                        "VALUES ($1,$2::int,$3,$4,NULLIF($5,''),NULLIF($6::int,0),$7::boolean,$8::boolean)",
                        [trans, responded](const drogon::orm::Result&) {},
                        [trans, responded, callback, traceId](const drogon::orm::DrogonDbException& e) {
                            if (*responded)
                                return;
                            *responded = true;
                            callback(ApiResponse::error(500, e.base().what(), traceId));
                        },
                        SqlArg(processId), SqlArg(s["step_seq"].get<int>()),
                        s["step_name"].get<std::string>(), s["step_code"].get<std::string>(),
                        s.value("workstation_type", ""), SqlArg(s.value("std_cycle_time", 0)),
                        SqlArg(s.value("quality_check", false)),
                        SqlArg(s.value("is_key_step", false)));
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            },
            j["process_code"].get<std::string>(), j["process_name"].get<std::string>(),
            SqlArg(j.value("product_id", (int64_t)0)), j.value("version", "1.0"),
            SqlArg((int)steps.size()), SqlArg(createdBy));
    }

    // 创建计划并维护 prod_plan_work_orders 关联 (计划任务 16)
    void createPlan(const drogon::HttpRequestPtr& req,
                    std::function<void(const drogon::HttpResponsePtr&)>&& callback) {
        auto traceId = traceIdOf(req);
        auto body = bodyJson(req);
        if (body.is_null() || !body.contains("plan_date") || !body.contains("line_id") ||
            !body.contains("shift") || !body.contains("plan_qty")) {
            callback(ApiResponse::error(400, "plan_date/line_id/shift/plan_qty 必填", traceId));
            return;
        }
        auto attrs = req->getAttributes();
        int64_t createdBy =
            attrs->find("current_user_id") ? attrs->get<int64_t>("current_user_id") : 0;

        char planNo[32];
        std::snprintf(planNo, sizeof(planNo), "PL%lld", (long long)std::time(nullptr));
        auto db = drogon::app().getDbClient();
        auto trans = db->newTransaction();
        auto j = body;
        trans->execSqlAsync(
            "INSERT INTO prod_production_plans (plan_no, plan_date, line_id, shift, plan_qty, "
            "status, created_by) VALUES ($1,$2,$3,$4,$5,0,$6) RETURNING id",
            [trans, j, callback, traceId](const drogon::orm::Result& r) mutable {
                auto planId = r[0]["id"].as<int64_t>();
                // 关联 INSERT 回调捕获 trans: 最后一个回调结束 -> 析构排队 COMMIT ->
                // commitCallback 响应 (无关联时外层回调末尾释放)
                auto responded = std::make_shared<bool>(false);
                trans->setCommitCallback([responded, planId, callback, traceId](bool committed) {
                    if (*responded)
                        return;
                    *responded = true;
                    if (!committed) {
                        callback(ApiResponse::error(500, "计划事务提交失败", traceId));
                        return;
                    }
                    callback(ApiResponse::success({{"id", planId}, {"created", true}}, traceId));
                });
                if (j.contains("work_order_ids") && j["work_order_ids"].is_array() &&
                    !j["work_order_ids"].empty()) {
                    for (const auto& wo : j["work_order_ids"])
                        trans->execSqlAsync(
                            "INSERT INTO prod_plan_work_orders (plan_id, work_order_id) "
                            "VALUES ($1,$2) ON CONFLICT DO NOTHING",
                            [trans, responded](const drogon::orm::Result&) {},
                            [trans, responded](const drogon::orm::DrogonDbException&) {},
                            SqlArg(planId), SqlArg(wo.get<int64_t>()));
                } else {
                    trans.reset(); // 无关联语句, 立即释放触发 COMMIT
                }
            },
            [callback, traceId](const drogon::orm::DrogonDbException& e) {
                callback(ApiResponse::error(500, e.base().what(), traceId));
            },
            std::string(planNo), j["plan_date"].get<std::string>(),
            SqlArg(j["line_id"].get<int64_t>()), SqlArg(j["shift"].get<int>()),
            SqlArg(j["plan_qty"].get<int>()), SqlArg(createdBy));
    }
};

} // namespace hms
