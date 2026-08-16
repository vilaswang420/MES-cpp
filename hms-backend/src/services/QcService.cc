#include "services/QcService.hh"

#include <drogon/drogon.h>

#include <chrono>
#include <cstdio>
#include <ctime>
#include <random>

#include "common/SqlParam.hh"
#include "utils/QcRules.hh"

namespace hms::QcService {

namespace {

std::string optStr(const drogon::orm::Row& row, const char* field) {
    return row[field].isNull() ? std::string() : row[field].as<std::string>();
}

nlohmann::json parseJsonField(const std::string& s) {
    try {
        return nlohmann::json::parse(s);
    } catch (...) {
        return nlohmann::json::array();
    }
}

void clampPage(int& page, int& pageSize) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;
}

// 检验单号: QC-YYYYMMDD-HHMMSS-XXX (与工单号同风格, 随机尾缀防碰撞)
std::string genInspectionNo() {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    static thread_local std::mt19937 rng{std::random_device{}()};
    char buf[40];
    std::snprintf(buf, sizeof(buf), "QC-%04d%02d%02d-%02d%02d%02d-%03d", tm.tm_year + 1900,
                  tm.tm_mon + 1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec,
                  static_cast<int>(rng() % 1000));
    return buf;
}

nlohmann::json defectRow(const drogon::orm::Row& row) {
    return {
        {"id", row["id"].as<int64_t>()},
        {"inspection_id", row["inspection_id"].as<int64_t>()},
        {"work_order_id", row["work_order_id"].isNull() ? 0 : row["work_order_id"].as<int64_t>()},
        {"defect_code", row["defect_code"].as<std::string>()},
        {"defect_name", row["defect_name"].as<std::string>()},
        {"defect_category", optStr(row, "defect_category")},
        {"quantity", row["quantity"].as<int>()},
        {"severity", row["severity"].as<int>()},
        {"disposition", row["disposition"].as<int>()},
        {"root_cause", optStr(row, "root_cause")},
        {"corrective_action", optStr(row, "corrective_action")},
        {"created_at", optStr(row, "created_at")}};
}

} // namespace

// ============ 检验标准 ============

