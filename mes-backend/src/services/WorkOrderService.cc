#include "services/WorkOrderService.hh"

#include <drogon/drogon.h>
#include <drogon/orm/CoroMapper.h>

#include <chrono>
#include <cstdio>
#include <random>
#include <sstream>

#include "common/ApiResponse.hh"
#include "common/SqlParam.hh"
#include "models/WorkOrderStateMachine.hh"
#include "mq/OutboxDispatcher.hh"
#include "utils/ReportRules.hh"
#include "utils/TimeUtils.hh"

namespace mes::WorkOrderService {

namespace orm = drogon::orm;
using WorkOrderStateMachine::Event;

namespace {

// ---- 工具 ----

// 工单号 = WO + UTC日期 + 全局序列 (历史版本用随机三位数, 同日工单多时碰撞致 500)
std::string genWorkOrderNo(int64_t seq) {
    auto now = std::chrono::system_clock::now();
    std::time_t t = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &t);
#else
    gmtime_r(&t, &tm);
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "WO%04d%02d%02d%05lld", tm.tm_year + 1900, tm.tm_mon + 1,
                  tm.tm_mday, static_cast<long long>(seq));
    return buf;
}

std::string num(int64_t v) {
    return std::to_string(v);
}

// data_scope 过滤条件 (工单表无 dept 字段, 经 created_by -> sys_users.dept_id 下钻;
// 档位语义与 utils/DataScopeFilter.hh 一致, 递归 CTE 需提升到查询顶层)
struct ScopeSql {
    std::string cte;       // WITH 前缀 (仅档位 3)
    std::string condition; // wo 上的过滤条件
};

ScopeSql buildScopeSql(const UserCtx& ctx) {
    ScopeSql s;
    switch (ctx.dataScope) {
    case 4:
        s.condition = "1=1";
        break;
    case 1:
        s.condition = "wo.created_by = " + num(ctx.userId);
        break;
    case 2:
        s.condition =
            "wo.created_by IN (SELECT id FROM sys_users WHERE dept_id = " + num(ctx.deptId) + ")";
        break;
    case 3:
        s.cte = "WITH RECURSIVE dept_tree(id) AS ("
                "SELECT id FROM sys_departments WHERE id = " +
                num(ctx.deptId) +
                " UNION ALL "
                "SELECT d.id FROM sys_departments d JOIN dept_tree t ON d.parent_id = t.id) ";
        s.condition = "wo.created_by IN (SELECT id FROM sys_users WHERE dept_id IN "
                      "(SELECT id FROM dept_tree))";
        break;
    case 5: {
        if (ctx.customDeptIds.empty()) {
            s.condition = "1=0"; // 未配置自定义部门 -> fail-closed
            break;
        }
        std::ostringstream ids;
        for (size_t i = 0; i < ctx.customDeptIds.size(); ++i) {
            if (i)
                ids << ",";
            ids << ctx.customDeptIds[i];
        }
        s.condition =
            "wo.created_by IN (SELECT id FROM sys_users WHERE dept_id IN (" + ids.str() + "))";
        break;
    }
    default:
        s.condition = "1=0"; // 未知档位一律拒绝
    }
    return s;
}

const char* kWoSelect =
    "SELECT wo.id, wo.work_order_no, wo.product_id, p.product_name, wo.line_id, "
    "l.line_name, wo.plan_qty, wo.completed_qty, wo.good_qty, wo.defect_qty, wo.scrap_qty, "
    "wo.status, wo.priority, wo.source, wo.remark, wo.plan_start_at, wo.plan_end_at, "
    "wo.actual_start_at, wo.actual_end_at, wo.created_by, wo.created_at "
    "FROM prod_work_orders wo "
    "LEFT JOIN prod_products p ON p.id = wo.product_id "
    "LEFT JOIN prod_production_lines l ON l.id = wo.line_id ";

