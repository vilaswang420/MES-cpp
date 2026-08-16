import { createApp } from "vue";
import App from "./App.vue";

// mes-dashboard 大屏入口 (计划任务 21)。
// 严格后置: 页面数据一律来自 WebSocket 频道推送, 不使用假数据联调。
createApp(App).mount("#app");