void listStandards(int page, int pageSize, const std::string& keyword, int64_t productId,
                   JsonCb onOk, ErrCb onErr) {
    clampPage(page, pageSize);

    std::string where = "WHERE s.status = 1";
    bool hasKw = !keyword.empty();
    if (hasKw)
        where += " AND (s.standard_code ILIKE $1 OR s.standard_name ILIKE $1)";
    if (productId > 0)
        where += " AND s.product_id = " + std::to_string(productId);

    std::string listSql =
        std::string("SELECT s.id, s.standard_code, s.standard_name, s.product_id, "
                    "COALESCE(p.product_name,'') AS product_name, s.inspection_type, "
                    "s.sample_size, s.aql_level, "
                    "(SELECT COALESCE(json_agg(jsonb_build_object("
                    "'id', i.id, 'item_code', i.item_code, 'item_name', i.item_name, "
                    "'data_type', i.data_type, 'upper_limit', i.upper_limit, "
                    "'lower_limit', i.lower_limit, 'nominal_value', i.nominal_value, "
                    "'unit', i.unit, 'is_key_item', i.is_key_item) "
                    "ORDER BY i.sort_order), '[]'::json) "
                    "FROM qc_inspection_items i WHERE i.standard_id = s.id) AS items "
                    "FROM qc_inspection_standards s "
                    "LEFT JOIN prod_products p ON p.id = s.product_id ") +
        where + " ORDER BY s.id LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM qc_inspection_standards s " + where;

    auto rowHandler = [page, pageSize, countSql, hasKw, keyword, onOk,
                       onErr](const drogon::orm::Result& r) {
        nlohmann::json listArr = nlohmann::json::array();
        for (const auto& row : r) {
            listArr.push_back(
                {{"id", row["id"].as<int64_t>()},
                 {"standard_code", row["standard_code"].as<std::string>()},
                 {"standard_name", row["standard_name"].as<std::string>()},
                 {"product_id", row["product_id"].isNull() ? 0 : row["product_id"].as<int64_t>()},
                 {"product_name", optStr(row, "product_name")},
                 {"inspection_type", row["inspection_type"].as<int>()},
                 {"sample_size", row["sample_size"].as<int>()},
                 {"aql_level", optStr(row, "aql_level")},
                 {"items", parseJsonField(optStr(row, "items"))}});
        }
        auto db2 = drogon::app().getDbClient();
        auto countOk = [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
            onOk({{"list", listArr},
                  {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        auto countErr = [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(500, e.base().what());
        };
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

// ============ 检验记录 ============

// 单 SQL 原子落库: CTE 插入检验主记录 + jsonb_array_elements 展开缺陷明细
void createInspection(const nlohmann::json& body, int64_t inspectorId, JsonCb onOk, ErrCb onErr) {
    int inspectionType = body.value("inspection_type", 0);
    if (inspectionType < 1 || inspectionType > 4)
        return onErr(400, "inspection_type 必填 (1首件/2过程/3完工/4抽样)");

    int sampleQty = body.value("sample_qty", 1);
    int passQty = body.value("pass_qty", 0);
    int defectQty = body.value("defect_qty", 0);
    if (sampleQty < 0 || passQty < 0 || defectQty < 0)
        return onErr(400, "数量不能为负");
    // result: 显式传入优先, 否则按数量推断 (0待检/1合格/2不合格/3让步)
    int result = body.value("result", -1);
    if (result == -1)
        result = defectQty > 0 ? 2 : (sampleQty > 0 ? 1 : 0);
    if (result < 0 || result > 3)
        return onErr(400, "result 取值 0-3");

    std::string defectsJson = "[]";
    if (body.contains("defects") && body["defects"].is_array()) {
        for (const auto& d : body["defects"]) {
            if (d.value("defect_code", "").empty() || d.value("defect_name", "").empty())
                return onErr(400, "缺陷 defect_code/defect_name 必填");
        }
        defectsJson = body["defects"].dump();
    }

    auto inspectionNo = genInspectionNo();
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "WITH insp AS ("
        "  INSERT INTO qc_inspections (inspection_no, standard_id, work_order_id, operation_id, "
        "    product_id, inspector_id, inspection_type, sample_qty, pass_qty, defect_qty, "
        "    result, remark) "
        "  VALUES ($1, NULLIF($2::bigint,0), NULLIF($3::bigint,0), NULLIF($4::bigint,0), "
        "    NULLIF($5::bigint,0), $6, $7::smallint, $8, $9, $10, $11::smallint, $12) "
        "  RETURNING id"
        "), def AS ("
        "  INSERT INTO qc_defects (inspection_id, work_order_id, defect_code, defect_name, "
        "    defect_category, quantity, severity, root_cause, station_id, operator_id) "
        "  SELECT (SELECT id FROM insp), NULLIF($3::bigint,0), "
        "    d->>'defect_code', d->>'defect_name', d->>'defect_category', "
        "    COALESCE((d->>'quantity')::int, 1), COALESCE((d->>'severity')::smallint, 2), "
        "    d->>'root_cause', NULLIF((d->>'station_id')::bigint, 0), "
        "    NULLIF((d->>'operator_id')::bigint, 0) "
        "  FROM jsonb_array_elements($13::jsonb) AS d"
        ") "
        "SELECT id FROM insp",
        [inspectionNo, onOk](const drogon::orm::Result& r) {
            onOk({{"id", r[0]["id"].as<int64_t>()}, {"inspection_no", inspectionNo}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        inspectionNo, SqlArg(body.value("standard_id", (int64_t)0)),
        SqlArg(body.value("work_order_id", (int64_t)0)),
        SqlArg(body.value("operation_id", (int64_t)0)),
        SqlArg(body.value("product_id", (int64_t)0)), SqlArg(inspectorId), SqlArg(inspectionType),
        SqlArg(sampleQty), SqlArg(passQty), SqlArg(defectQty), SqlArg(result),
        body.value("remark", ""), defectsJson);
}

void listInspections(int page, int pageSize, int64_t workOrderId, int result, JsonCb onOk,
                     ErrCb onErr) {
    clampPage(page, pageSize);

    std::string where = "WHERE TRUE";
    if (workOrderId > 0)
        where += " AND qi.work_order_id = " + std::to_string(workOrderId);
    if (result >= 0)
        where += " AND qi.result = " + std::to_string(result);

    std::string listSql =
        std::string("SELECT qi.id, qi.inspection_no, qi.standard_id, qi.work_order_id, "
                    "COALESCE(wo.work_order_no,'') AS work_order_no, qi.product_id, "
                    "qi.inspector_id, COALESCE(u.real_name,'') AS inspector_name, "
                    "qi.inspection_type, qi.sample_qty, qi.pass_qty, qi.defect_qty, qi.result, "
                    "qi.remark, qi.inspected_at "
                    "FROM qc_inspections qi "
                    "LEFT JOIN prod_work_orders wo ON wo.id = qi.work_order_id "
                    "LEFT JOIN sys_users u ON u.id = qi.inspector_id ") +
        where + " ORDER BY qi.id DESC LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM qc_inspections qi " + where;

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        listSql,
        [page, pageSize, countSql, onOk, onErr](const drogon::orm::Result& r) {
            nlohmann::json listArr = nlohmann::json::array();
            for (const auto& row : r) {
                listArr.push_back(
                    {{"id", row["id"].as<int64_t>()},
                     {"inspection_no", row["inspection_no"].as<std::string>()},
                     {"standard_id",
                      row["standard_id"].isNull() ? 0 : row["standard_id"].as<int64_t>()},
                     {"work_order_id",
                      row["work_order_id"].isNull() ? 0 : row["work_order_id"].as<int64_t>()},
                     {"work_order_no", optStr(row, "work_order_no")},
                     {"inspector_name", optStr(row, "inspector_name")},
                     {"inspection_type", row["inspection_type"].as<int>()},
                     {"sample_qty", row["sample_qty"].as<int>()},
                     {"pass_qty", row["pass_qty"].as<int>()},
                     {"defect_qty", row["defect_qty"].as<int>()},
                     {"result", row["result"].as<int>()},
                     {"remark", optStr(row, "remark")},
                     {"inspected_at", optStr(row, "inspected_at")}});
            }
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                countSql,
                [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
                    onOk({{"list", listArr},
                          {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                          {"page", page},
                          {"page_size", pageSize}});
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); });
}

// 详情含缺陷明细 (json_agg)
void getInspection(int64_t id, JsonCb onOk, ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "SELECT qi.id, qi.inspection_no, qi.standard_id, qi.work_order_id, qi.product_id, "
        "qi.inspector_id, qi.inspection_type, qi.sample_qty, qi.pass_qty, qi.defect_qty, "
        "qi.result, qi.remark, qi.inspected_at, "
        "(SELECT COALESCE(json_agg(jsonb_build_object("
        "'id', d.id, 'defect_code', d.defect_code, 'defect_name', d.defect_name, "
        "'defect_category', d.defect_category, 'quantity', d.quantity, "
        "'severity', d.severity, 'disposition', d.disposition, "
        "'root_cause', d.root_cause, 'corrective_action', d.corrective_action)), '[]'::json) "
        "FROM qc_defects d WHERE d.inspection_id = qi.id) AS defects "
        "FROM qc_inspections qi WHERE qi.id = $1",
        [onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // -> 404
            const auto& row = r[0];
            onOk({{"id", row["id"].as<int64_t>()},
                  {"inspection_no", row["inspection_no"].as<std::string>()},
                  {"standard_id",
                   row["standard_id"].isNull() ? 0 : row["standard_id"].as<int64_t>()},
                  {"work_order_id",
                   row["work_order_id"].isNull() ? 0 : row["work_order_id"].as<int64_t>()},
                  {"product_id", row["product_id"].isNull() ? 0 : row["product_id"].as<int64_t>()},
                  {"inspector_id", row["inspector_id"].as<int64_t>()},
                  {"inspection_type", row["inspection_type"].as<int>()},
                  {"sample_qty", row["sample_qty"].as<int>()},
                  {"pass_qty", row["pass_qty"].as<int>()},
                  {"defect_qty", row["defect_qty"].as<int>()},
                  {"result", row["result"].as<int>()},
                  {"remark", optStr(row, "remark")},
                  {"inspected_at", optStr(row, "inspected_at")},
                  {"defects", parseJsonField(optStr(row, "defects"))}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id));
}

// ============ 缺陷 ============

void listDefects(int page, int pageSize, int64_t workOrderId, int disposition,
                 const std::string& category, JsonCb onOk, ErrCb onErr) {
    clampPage(page, pageSize);

    std::string where = "WHERE TRUE";
    if (workOrderId > 0)
        where += " AND qd.work_order_id = " + std::to_string(workOrderId);
    if (disposition >= 0)
        where += " AND qd.disposition = " + std::to_string(disposition);
    bool hasCat = !category.empty();
    if (hasCat)
        where += " AND qd.defect_category = $1";

    std::string listSql =
        std::string("SELECT qd.id, qd.inspection_id, qd.work_order_id, qd.defect_code, "
                    "qd.defect_name, qd.defect_category, qd.quantity, qd.severity, "
                    "qd.disposition, qd.root_cause, qd.corrective_action, qd.created_at "
                    "FROM qc_defects qd ") +
        where + " ORDER BY qd.id DESC LIMIT " + std::to_string(pageSize) + " OFFSET " +
        std::to_string((page - 1) * pageSize);
    std::string countSql = "SELECT COUNT(*) AS cnt FROM qc_defects qd " + where;

    auto rowHandler = [page, pageSize, countSql, hasCat, category, onOk,
                       onErr](const drogon::orm::Result& r) {
        nlohmann::json listArr = nlohmann::json::array();
        for (const auto& row : r)
            listArr.push_back(defectRow(row));
        auto db2 = drogon::app().getDbClient();
        auto countOk = [listArr, page, pageSize, onOk](const drogon::orm::Result& cr) {
            onOk({{"list", listArr},
                  {"total", cr.empty() ? 0 : cr[0]["cnt"].as<int64_t>()},
                  {"page", page},
                  {"page_size", pageSize}});
        };
        auto countErr = [onErr](const drogon::orm::DrogonDbException& e) {
            onErr(500, e.base().what());
        };
        if (hasCat)
            db2->execSqlAsync(countSql, countOk, countErr, category);
        else
            db2->execSqlAsync(countSql, countOk, countErr);
    };
    auto rowErr = [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); };

    auto db = drogon::app().getDbClient();
    if (hasCat)
        db->execSqlAsync(listSql, rowHandler, rowErr, category);
    else
        db->execSqlAsync(listSql, rowHandler, rowErr);
}

// 缺陷处置 (1返工/2返修/3报废/4让步); 仅待处理状态可处置
void handleDefect(int64_t id, const nlohmann::json& body, int64_t userId, JsonCb onOk,
                  ErrCb onErr) {
    int disposition = body.value("disposition", 0);
    if (!QcRules::validDisposition(disposition))
        return onErr(400, "disposition 取值 1-4 (返工/返修/报废/让步)");

    auto db = drogon::app().getDbClient();
    db->execSqlAsync(
        "UPDATE qc_defects SET disposition = $2, disposition_by = $5, disposition_at = NOW(), "
        "root_cause = COALESCE(NULLIF($3, ''), root_cause), "
        "corrective_action = COALESCE(NULLIF($4, ''), corrective_action) "
        "WHERE id = $1 AND disposition = 0 RETURNING id",
        [id, disposition, userId, onOk](const drogon::orm::Result& r) {
            if (r.empty())
                return onOk(nullptr); // 不存在或已处置 -> 404
            onOk({{"id", id}, {"disposition", disposition}, {"disposition_by", userId}});
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        SqlArg(id), SqlArg(disposition), body.value("root_cause", ""),
        body.value("corrective_action", ""), SqlArg(userId));
}

// ============ 统计 ============

// 质量统计: 汇总 + 按缺陷类别分布 + 按日趋势; 时间窗参数缺省近 30 天
void statistics(const std::string& startDate, const std::string& endDate, JsonCb onOk,
                ErrCb onErr) {
    auto db = drogon::app().getDbClient();
    // 第 1 步: 检验汇总
    db->execSqlAsync(
        "SELECT COUNT(*) AS total, "
        "COUNT(*) FILTER (WHERE result = 1) AS pass_cnt, "
        "COUNT(*) FILTER (WHERE result = 2) AS fail_cnt, "
        "COUNT(*) FILTER (WHERE result = 3) AS concession_cnt, "
        "COALESCE(SUM(defect_qty), 0) AS defect_total "
        "FROM qc_inspections "
        "WHERE inspected_at >= COALESCE(NULLIF($1, '')::timestamptz, NOW() - INTERVAL '30 days') "
        "AND inspected_at < COALESCE(NULLIF($2, '')::timestamptz, NOW()) + INTERVAL '1 day'",
        [startDate, endDate, onOk, onErr](const drogon::orm::Result& r) {
            auto total = r[0]["total"].as<int64_t>();
            auto passCnt = r[0]["pass_cnt"].as<int64_t>();
            nlohmann::json summary = {{"total", total},
                                      {"pass_cnt", passCnt},
                                      {"fail_cnt", r[0]["fail_cnt"].as<int64_t>()},
                                      {"concession_cnt", r[0]["concession_cnt"].as<int64_t>()},
                                      {"defect_total", r[0]["defect_total"].as<int64_t>()},
                                      {"first_pass_rate", QcRules::firstPassRate(total, passCnt)}};
            // 第 2 步: 缺陷类别分布
            auto db2 = drogon::app().getDbClient();
            db2->execSqlAsync(
                "SELECT COALESCE(defect_category, '未分类') AS category, COUNT(*) AS cnt, "
                "COALESCE(SUM(quantity), 0) AS qty "
                "FROM qc_defects "
                "WHERE created_at >= COALESCE(NULLIF($1, '')::timestamptz, "
                "NOW() - INTERVAL '30 days') "
                "AND created_at < COALESCE(NULLIF($2, '')::timestamptz, NOW()) + INTERVAL '1 day' "
                "GROUP BY 1 ORDER BY cnt DESC",
                [summary, startDate, endDate, onOk, onErr](const drogon::orm::Result& r2) {
                    nlohmann::json cats = nlohmann::json::array();
                    for (const auto& row : r2)
                        cats.push_back({{"category", row["category"].as<std::string>()},
                                        {"count", row["cnt"].as<int64_t>()},
                                        {"quantity", row["qty"].as<int64_t>()}});
                    // 第 3 步: 按日趋势
                    auto db3 = drogon::app().getDbClient();
                    db3->execSqlAsync(
                        "SELECT to_char(inspected_at, 'YYYY-MM-DD') AS day, COUNT(*) AS total, "
                        "COUNT(*) FILTER (WHERE result = 1) AS pass_cnt, "
                        "COALESCE(SUM(defect_qty), 0) AS defect_qty "
                        "FROM qc_inspections "
                        "WHERE inspected_at >= COALESCE(NULLIF($1, '')::timestamptz, "
                        "NOW() - INTERVAL '30 days') "
                        "AND inspected_at < COALESCE(NULLIF($2, '')::timestamptz, NOW()) "
                        "+ INTERVAL '1 day' "
                        "GROUP BY 1 ORDER BY 1",
                        [summary, cats, onOk](const drogon::orm::Result& r3) {
                            nlohmann::json trend = nlohmann::json::array();
                            for (const auto& row : r3)
                                trend.push_back({{"day", row["day"].as<std::string>()},
                                                 {"total", row["total"].as<int64_t>()},
                                                 {"pass_cnt", row["pass_cnt"].as<int64_t>()},
                                                 {"defect_qty", row["defect_qty"].as<int64_t>()}});
                            onOk({{"summary", summary},
                                  {"defect_categories", cats},
                                  {"daily_trend", trend}});
                        },
                        [onErr](const drogon::orm::DrogonDbException& e) {
                            onErr(500, e.base().what());
                        },
                        startDate, endDate);
                },
                [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
                startDate, endDate);
        },
        [onErr](const drogon::orm::DrogonDbException& e) { onErr(500, e.base().what()); },
        startDate, endDate);
}

} // namespace hms::QcService
