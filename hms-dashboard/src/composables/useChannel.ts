// WebSocket 订阅 composable (计划任务 21 / contracts/ws-push.schema.json)。
// 订阅协议: 连接建立后发送 {"action":"subscribe","channel":...};
// 推送信封 {version,channel,ts,payload}; 断线自动重连 (指数退避上限 30s)。
// 鉴权: 后端 /ws 以 query token 严格校验 (浏览器 WS 无法带 Authorization 头),
// token 优先读 localStorage.hms_ws_token, 否则用大屏专用账号自动登录获取。
import { onUnmounted, ref } from "vue";

export interface WsPushMessage {
    version: string;
    channel: string;
    ts: string;
    payload: Record<string, unknown>;
}

// 同源接入: dev 经 vite proxy, 生产经 Nginx 反代 (后端未启 CORS, 不跨源直连)
const WS_BASE = (import.meta.env.VITE_WS_URL as string) ??
    `${location.protocol === "https:" ? "wss" : "ws"}://${location.host}/ws`;
const API_BASE = (import.meta.env.VITE_API_BASE as string) ?? "";

let tokenPromise: Promise<string> | null = null;

// 获取 WS 接入 token: localStorage 覆盖 > 自动登录 (失败返回空串, 连接将被后端拒绝并重连)
export function acquireToken(): Promise<string> {
    const cached = localStorage.getItem("hms_ws_token");
    if (cached) return Promise.resolve(cached);
    if (!tokenPromise) {
        tokenPromise = fetch(`${API_BASE}/api/v1/auth/login`, {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({
                username: (import.meta.env.VITE_DASH_USER as string) ?? "admin",
                password: (import.meta.env.VITE_DASH_PWD as string) ?? "password",
            }),
        })
            .then((r) => r.json())
            .then((j) => (j.code === 200 ? String(j.data.access_token) : ""))
            .catch(() => "");
    }
    return tokenPromise;
}

export function useChannel(channel: string, onMessage: (payload: Record<string, unknown>, ts: string) => void) {
    const connected = ref(false);
    // 降级态 (任务 28 看板降级策略): 连续重连失败 >= 3 次置 true,
    // 页面据此切 REST 轮询展示历史数据; WS 恢复后自动回 false
    const degraded = ref(false);
    let ws: WebSocket | null = null;
    let retry = 0;
    let closed = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    function connect() {
        if (closed) return;
        void acquireToken().then((token) => {
            if (closed) return;
            ws = new WebSocket(`${WS_BASE}?token=${encodeURIComponent(token)}`);
            ws.onopen = () => {
                connected.value = true;
                degraded.value = false;
                retry = 0;
                ws?.send(JSON.stringify({ action: "subscribe", channel }));
            };
            ws.onmessage = (ev) => {
                try {
                    const msg = JSON.parse(ev.data as string) as WsPushMessage;
                    if (msg.channel === channel) onMessage(msg.payload, msg.ts);
                } catch {
                    // 非法消息丢弃 (契约校验在后端 WsBroadcastManager 侧兜底)
                }
            };
            ws.onclose = () => {
                connected.value = false;
                if (closed) return;
                if (retry >= 3) degraded.value = true;
                const delay = Math.min(30000, 1000 * 2 ** retry++);
                timer = setTimeout(connect, delay);
            };
            ws.onerror = () => ws?.close();
        });
    }

    connect();

    onUnmounted(() => {
        closed = true;
        if (timer) clearTimeout(timer);
        ws?.close();
    });

    return { connected, degraded };
}
