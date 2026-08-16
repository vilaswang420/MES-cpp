#include "services/ProductionService.hh"

#include <drogon/drogon.h>

#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

#include "common/ApiResponse.hh"
#include "common/SqlParam.hh"

namespace hms::ProductionService {

namespace orm = drogon::orm;

namespace {

// ---- 工具 ----

constexpr int kMaxPageSize = 200;

int clampPage(int page) {
    return page < 1 ? 1 : page;
}

int clampPageSize(int pageSize) {
    return (pageSize < 1 || pageSize > kMaxPageSize) ? 20 : pageSize;
}

std::string num(int64_t v) {
    return std::to_string(v);
}

// keyword 搜索条件 (NULL 哨兵参数化, $1 由调用方传 SqlArg(keyword) 或 SqlArgNull())
std::string kwWhere(const std::string& cols, const std::string& extra = "") {
    return "WHERE deleted = FALSE" + (extra.empty() ? "" : " AND " + extra) +
           " AND ($1::text IS NULL OR " + cols + " ILIKE '%' || $1 || '%')";
}

// 计划号 = PL + UTC日期 + 全局序列 (对齐工单号 WO 格式, 防 time(nullptr) 秒级碰撞)
std::string genPlanNo(int64_t seq) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "PL%04d%02d%02d%05lld", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, static_cast<long long>(seq));
    return buf;
}

nlohmann::json lineToJson(const orm::Row& row) {
    return {
        {"id", row["id"].as<int64_t>()},
        {"line_code", row["line_code"].as<std::string>()},
        {"line_name", row["line_name"].as<std::string>()},
        {"workshop", row["workshop"].isNull() ? "" : row["workshop"].as<std::string>()},
        {"location", row["location"].isNull() ? "" : row["location"].as<std::string>()},
        {"capacity_per_hour",
         row["capacity_per_hour"].isNull() ? 0 : row["capacity_per_hour"].as<int>()},
        {"status", row["status"].as<int>()},
    };
}

nlohmann::json productToJson(const orm::Row& row) {
    auto str = [&row](const char* f) {
        return row[f].isNull() ? std::string() : row[f].as<std::string>();
    };
    return {
        {"id", row["id"].as<int64_t>()},
        {"product_code", str("product_code")},
        {"product_name", str("product_name")},
        {"specification", str("specification")},
        {"unit", str("unit")},
        {"category", str("category")},
        {"erp_material_code", str("erp_material_code")},
        {"process_id", row["process_id"].isNull() ? 0 : row["process_id"].as<int64_t>()},
        {"status", row["status"].as<int>()},
    };
}

nlohmann::json processToJson(const orm::Row& row) {
    auto str = [&row](const char* f) {
        return row[f].isNull() ? std::string() : row[f].as<std::string>();
    };
    return {
        {"id", row["id"].as<int64_t>()},
        {"process_code", str("process_code")},
        {"process_name", str("process_name")},
        {"product_id", row["product_id"].isNull() ? 0 : row["product_id"].as<int64_t>()},
        {"product_name", str("product_name")},
        {"version", str("version")},
        {"total_steps", row["total_steps"].as<int>()},
        {"status", row["status"].as<int>()},
    };
}

nlohmann::json planToJson(const orm::Row& row) {
    return {
        {"id", row["id"].as<int64_t>()},
        {"plan_no", row["plan_no"].as<std::string>()},
        {"plan_date", row["plan_date"].as<std::string>()},
        {"line_id", row["line_id"].isNull() ? 0 : row["line_id"].as<int64_t>()},
        {"line_name", row["line_name"].isNull() ? "" : row["line_name"].as<std::string>()},
        {"shift", row["shift"].as<int>()},
        {"plan_qty", row["plan_qty"].as<int>()},
        {"status", row["status"].as<int>()},
        {"created_at", row["created_at"].as<std::string>()},
    };
}

} // namespace

// ============ 产线 ============

