// WebSocket 订阅 composable (计划任务 21 / contracts/ws-push.schema.json)。
// 订阅协议: 连接建立后发送 {"action":"subscribe","channel":...};
// 推送信封 {version,channel,ts,payload}; 断线自动重连 (指数退避上限 30s)。
import { onUnmounted, ref } from "vue";

export interface WsPushMessage {
    version: string;
    channel: string;
    ts: string;
    payload: Record<string, unknown>;
}

const WS_URL = (import.meta.env.VITE_WS_URL as string) ?? "ws://127.0.0.1:8088/ws";

export function useChannel(channel: string, onMessage: (payload: Record<string, unknown>, ts: string) => void) {
    const connected = ref(false);
    let ws: WebSocket | null = null;
    let retry = 0;
    let closed = false;
    let timer: ReturnType<typeof setTimeout> | undefined;

    function connect() {
        if (closed) return;
        ws = new WebSocket(WS_URL);
        ws.onopen = () => {
            connected.value = true;
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
            const delay = Math.min(30000, 1000 * 2 ** retry++);
            timer = setTimeout(connect, delay);
        };
        ws.onerror = () => ws?.close();
    }

    connect();

    onUnmounted(() => {
        closed = true;
        if (timer) clearTimeout(timer);
        ws?.close();
    });

    return { connected };
}