nlohmann::json woToJson(const orm::Row& row) {
    nlohmann::json j;
    auto str = [&row](const char* f) {
        return row[f].isNull() ? std::string() : row[f].as<std::string>();
    };
    j["id"] = row["id"].as<int64_t>();
    j["work_order_no"] = str("work_order_no");
    j["product_id"] = row["product_id"].isNull() ? 0 : row["product_id"].as<int64_t>();
    j["product_name"] = str("product_name");
    j["line_id"] = row["line_id"].isNull() ? 0 : row["line_id"].as<int64_t>();
    j["line_name"] = str("line_name");
    j["plan_qty"] = row["plan_qty"].as<int>();
    j["completed_qty"] = row["completed_qty"].as<int>();
    j["good_qty"] = row["good_qty"].as<int>();
    j["defect_qty"] = row["defect_qty"].as<int>();
    j["scrap_qty"] = row["scrap_qty"].as<int>();
    j["status"] = row["status"].as<int>();
    j["status_name"] = WorkOrderStateMachine::statusName(row["status"].as<int>());
    j["priority"] = row["priority"].as<int>();
    j["source"] = row["source"].as<int>();
    j["remark"] = str("remark");
    j["plan_start_at"] = str("plan_start_at");
    j["plan_end_at"] = str("plan_end_at");
    j["actual_start_at"] = str("actual_start_at");
    j["actual_end_at"] = str("actual_end_at");
    j["created_by"] = row["created_by"].isNull() ? 0 : row["created_by"].as<int64_t>();
    j["created_at"] = str("created_at");
    return j;
}

Event actionToEvent(const std::string& action) {
    if (action == "schedule")
        return Event::Schedule;
    if (action == "release")
        return Event::Release;
    if (action == "start")
        return Event::Start;
    if (action == "pause")
        return Event::Pause;
    if (action == "complete")
        return Event::Complete;
    if (action == "close")
        return Event::Close;
    if (action == "cancel")
        return Event::Cancel;
    throw BadRequest("未知操作: " + action);
}

} // namespace

// ---- 列表 ----
drogon::Task<nlohmann::json> list(int page, int pageSize, int status, int64_t lineId,
                                  const UserCtx& ctx) {
    if (page < 1)
        page = 1;
    if (pageSize < 1 || pageSize > 200)
        pageSize = 20;

    auto scope = buildScopeSql(ctx);
    std::string where = "WHERE " + scope.condition;
    if (status >= 0)
        where += " AND wo.status = " + num(status);
    if (lineId > 0)
        where += " AND wo.line_id = " + num(lineId);

    auto db = drogon::app().getDbClient();
    auto countRes = co_await db->execSqlCoro(
        scope.cte + "SELECT COUNT(*) AS cnt FROM prod_work_orders wo " + where);
    int64_t total = countRes[0]["cnt"].as<int64_t>();

    auto rows = co_await db->execSqlCoro(scope.cte + std::string(kWoSelect) + where +
                                         " ORDER BY wo.created_at DESC LIMIT " + num(pageSize) +
                                         " OFFSET " + num((page - 1) * pageSize));
    nlohmann::json listArr = nlohmann::json::array();
    for (const auto& row : rows)
        listArr.push_back(woToJson(row));

    co_return nlohmann::json{
        {"list", listArr}, {"total", total}, {"page", page}, {"page_size", pageSize}};
}

// ---- 详情 ----
drogon::Task<nlohmann::json> detail(int64_t id, const UserCtx& ctx) {
    auto db = drogon::app().getDbClient();
    auto scope = buildScopeSql(ctx);
    auto r = co_await db->execSqlCoro(
        std::string(kWoSelect) + "WHERE wo.id = $1 AND " + scope.condition, SqlArg(id));
    if (r.empty())
        throw NotFound("工单不存在或无权访问");
    auto data = woToJson(r[0]);

    auto ops = co_await db->execSqlCoro(
        "SELECT id, step_seq, step_name, plan_qty, completed_qty, good_qty, defect_qty, "
        "scrap_qty, operator_id, status, workstation_id FROM prod_work_order_operations "
        "WHERE work_order_id = $1 ORDER BY step_seq",
        SqlArg(id));
    nlohmann::json opArr = nlohmann::json::array();
    for (const auto& op : ops) {
        opArr.push_back({
            {"id", op["id"].as<int64_t>()},
            {"step_seq", op["step_seq"].as<int>()},
            {"step_name", op["step_name"].as<std::string>()},
            {"plan_qty", op["plan_qty"].as<int>()},
            {"completed_qty", op["completed_qty"].as<int>()},
            {"good_qty", op["good_qty"].as<int>()},
            {"defect_qty", op["defect_qty"].as<int>()},
            {"scrap_qty", op["scrap_qty"].as<int>()},
            {"operator_id", op["operator_id"].isNull() ? 0 : op["operator_id"].as<int64_t>()},
            {"status", op["status"].as<int>()},
            {"workstation_id",
             op["workstation_id"].isNull() ? 0 : op["workstation_id"].as<int64_t>()},
        });
    }
    data["operations"] = opArr;
    co_return data;
}