drogon::Task<nlohmann::json> listLines(int page, int pageSize, const std::string& keyword) {
    page = clampPage(page);
    pageSize = clampPageSize(pageSize);
    bool hasKw = !keyword.empty();
    auto kwArg = hasKw ? SqlArg(keyword) : SqlArgNull();
    auto where = kwWhere("line_code || ' ' || line_name");

    auto db = drogon::app().getDbClient();
    auto countRes =
        co_await db->execSqlCoro("SELECT COUNT(*) AS cnt FROM prod_production_lines " + where,
                                 kwArg);
    auto rows = co_await db->execSqlCoro(
        "SELECT id, line_code, line_name, workshop, location, capacity_per_hour, status "
        "FROM prod_production_lines " +
            where + " ORDER BY id LIMIT " + num(pageSize) + " OFFSET " +
            num((page - 1) * pageSize),
        kwArg);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& row : rows)
        arr.push_back(lineToJson(row));
    co_return {{"list", arr},
               {"total", countRes.empty() ? 0 : countRes[0]["cnt"].as<int64_t>()},
               {"page", page},
               {"page_size", pageSize}};
}

drogon::Task<nlohmann::json> createLine(const nlohmann::json& body) {
    if (!body.contains("line_code") || !body.contains("line_name"))
        throw BadRequest("line_code/line_name 必填");
    auto db = drogon::app().getDbClient();
    auto r = co_await db->execSqlCoro(
        "INSERT INTO prod_production_lines (line_code, line_name, workshop, location, "
        "capacity_per_hour) VALUES ($1,$2,NULLIF($3,''),NULLIF($4,''),NULLIF($5::int,0)) "
        "RETURNING id",
        body["line_code"].get<std::string>(), body["line_name"].get<std::string>(),
        body.value("workshop", ""), body.value("location", ""),
        SqlArg(body.value("capacity_per_hour", 0)));
    co_return {{"id", r[0]["id"].as<int64_t>()}, {"created", true}};
}

drogon::Task<nlohmann::json> updateLine(int64_t id, const nlohmann::json& body) {
    auto db = drogon::app().getDbClient();
    // line_code 为唯一编码不可改 (改编码会破坏工单/工位引用语义)
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_production_lines SET "
        "line_name = COALESCE(NULLIF($2,''), line_name), "
        "workshop = COALESCE(NULLIF($3,''), workshop), "
        "location = COALESCE(NULLIF($4,''), location), "
        "capacity_per_hour = CASE WHEN $5 > 0 THEN $5::int ELSE capacity_per_hour END, "
        "status = CASE WHEN $6 >= 0 THEN $6::smallint ELSE status END, "
        "updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id), body.value("line_name", ""), body.value("workshop", ""),
        body.value("location", ""), SqlArg(body.value("capacity_per_hour", 0)),
        SqlArg(body.value("status", -1)));
    if (r.empty())
        throw NotFound("产线不存在");
    co_return {{"id", id}, {"updated", true}};
}

drogon::Task<nlohmann::json> deleteLine(int64_t id) {
    auto db = drogon::app().getDbClient();
    auto ref = co_await db->execSqlCoro(
        "SELECT EXISTS(SELECT 1 FROM prod_workstations WHERE line_id = $1 AND deleted = FALSE) "
        "    OR EXISTS(SELECT 1 FROM prod_work_orders WHERE line_id = $1) "
        "    OR EXISTS(SELECT 1 FROM prod_production_plans WHERE line_id = $1) AS has_ref",
        SqlArg(id));
    if (!ref.empty() && ref[0]["has_ref"].as<bool>())
        throw Conflict("产线已被工位/工单/计划引用, 禁止删除");
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_production_lines SET deleted = TRUE, updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id));
    if (r.empty())
        throw NotFound("产线不存在");
    co_return {{"id", id}, {"deleted", true}};
}

drogon::Task<nlohmann::json> listStations(int64_t lineId) {
    auto db = drogon::app().getDbClient();
    auto rows = co_await db->execSqlCoro(
        "SELECT id, station_code, station_name, station_seq, std_cycle_time, status "
        "FROM prod_workstations WHERE line_id = $1 AND deleted = FALSE ORDER BY station_seq",
        SqlArg(lineId));
    nlohmann::json arr = nlohmann::json::array();
    for (const auto& row : rows)
        arr.push_back({
            {"id", row["id"].as<int64_t>()},
            {"station_code", row["station_code"].as<std::string>()},
            {"station_name", row["station_name"].as<std::string>()},
            {"station_seq", row["station_seq"].as<int>()},
            {"std_cycle_time",
             row["std_cycle_time"].isNull() ? 0 : row["std_cycle_time"].as<int>()},
            {"status", row["status"].as<int>()},
        });
    co_return {{"list", arr}};
}

