# ADR 0001: AuthForge 集成决策 — MVP 自研薄层, 不嵌入 SDK

- 状态: **已接受** (No-Go 嵌入式集成; 保留 M3 后 SSO 场景切换预案)
- 日期: 2026-08-09
- 决策者: HMS 架构组
- 相关: S0.3 spike (`spike/authforge-poc/`, 限时 3 天), 设计文档第 5 章

## 背景

HMS 需要用户认证 (账密 + 验证码 + JWT + 黑名单) 与授权 (RBAC + 5 档 `data_scope` 数据权限)。
候选方案:

- **A. 自研薄层**: 按设计文档第 5 章在 hms-backend 内实现, 约 1.5 人周;
- **B. 嵌入 AuthForge identity SDK**: 复用其用户模型/密码哈希/MFA;
- **C. AuthForge 独立授权服务先行**: HMS 作为客户端对接。

## 决策

**MVP 采用方案 A (自研薄层 + 部分借鉴)**:

1. 认证全链路 (账密 / 验证码 / JWT 签发 / Redis 黑名单登出) 自研于 hms-backend;
2. 可选借鉴 AuthForge identity 库的密码哈希与 MFA 实现思路, 但不引入其依赖与用户模型;
3. AuthForge 若引入, 时机不早于 M3, 且仅承担**认证协议层** (OAuth2/OIDC 签发、MFA/WebAuthn、IdP 联邦); HMS 作为 OIDC RP, RBAC 表继续负责授权与数据权限, 两侧以 `username/employee_no` 映射。

## 理由

1. **权责模型不可外置**: `data_scope` 5 档 + 递归部门 CTE + 多角色取最宽合并 (设计文档 5.4 节) 是 MES 特有权责模型, 任何通用 OAuth2 服务器不覆盖, 必须自研且留在 HMS 侧;
2. **双用户主数据漂移风险**: 嵌入式 SDK 会形成 AuthForge ORM 用户模型与 `sys_users` (含 `dept_id/employee_no`) 双份用户主数据, 双写漂移风险高;
3. **MVP 无外部需求**: 无 SSO/OIDC 联邦需求, 独立授权服务徒增登录路径一跳网络延迟与一个运维单元;
4. **S0.3 spike 实测佐证**: 3 天限时验证确认 `find_package` 构建兼容性成本与 schema 隔离成本均不为零, 而 MVP 侧收益为零。

## 后果

- 认证代码 (~1.5 人周) 由 HMS 团队自维护, 密码哈希采用 bcrypt (迁移 `001_auth` 已落库 `password_hash`);
- 后续若出现真实 SSO 需求, 按下方切换预案执行, 现有 RBAC/数据权限层**无需返工**。

## 切换预案 (M3 决策点触发)

当且仅当出现真实 SSO/OIDC 需求时重议:

1. 部署 AuthForge 作为独立 IdP, HMS 增加 OIDC RP 登录流 (authorization code + PKCE);
2. `sys_users` 保留为授权侧主数据, 以 `username/employee_no` 与 IdP 主体映射, 禁止反向双写;
3. JWT 校验中间件扩展为"自签 token 与 IdP token 双轨", 灰度切换后回收自签登录;
4. 权限映射门禁 (`scripts/check_perm_mapping.py`) 与 data_scope 层不受影响。

## 已拒绝的替代方案

- **B 嵌入式 SDK 作为 MVP 主方案**: 双用户主数据漂移、依赖升级耦合、MVP 无 OAuth2 需求;
- **C 独立授权服务先行**: 增加一跳与运维单元, MVP 收益为零。