// ---- 创建 ----
drogon::Task<nlohmann::json> create(const nlohmann::json& body, const UserCtx& ctx) {
    auto productId = body.value("product_id", (int64_t)0);
    auto planQty = body.value("plan_qty", 0);
    if (productId <= 0 || planQty <= 0)
        throw BadRequest("product_id 与 plan_qty 必填且大于 0");

    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();

    // 工艺路线: 显式指定或取产品默认
    int64_t processId = body.value("process_id", (int64_t)0);
    if (processId == 0) {
        auto pr = co_await trans->execSqlCoro(
            "SELECT process_id FROM prod_products WHERE id = $1 AND deleted = FALSE",
            SqlArg(productId));
        if (pr.empty())
            throw NotFound("产品不存在");
        if (pr[0]["process_id"].isNull())
            throw BadRequest("产品未绑定工艺路线, 请先指定 process_id");
        processId = pr[0]["process_id"].as<int64_t>();
    }

    auto steps =
        co_await trans->execSqlCoro("SELECT id, step_seq, step_name FROM prod_process_steps "
                                    "WHERE process_id = $1 ORDER BY step_seq",
                                    SqlArg(processId));
    if (steps.empty())
        throw BadRequest("工艺路线无工序步骤, 无法创建工单");

    auto seqRow = co_await trans->execSqlCoro("SELECT nextval('prod_work_order_no_seq')");
    auto woNo = genWorkOrderNo(seqRow[0][0].as<int64_t>());
    auto ins = co_await trans->execSqlCoro(
        "INSERT INTO prod_work_orders (work_order_no, product_id, process_id, line_id, "
        "plan_qty, priority, status, source, remark, created_by) "
        "VALUES ($1,$2,$3,NULLIF($4::bigint,0),$5::int,$6::int,0,2,$7,$8) RETURNING id",
        woNo, SqlArg(productId), SqlArg(processId), SqlArg(body.value("line_id", (int64_t)0)),
        SqlArg(planQty), SqlArg(body.value("priority", 5)), body.value("remark", ""),
        SqlArg(ctx.userId));
    auto woId = ins[0]["id"].as<int64_t>();

    // 按工艺步骤生成工序行 (每道工序计划量 = 工单计划量)
    for (const auto& s : steps) {
        co_await trans->execSqlCoro(
            "INSERT INTO prod_work_order_operations "
            "(work_order_id, process_step_id, step_seq, step_name, plan_qty, status) "
            "VALUES ($1,$2,$3,$4,$5,0)",
            SqlArg(woId), SqlArg(s["id"].as<int64_t>()), SqlArg(s["step_seq"].as<int>()),
            s["step_name"].as<std::string>(), SqlArg(planQty));
    }
    // 等待 COMMIT 落库后再响应, 避免调用方写后读不一致
    if (!co_await commitAwait(std::move(trans)))
        throw Internal("工单事务提交失败");
    co_return nlohmann::json{{"id", woId}, {"work_order_no", woNo}, {"status", 0}};
}

// ---- 修改 (仅待排产/已排产) ----
drogon::Task<nlohmann::json> update(int64_t id, const nlohmann::json& body, const UserCtx& ctx) {
    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();
    auto r = co_await trans->execSqlCoro(
        "SELECT status FROM prod_work_orders WHERE id = $1 FOR UPDATE", SqlArg(id));
    if (r.empty())
        throw NotFound("工单不存在");
    auto status = r[0]["status"].as<int>();
    if (status > WorkOrderStateMachine::kScheduled)
        throw Conflict("已下达的工单不允许修改");

    std::vector<std::string> sets;
    if (body.contains("plan_qty"))
        sets.push_back("plan_qty = " + num(body["plan_qty"].get<int>()));
    if (body.contains("priority"))
        sets.push_back("priority = " + num(body["priority"].get<int>()));
    if (body.contains("line_id"))
        sets.push_back("line_id = " + num(body["line_id"].get<int64_t>()));
    if (sets.empty())
        throw BadRequest("无可更新字段");
    std::string setSql;
    for (size_t i = 0; i < sets.size(); ++i) {
        if (i)
            setSql += ", ";
        setSql += sets[i];
    }
    co_await trans->execSqlCoro(
        "UPDATE prod_work_orders SET " + setSql + ", updated_at = NOW() WHERE id = $1", SqlArg(id));
    // 计划量变化同步未开工的工序行
    if (body.contains("plan_qty"))
        co_await trans->execSqlCoro("UPDATE prod_work_order_operations SET plan_qty = $1 "
                                    "WHERE work_order_id = $2 AND status = 0",
                                    SqlArg(body["plan_qty"].get<int>()), SqlArg(id));
    if (!co_await commitAwait(std::move(trans)))
        throw Internal("工单事务提交失败");
    co_return nlohmann::json{{"id", id}, {"updated", true}};
}