// ============ 产品 ============

drogon::Task<nlohmann::json> listProducts(int page, int pageSize, const std::string& keyword) {
    page = clampPage(page);
    pageSize = clampPageSize(pageSize);
    bool hasKw = !keyword.empty();
    auto kwArg = hasKw ? SqlArg(keyword) : SqlArgNull();
    auto where = kwWhere("product_code || ' ' || product_name");

    auto db = drogon::app().getDbClient();
    auto countRes =
        co_await db->execSqlCoro("SELECT COUNT(*) AS cnt FROM prod_products " + where, kwArg);
    auto rows = co_await db->execSqlCoro(
        "SELECT id, product_code, product_name, specification, unit, category, "
        "erp_material_code, process_id, status FROM prod_products " +
            where + " ORDER BY id LIMIT " + num(pageSize) + " OFFSET " +
            num((page - 1) * pageSize),
        kwArg);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& row : rows)
        arr.push_back(productToJson(row));
    co_return {{"list", arr},
               {"total", countRes.empty() ? 0 : countRes[0]["cnt"].as<int64_t>()},
               {"page", page},
               {"page_size", pageSize}};
}

drogon::Task<nlohmann::json> createProduct(const nlohmann::json& body) {
    if (!body.contains("product_code") || !body.contains("product_name"))
        throw BadRequest("product_code/product_name 必填");
    auto db = drogon::app().getDbClient();
    // 注: 数值绑定参数经 drogon 以文本格式绑定 (SqlArg), 参与表达式时显式 cast 保安全
    auto r = co_await db->execSqlCoro(
        "INSERT INTO prod_products (product_code, product_name, specification, unit, "
        "category, erp_material_code, process_id) "
        "VALUES ($1,$2,NULLIF($3,''),COALESCE(NULLIF($4,''),'PCS'),NULLIF($5,''),NULLIF($6,''),"
        "NULLIF($7::bigint,0)) RETURNING id",
        body["product_code"].get<std::string>(), body["product_name"].get<std::string>(),
        body.value("specification", ""), body.value("unit", ""), body.value("category", ""),
        body.value("erp_material_code", ""), SqlArg(body.value("process_id", (int64_t)0)));
    co_return {{"id", r[0]["id"].as<int64_t>()}, {"created", true}};
}

drogon::Task<nlohmann::json> updateProduct(int64_t id, const nlohmann::json& body) {
    auto db = drogon::app().getDbClient();
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_products SET "
        "product_name = COALESCE(NULLIF($2,''), product_name), "
        "specification = COALESCE(NULLIF($3,''), specification), "
        "unit = COALESCE(NULLIF($4,''), unit), "
        "category = COALESCE(NULLIF($5,''), category), "
        "erp_material_code = COALESCE(NULLIF($6,''), erp_material_code), "
        "process_id = CASE WHEN $7::bigint > 0 THEN $7::bigint ELSE process_id END, "
        "status = CASE WHEN $8 >= 0 THEN $8::smallint ELSE status END, "
        "updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id), body.value("product_name", ""), body.value("specification", ""),
        body.value("unit", ""), body.value("category", ""), body.value("erp_material_code", ""),
        SqlArg(body.value("process_id", (int64_t)0)), SqlArg(body.value("status", -1)));
    if (r.empty())
        throw NotFound("产品不存在");
    co_return {{"id", id}, {"updated", true}};
}

drogon::Task<nlohmann::json> deleteProduct(int64_t id) {
    auto db = drogon::app().getDbClient();
    auto ref = co_await db->execSqlCoro(
        "SELECT EXISTS(SELECT 1 FROM prod_work_orders WHERE product_id = $1) "
        "    OR EXISTS(SELECT 1 FROM prod_processes WHERE product_id = $1 AND deleted = FALSE) "
        "    AS has_ref",
        SqlArg(id));
    if (!ref.empty() && ref[0]["has_ref"].as<bool>())
        throw Conflict("产品已被工单/工艺路线引用, 禁止删除");
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_products SET deleted = TRUE, updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id));
    if (r.empty())
        throw NotFound("产品不存在");
    co_return {{"id", id}, {"deleted", true}};
}

// ============ 工艺路线 ============

