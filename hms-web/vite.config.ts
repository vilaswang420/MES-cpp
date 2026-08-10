import { defineConfig } from "vite";
import react from "@vitejs/plugin-react";

// hms-web 管理后台 (计划任务 12/16)。dev 代理到 hms-backend :8088。
export default defineConfig({
    plugins: [react()],
    server: {
        port: 5173,
        proxy: {
            "/api": {
                target: "http://127.0.0.1:8088",
                changeOrigin: true
            }
        }
    }
});
