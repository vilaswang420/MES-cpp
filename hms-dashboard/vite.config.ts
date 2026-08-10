import { defineConfig } from "vite";
import vue from "@vitejs/plugin-vue";

// hms-dashboard 大屏看板 (计划任务 21)。
// dev 期间 WebSocket 直连 hms-backend :8088/ws; 生产经 Nginx WSS 反代。
export default defineConfig({
    plugins: [vue()],
    server: {
        port: 5174
    }
});