drogon::Task<nlohmann::json> listProcesses(int page, int pageSize, const std::string& keyword) {
    page = clampPage(page);
    pageSize = clampPageSize(pageSize);
    bool hasKw = !keyword.empty();
    auto kwArg = hasKw ? SqlArg(keyword) : SqlArgNull();
    // 只列已发布 (status=1) 且未软删; 保留 product_name 左连接
    std::string where =
        "WHERE pr.deleted = FALSE AND pr.status = 1 "
        "AND ($1::text IS NULL OR pr.process_code ILIKE '%' || $1 || '%' "
        "     OR pr.process_name ILIKE '%' || $1 || '%')";

    auto db = drogon::app().getDbClient();
    auto countRes = co_await db->execSqlCoro(
        "SELECT COUNT(*) AS cnt FROM prod_processes pr " + where, kwArg);
    auto rows = co_await db->execSqlCoro(
        "SELECT pr.id, pr.process_code, pr.process_name, pr.product_id, pr.version, "
        "pr.total_steps, pr.status, p.product_name "
        "FROM prod_processes pr LEFT JOIN prod_products p ON p.id = pr.product_id " +
            where + " ORDER BY pr.id LIMIT " + num(pageSize) + " OFFSET " +
            num((page - 1) * pageSize),
        kwArg);

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& row : rows)
        arr.push_back(processToJson(row));
    co_return {{"list", arr},
               {"total", countRes.empty() ? 0 : countRes[0]["cnt"].as<int64_t>()},
               {"page", page},
               {"page_size", pageSize}};
}

drogon::Task<nlohmann::json> createProcess(const nlohmann::json& body, int64_t createdBy) {
    if (!body.contains("process_code") || !body.contains("process_name") ||
        !body.contains("steps") || !body["steps"].is_array() || body["steps"].empty())
        throw BadRequest("process_code/process_name/steps[] 必填");
    for (const auto& s : body["steps"]) {
        if (!s.contains("step_seq") || !s.contains("step_name") || !s.contains("step_code"))
            throw BadRequest("steps[i] 需含 step_seq/step_name/step_code");
    }

    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();
    auto ins = co_await trans->execSqlCoro(
        "INSERT INTO prod_processes (process_code, process_name, product_id, version, "
        "total_steps, status, published_at, created_by) "
        "VALUES ($1,$2,NULLIF($3::bigint,0),$4,$5::int,1,NOW(),$6::bigint) RETURNING id",
        body["process_code"].get<std::string>(), body["process_name"].get<std::string>(),
        SqlArg(body.value("product_id", (int64_t)0)), body.value("version", "1.0"),
        SqlArg((int)body["steps"].size()), SqlArg(createdBy));
    auto processId = ins[0]["id"].as<int64_t>();

    // 任一步骤失败即抛异常 -> 事务析构自动 ROLLBACK (旧回调式实现存在半提交风险)
    for (const auto& s : body["steps"])
        co_await trans->execSqlCoro(
            "INSERT INTO prod_process_steps (process_id, step_seq, step_name, step_code, "
            "workstation_type, std_cycle_time, quality_check, is_key_step) "
            "VALUES ($1,$2::int,$3,$4,NULLIF($5,''),NULLIF($6::int,0),$7::boolean,$8::boolean)",
            SqlArg(processId), SqlArg(s["step_seq"].get<int>()),
            s["step_name"].get<std::string>(), s["step_code"].get<std::string>(),
            s.value("workstation_type", ""), SqlArg(s.value("std_cycle_time", 0)),
            SqlArg(s.value("quality_check", false)), SqlArg(s.value("is_key_step", false)));

    if (!co_await commitAwait(std::move(trans)))
        throw Internal("工艺路线事务提交失败");
    co_return {{"id", processId}, {"steps", (int)body["steps"].size()}, {"created", true}};
}

drogon::Task<nlohmann::json> updateProcess(int64_t id, const nlohmann::json& body) {
    auto db = drogon::app().getDbClient();
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_processes SET "
        "process_name = COALESCE(NULLIF($2,''), process_name), "
        "product_id = CASE WHEN $3::bigint > 0 THEN $3::bigint ELSE product_id END, "
        "version = COALESCE(NULLIF($4,''), version), "
        "status = CASE WHEN $5 >= 0 THEN $5::smallint ELSE status END, "
        "updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id), body.value("process_name", ""), SqlArg(body.value("product_id", (int64_t)0)),
        body.value("version", ""), SqlArg(body.value("status", -1)));
    if (r.empty())
        throw NotFound("工艺路线不存在");
    co_return {{"id", id}, {"updated", true}};
}

