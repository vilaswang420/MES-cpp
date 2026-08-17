# mes-dashboard — MES 大屏看板（Vue3）

基于 Vue 3 + ECharts 5 + WebSocket 的实时大屏看板，面向车间生产监控、OEE 实时展示与告警可视化。

## 技术栈

- Vue 3 + TypeScript
- Vite（dev server 端口 **5174**）
- ECharts 5
- WebSocket（经 Redis Pub/Sub 跨实例广播，由 `mes-backend` 推送）

## 常用命令

```bash
npm install
npm run dev        # 开发服务器 http://localhost:5174
npm run build      # 类型检查 + 生产构建 (vue-tsc -b && vite build)
npm run lint       # eslint src --ext .ts,.vue
npm run preview    # 预览构建产物
```

## 配置

- WebSocket 地址通过 Vite 配置 / 环境变量注入，默认连接 `ws://localhost:8088`。
- OEE 优先展示后端计算的真值（`payload.oee ?? yield_rate`），含 可用率/性能/质量 三因子明细。

## 说明

- 看板与后端**仅通过 WebSocket** 通信，不轮询 REST。
- 降级策略（WS 断开时回退静态快照）在 M3 已做浏览器实证，详见 [HANDOVER.md](../HANDOVER.md) 与 [MES_Architecture_Design.md](../docs/MES_Architecture_Design.md)。
