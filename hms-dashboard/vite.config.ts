import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

// hms-dashboard 大屏看板 (计划任务 21)。
// dev 期间 /api 与 /ws 经 vite proxy 同源转发到 hms-backend :8088
// (后端未启 CORS, 与生产 Nginx 反代同源模型一致)。
// HMS_PROXY_WS 可覆盖 /ws 目标 (任务 28 降级演练: 指向死端口模拟实时链路宕机)。
export default defineConfig({
    plugins: [vue()],
    server: {
        port: 5174,
        proxy: {
            "/api": {
                target: "http://127.0.0.1:8088",
                changeOrigin: true
            },
            "/ws": {
                target: process.env.HMS_PROXY_WS ?? "ws://127.0.0.1:8088",
                ws: true
            }
        }
    }
});
