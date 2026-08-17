# mes-web — MES 管理后台（React）

基于 React 18 + Vite + Ant Design 5 的管理后台，面向车间管理、生产排程、质量、权限等日常业务操作。

## 技术栈

- React 18 + TypeScript
- Vite（dev server 端口 **5173**）
- Ant Design 5（含 `@ant-design/icons`）
- react-router-dom 6
- dayjs

## 常用命令

```bash
npm install
npm run dev        # 开发服务器 http://localhost:5173
npm run build      # 类型检查 + 生产构建 (tsc -b && vite build)
npm run lint       # eslint src --ext .ts,.tsx
npm run preview    # 预览构建产物
```

## 配置

- 后端 API 基址通过 Vite 配置 / 环境变量注入，默认指向 `http://localhost:8088`。
- 路由与权限菜单由后端 RBAC（`sys_*` 表）驱动，前端按 `perm_routes` 映射渲染。

## 说明

- 代码规范与整体架构见 [DEV_GUIDE.md](../docs/DEV_GUIDE.md)。
- 管理后台不直连数据库，所有数据经由 `mes-backend` REST API。
