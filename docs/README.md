# MES 文档索引

> 本文是项目全部文档的**导航与状态总表**。新增/废弃文档时请同步更新本表。
> 最近更新：2026-08-17（HMS→MES 全量更名后重建索引）。

## 一、按用途分类

### 1. 设计与架构
| 文档 | 说明 | 状态 |
|------|------|------|
| [MES_Architecture_Design.md](MES_Architecture_Design.md) | 完整架构设计（技术事实源，2551 行） | v1.0 / 已定稿 |
| [adr/0001-authforge-integration.md](adr/0001-authforge-integration.md) | ADR 0001：AuthForge 集成决策（MVP 自研薄层） | 已接受 |

### 2. 开发指南
| 文档 | 说明 | 状态 |
|------|------|------|
| [DEV_GUIDE.md](DEV_GUIDE.md) | 开发维护指南：项目结构 / 编码规范 / 调试 / 踩坑速查 | v1.0 |
| [BUILD_PROGRESS.md](BUILD_PROGRESS.md) | 首次真实构建验证过程与结论（任务 s1–s5） | 维护中 |
| 各子服务 README | mes-backend / mes-iot / mes-web / mes-dashboard 构建运行说明 | 2026-08-17 新建 |

### 3. 部署与运维
| 文档 | 说明 | 状态 |
|------|------|------|
| [DEPLOY_LINUX.md](DEPLOY_LINUX.md) | Linux Ubuntu 24.04 部署完整手册（含 HTTPS / Let's Encrypt） | v1.0 |
| [PAD_MANUAL.md](PAD_MANUAL.md) | 手持 PAD 扫码接入 / 日常操作手册（车间操作员向） | v1.0 |

### 4. 功能与缺口分析
| 文档 | 说明 | 状态 |
|------|------|------|
| [FEATURE_INVENTORY.md](FEATURE_INVENTORY.md) | 已有功能清单（76 路由）+ 核心缺口分析 | v1.1 |
| [GAP_ANALYSIS.md](GAP_ANALYSIS.md) | 功能缺失详细分析（7 核心 / 11 增强 / 8 技术债，含文件行号） | v1.0 |
| [CORE_PLAN.md](CORE_PLAN.md) | 核心功能完善方案（P1 正确性 / P2 稳定性 / P3 安全性） | v1.1 / P1–P3 已完成 |
| [P4_IMPLEMENTATION_PLAN.md](P4_IMPLEMENTATION_PLAN.md) | P4 缺失功能实施方案（5.1–5.7） | v1.0 / 已完成（代码与集成已落地；容器化待补） |

### 5. 过程 / 交接文档
| 文档 | 说明 | 状态 |
|------|------|------|
| [../HANDOVER.md](../HANDOVER.md) | 项目交接文档（新会话 / 新接手者必读，含 43 条踩坑） | 维护中 |
| [../CONTRIBUTING.md](../CONTRIBUTING.md) | 贡献指南：分支 / 提交 / 迁移 / PR 规范 | 维护中 |
| [../.qoder/plans/MES_分阶段实施计划_87e5c117.md](../.qoder/plans/MES_分阶段实施计划_87e5c117.md) | 分阶段实施计划（greenfield 全量路线图） | 历史参考 |

## 二、文档健康度速览

- **命名一致性**：2026-08-17 起全量使用 MES（HMS 旧名已废弃，机械替换覆盖 256 个跟踪文件）。
- **契约单一事实源**：`contracts/*.json`（MQ / WS / 错误响应 Schema）为接口权威定义，代码与文档不得与之冲突。
- **待补强**：API 参考文档（当前以 `contracts/` + 路由代码为事实源，未在 docs/ 展开）；监控告警手册（Prometheus 指标已在 M3 落地，未单独成文）。

## 三、阅读建议

- **新接手**：README → HANDOVER.md → MES_Architecture_Design.md → DEV_GUIDE.md
- **要部署**：DEPLOY_LINUX.md + PAD_MANUAL.md
- **要开发某模块**：对应子服务 README + FEATURE_INVENTORY.md + GAP_ANALYSIS.md
- **要排期需求**：CORE_PLAN.md（已完成项）+ P4_IMPLEMENTATION_PLAN.md（已完成，容器化待补）