drogon::Task<nlohmann::json> deleteProcess(int64_t id) {
    auto db = drogon::app().getDbClient();
    auto ref = co_await db->execSqlCoro(
        "SELECT EXISTS(SELECT 1 FROM prod_work_orders WHERE process_id = $1) "
        "    OR EXISTS(SELECT 1 FROM prod_products WHERE process_id = $1 AND deleted = FALSE) "
        "    AS has_ref",
        SqlArg(id));
    if (!ref.empty() && ref[0]["has_ref"].as<bool>())
        throw Conflict("工艺路线已被工单/产品引用, 禁止删除");
    auto r = co_await db->execSqlCoro(
        "UPDATE prod_processes SET deleted = TRUE, status = 2, updated_at = NOW() "
        "WHERE id = $1 AND deleted = FALSE RETURNING id",
        SqlArg(id));
    if (r.empty())
        throw NotFound("工艺路线不存在");
    co_return {{"id", id}, {"deleted", true}};
}

// ============ 生产计划 ============

drogon::Task<nlohmann::json> listPlans(int page, int pageSize) {
    page = clampPage(page);
    pageSize = clampPageSize(pageSize);
    auto db = drogon::app().getDbClient();
    auto countRes = co_await db->execSqlCoro("SELECT COUNT(*) AS cnt FROM prod_production_plans");
    auto rows = co_await db->execSqlCoro(
        "SELECT pl.id, pl.plan_no, pl.plan_date, pl.line_id, l.line_name, pl.shift, "
        "pl.plan_qty, pl.status, pl.created_at "
        "FROM prod_production_plans pl "
        "LEFT JOIN prod_production_lines l ON l.id = pl.line_id "
        "ORDER BY pl.plan_date DESC, pl.id DESC LIMIT " +
            num(pageSize) + " OFFSET " + num((page - 1) * pageSize));

    nlohmann::json arr = nlohmann::json::array();
    for (const auto& row : rows)
        arr.push_back(planToJson(row));
    co_return {{"list", arr},
               {"total", countRes.empty() ? 0 : countRes[0]["cnt"].as<int64_t>()},
               {"page", page},
               {"page_size", pageSize}};
}

drogon::Task<nlohmann::json> createPlan(const nlohmann::json& body, int64_t createdBy) {
    if (!body.contains("plan_date") || !body.contains("line_id") || !body.contains("shift") ||
        !body.contains("plan_qty"))
        throw BadRequest("plan_date/line_id/shift/plan_qty 必填");

    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();
    // 单号 = PL + UTC日期 + 全局序列 (迁移 012 建 prod_plan_no_seq; 防秒级时间戳碰撞)
    auto seqRow = co_await trans->execSqlCoro("SELECT nextval('prod_plan_no_seq')");
    auto planNo = genPlanNo(seqRow[0]["nextval"].as<int64_t>());

    auto ins = co_await trans->execSqlCoro(
        "INSERT INTO prod_production_plans (plan_no, plan_date, line_id, shift, plan_qty, "
        "status, created_by) VALUES ($1,$2,$3,$4,$5,0,$6) RETURNING id",
        planNo, body["plan_date"].get<std::string>(), SqlArg(body["line_id"].get<int64_t>()),
        SqlArg(body["shift"].get<int>()), SqlArg(body["plan_qty"].get<int>()),
        SqlArg(createdBy));
    auto planId = ins[0]["id"].as<int64_t>();

    if (body.contains("work_order_ids") && body["work_order_ids"].is_array())
        for (const auto& wo : body["work_order_ids"])
            co_await trans->execSqlCoro(
                "INSERT INTO prod_plan_work_orders (plan_id, work_order_id) "
                "VALUES ($1,$2) ON CONFLICT DO NOTHING",
                SqlArg(planId), SqlArg(wo.get<int64_t>()));

    if (!co_await commitAwait(std::move(trans)))
        throw Internal("计划事务提交失败");
    co_return {{"id", planId}, {"created", true}};
}

} // namespace hms::ProductionService
