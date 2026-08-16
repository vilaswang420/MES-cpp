#include <gtest/gtest.h>

#include "utils/DataScopeFilter.hh"

// data_scope 5 档 SQL 条件生成 + 多角色合并单测 (设计文档 5.4 节)。
using namespace mes::DataScopeFilter;

TEST(DataScopeFilter, Scope1OnlySelf) {
    auto cond = buildDeptCondition(1, 100, 10);
    EXPECT_EQ(cond, "created_by = 100");
}

TEST(DataScopeFilter, Scope2OwnDept) {
    auto cond = buildDeptCondition(2, 100, 10);
    EXPECT_EQ(cond, "dept_id = 10");
}

TEST(DataScopeFilter, Scope3RecursiveCte) {
    auto cond = buildDeptCondition(3, 100, 10);
    // 必须含递归 CTE 下钻, 且以本部门为根
    EXPECT_NE(cond.find("WITH RECURSIVE"), std::string::npos);
    EXPECT_NE(cond.find("parent_id"), std::string::npos);
    EXPECT_NE(cond.find("WHERE id = 10"), std::string::npos);
}

TEST(DataScopeFilter, Scope4All) {
    EXPECT_EQ(buildDeptCondition(4, 100, 10), "1=1");
}

TEST(DataScopeFilter, Scope5CustomDepts) {
    auto cond = buildDeptCondition(5, 100, 10);
    EXPECT_NE(cond.find("sys_role_dept_scope"), std::string::npos);
    EXPECT_NE(cond.find("user_id = 100"), std::string::npos);
}

TEST(DataScopeFilter, UnknownScopeFailClosed) {
    EXPECT_EQ(buildDeptCondition(0, 100, 10), "1=0");
    EXPECT_EQ(buildDeptCondition(9, 100, 10), "1=0");
}

TEST(DataScopeFilter, CustomDeptColumn) {
    auto cond = buildDeptCondition(2, 100, 10, "u.dept_id");
    EXPECT_EQ(cond, "u.dept_id = 10");
}

// ---- 多角色取最宽合并 ----

TEST(MergeDataScope, AnyScope4Wins) {
    EXPECT_EQ(mergeDataScope({1, 4, 2}), 4);
    EXPECT_EQ(mergeDataScope({4}), 4);
    EXPECT_EQ(mergeDataScope({5, 4}), 4);
}

TEST(MergeDataScope, WidestNonCustom) {
    EXPECT_EQ(mergeDataScope({1, 2}), 2);
    EXPECT_EQ(mergeDataScope({1, 3, 2}), 3);
    EXPECT_EQ(mergeDataScope({2, 5}), 2); // 5 与更宽的非自定义档合并, 取非自定义
    EXPECT_EQ(mergeDataScope({3, 5}), 3);
}

TEST(MergeDataScope, AllCustomKeeps5) {
    EXPECT_EQ(mergeDataScope({5}), 5);
    EXPECT_EQ(mergeDataScope({5, 5}), 5);
}

TEST(MergeDataScope, EmptyDefaultsToSelf) {
    // 无角色用户 fail-safe 为仅本人
    EXPECT_EQ(mergeDataScope({}), 1);
}