// ---- 状态流转 (表驱动) ----
drogon::Task<nlohmann::json> transit(int64_t id, const std::string& action, const UserCtx& ctx) {
    auto event = actionToEvent(action);
    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();

    auto r = co_await trans->execSqlCoro(
        "SELECT status, completed_qty, plan_qty FROM prod_work_orders WHERE id = $1 FOR UPDATE",
        SqlArg(id));
    if (r.empty())
        throw NotFound("工单不存在");
    auto from = r[0]["status"].as<int>();
    auto to = WorkOrderStateMachine::next(from, event);
    if (to < 0)
        throw Conflict(std::string("非法状态流转: ") + WorkOrderStateMachine::statusName(from) +
                       " -> " + action);

    std::string extra;
    if (event == Event::Start && r[0]["completed_qty"].as<int>() == 0)
        extra = ", actual_start_at = NOW()";
    if (event == Event::Complete || event == Event::Close)
        extra = ", actual_end_at = COALESCE(actual_end_at, NOW())";
    co_await trans->execSqlCoro("UPDATE prod_work_orders SET status = $1" + extra +
                                    ", updated_at = NOW() WHERE id = $2",
                                SqlArg(to), SqlArg(id));
    if (!co_await commitAwait(std::move(trans)))
        throw Internal("工单事务提交失败");
    co_return nlohmann::json{
        {"id", id}, {"status", to}, {"status_name", WorkOrderStateMachine::statusName(to)}};
}

// ---- 报工 (7.5 节事务范式) ----
drogon::Task<nlohmann::json> report(int64_t id, const nlohmann::json& body, const UserCtx& ctx) {
    auto stepSeq = body.value("step_seq", 0);
    auto goodQty = body.value("good_qty", 0);
    auto defectQty = body.value("defect_qty", 0);
    auto scrapQty = body.value("scrap_qty", 0);
    // P2-3.2 规则统一入口: 序号为正 / 数量非负 / 总量为正 (规则见 ReportRules.hh, 单测覆盖)
    if (!ReportRules::validStep(stepSeq))
        throw BadRequest("step_seq 必填且大于 0");
    if (!ReportRules::validQty(goodQty, defectQty, scrapQty))
        throw BadRequest("good_qty/defect_qty/scrap_qty 不可为负");
    auto delta = ReportRules::totalDelta(goodQty, defectQty, scrapQty);
    if (delta <= 0)
        throw BadRequest("报工数量 (good+defect+scrap) 必须大于 0");

    auto db = drogon::app().getDbClient();
    auto trans = co_await db->newTransactionCoro();

    // P1-2.4 质检门禁: 灰度开关 quality_gate_enabled (sys_configs, 默认开启可回退);
    // 需质检工序 (prod_process_steps.quality_check=true) 报工前须已有合格/让步检验记录
    // (qc_inspections.result IN (1,3)); 无记录则 409 拒绝 (事务未提交, 数量不变)。
    // 注: 检验记录 operation_id 可为 NULL (创建时未关联工序), 兼容按工单级匹配;
    // 工序不存在或 process_step_id 为 NULL (老数据) 时跳过门禁 (无法判定, 降级放行)。
    auto gate = co_await trans->execSqlCoro(
        "SELECT ps.quality_check, "
        "  COALESCE((SELECT config_value FROM sys_configs WHERE config_key = "
        "  'quality_gate_enabled'), 'true') = 'true' AS gate_enabled, "
        "  EXISTS (SELECT 1 FROM qc_inspections qi "
        "          WHERE qi.work_order_id = $1 "
        "            AND (qi.operation_id = op.id OR qi.operation_id IS NULL) "
        "            AND qi.result IN (1, 3)) AS inspected "
        "FROM prod_work_order_operations op "
        "JOIN prod_process_steps ps ON ps.id = op.process_step_id "
        "WHERE op.work_order_id = $1 AND op.step_seq = $2",
        SqlArg(id), SqlArg(stepSeq));
    if (!gate.empty() && gate[0]["quality_check"].as<bool>() &&
        gate[0]["gate_enabled"].as<bool>() && !gate[0]["inspected"].as<bool>())
        throw Conflict("工序需质检: 尚无合格/让步检验记录, 请先完成质检后报工");

    // 单 SQL 合并更新 (性能优化: 1 次往返替代 SELECT FOR UPDATE x2 + UPDATE x2,
    // 行锁持有期压缩到仅剩本语句 + COMMIT 等待):
    //  - CTE 内条件 UPDATE 工序: completed_qty + delta <= plan_qty 原子防超报, 满量置完工
    //  - 主 UPDATE 工单汇总: 仅进行中可报工, EXISTS(op) 保证工序命中才更新, 满量自动完工
    auto r = co_await trans->execSqlCoro(
        "WITH op AS ("
        "  UPDATE prod_work_order_operations SET "
        "    completed_qty = completed_qty + $1, good_qty = good_qty + $2, "
        "    defect_qty = defect_qty + $3, scrap_qty = scrap_qty + $5, operator_id = $4, "
        "    status = CASE WHEN completed_qty + $1 >= plan_qty THEN 2 ELSE 1 END, "
        "    actual_start_at = COALESCE(actual_start_at, NOW()), "
        "    actual_end_at = CASE WHEN completed_qty + $1 >= plan_qty THEN NOW() "
        "                         ELSE actual_end_at END "
        "  WHERE work_order_id = $6 AND step_seq = $7 AND completed_qty + $1 <= plan_qty "
        "  RETURNING id"
        ") "
        "UPDATE prod_work_orders SET "
        "  completed_qty = completed_qty + $1, good_qty = good_qty + $2, "
        "  defect_qty = defect_qty + $3, scrap_qty = scrap_qty + $5, "
        "  status = CASE WHEN completed_qty + $1 >= plan_qty THEN $8 ELSE status END, "
        "  actual_end_at = CASE WHEN completed_qty + $1 >= plan_qty THEN NOW() "
        "                       ELSE actual_end_at END, "
        "  updated_at = NOW() "
        "WHERE id = $6 AND status = $9 AND EXISTS (SELECT 1 FROM op) "
        "RETURNING completed_qty >= plan_qty AS finished, work_order_no, product_id",
        SqlArg(delta), SqlArg(goodQty), SqlArg(defectQty), SqlArg(ctx.userId), SqlArg(scrapQty),
        SqlArg(id), SqlArg(stepSeq), SqlArg(static_cast<int>(WorkOrderStateMachine::kCompleted)),
        SqlArg(static_cast<int>(WorkOrderStateMachine::kInProgress)));
    if (r.empty()) {
        // 未命中: 走诊断路径给出精确错误 (仅异常路径多次往返, 不影响热路径)
        auto wo = co_await trans->execSqlCoro("SELECT status FROM prod_work_orders WHERE id = $1",
                                              SqlArg(id));
        if (wo.empty())
            throw NotFound("工单不存在");
        if (wo[0]["status"].as<int>() != WorkOrderStateMachine::kInProgress)
            throw Conflict("仅进行中的工单可报工");
        auto op = co_await trans->execSqlCoro(
            "SELECT completed_qty, plan_qty FROM prod_work_order_operations "
            "WHERE work_order_id = $1 AND step_seq = $2",
            SqlArg(id), SqlArg(stepSeq));
        if (op.empty())
            throw NotFound("工序不存在");
        throw Conflict("工序超报: 已完成 " + std::to_string(op[0]["completed_qty"].as<int>()) +
                       "/" + std::to_string(op[0]["plan_qty"].as<int>()));
    }

    // 满量自动完工: 事务内写 mq_outbox (停采指令), COMMIT 后投递
    bool finished = r[0]["finished"].as<bool>();
    if (finished) {
        nlohmann::json msg;
        msg["version"] = "1.0";
        msg["type"] = "stop_collection";
        msg["work_order_id"] = id;
        msg["work_order_no"] = r[0]["work_order_no"].as<std::string>();
        msg["product_id"] = r[0]["product_id"].isNull() ? 0 : r[0]["product_id"].as<int64_t>();
        msg["timestamp"] = TimeUtils::nowUtcIso();
        co_await trans->execSqlCoro(OutboxService::kEnqueueSql, "iot.exchange",
                                    "cmd.stop_collection", msg.dump());
    }
    // 等待 COMMIT 落库 (outbox 随之可见, OutboxDispatcher 异步投递), 杜绝"提交前响应"
    if (!co_await commitAwait(std::move(trans)))
        throw Internal("报工事务提交失败");
    co_return nlohmann::json{{"id", id},
                             {"step_seq", stepSeq},
                             {"reported", delta},
                             {"finished", finished},
                             {"status", finished ? WorkOrderStateMachine::kCompleted
                                                 : WorkOrderStateMachine::kInProgress}};
}

} // namespace mes::WorkOrderService
