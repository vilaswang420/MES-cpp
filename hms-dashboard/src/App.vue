<!-- 大屏根组件 (计划任务 21): 三频道布局 = 生产实时 / 设备状态 / 告警。
     降级策略 (任务 28): WS 连续重连失败 → REST 轮询展示历史数据 (IoT/实时链路宕机不影响看板可用性)。 -->
<script setup lang="ts">
import { reactive, ref, watch } from "vue";
import { acquireToken, useChannel } from "./composables/useChannel";

interface AlertItem {
    ts: string;
    device_code: string;
    level: string;
    message: string;
}

const production = reactive<Record<string, unknown>>({});
const devices = reactive<Record<string, unknown>>({});
const alerts = ref<AlertItem[]>([]);
const lastTs = ref("");

const p1 = useChannel("production.realtime", (payload, ts) => {
    Object.assign(production, payload);
    lastTs.value = ts;
});
useChannel("device.status", (payload) => {
    const id = String(payload.device_id ?? "");
    if (id) devices[id] = payload;
});
useChannel("alert", (payload, ts) => {
    alerts.value.unshift({
        ts,
        device_code: String(payload.device_code ?? ""),
        level: String(payload.level ?? "info"),
        message: String(payload.message ?? "")
    });
    if (alerts.value.length > 50) alerts.value.pop();
});

// ---- 降级模式: WS 不可达时轮询 REST 历史数据 (任务 28) ----
let pollTimer: ReturnType<typeof setInterval> | undefined;

async function api(path: string): Promise<Record<string, unknown> | null> {
    const token = await acquireToken();
    if (!token) return null;
    try {
        const r = await fetch(path, { headers: { Authorization: `Bearer ${token}` } });
        const j = await r.json();
        return j.code === 200 ? (j.data as Record<string, unknown>) : null;
    } catch {
        return null;
    }
}

async function fetchSnapshot() {
    // 生产实时降级源: 在制工单列表首条 (与 realtime 推送同数据源)
    const wo = (await api("/api/v1/production/work-orders?page=1&page_size=1&status=3")) as
        { list?: Record<string, unknown>[] } | null;
    const first = wo?.list?.[0];
    if (first) {
        const plan = Number(first.plan_qty ?? 0);
        const good = Number(first.good_qty ?? 0);
        Object.assign(production, {
            work_order_no: first.work_order_no,
            product_name: first.product_name,
            line_name: first.line_name,
            target_qty: plan,
            completed_qty: first.completed_qty,
            good_qty: good,
            defect_qty: first.defect_qty,
            yield_rate: plan > 0 ? (good * 100) / plan : 0,
            status: first.status,
            timestamp: new Date().toISOString(),
            degraded_source: true,
        });
        lastTs.value = new Date().toISOString();
    }
    // 告警降级源: 最近告警历史
    const al = (await api("/api/v1/iot/alerts?page=1&page_size=20")) as
        { list?: Record<string, unknown>[] } | null;
    if (al?.list?.length) {
        alerts.value = al.list.map((a) => ({
            ts: String(a.created_at ?? a.ts ?? ""),
            device_code: String(a.device_code ?? a.device_name ?? ""),
            level: String(a.alert_level ?? "info"),
            message: String(a.message ?? ""),
        }));
    }
}

watch(p1.degraded, (d) => {
    if (pollTimer) {
        clearInterval(pollTimer);
        pollTimer = undefined;
    }
    if (d) {
        void fetchSnapshot();
        pollTimer = setInterval(() => void fetchSnapshot(), 10000);
    }
}, { immediate: true });
</script>

<template>
    <div class="dashboard">
        <header class="dash-header">
            <h1>HMS 生产实时看板</h1>
            <div class="status">
                <span :class="['dot', p1.connected.value ? 'on' : 'off']"></span>
                <span v-if="p1.degraded.value" class="degraded">降级模式：实时链路不可用，展示历史数据 (10s 轮询)</span>
                <span v-else>{{ p1.connected.value ? "实时连接正常" : "连接断开, 重连中..." }}</span>
                <span v-if="lastTs" class="ts">最后推送: {{ lastTs }}</span>
            </div>
        </header>
        <main class="dash-grid">
            <section class="panel">
                <h2>生产实时 (production.realtime)</h2>
                <div v-if="Object.keys(production).length === 0" class="empty">等待数据推送</div>
                <pre v-else>{{ JSON.stringify(production, null, 2) }}</pre>
            </section>
            <section class="panel">
                <h2>设备状态 (device.status)</h2>
                <div v-if="Object.keys(devices).length === 0" class="empty">等待数据推送</div>
                <ul v-else>
                    <li v-for="(d, id) in devices" :key="id">{{ id }}: {{ JSON.stringify(d) }}</li>
                </ul>
            </section>
            <section class="panel">
                <h2>告警 (alert)</h2>
                <div v-if="alerts.length === 0" class="empty">暂无告警</div>
                <ul v-else class="alert-list">
                    <li v-for="(a, i) in alerts" :key="i" :class="'lv-' + a.level">
                        <span class="ts">{{ a.ts }}</span> [{{ a.device_code }}] {{ a.message }}
                    </li>
                </ul>
            </section>
        </main>
    </div>
</template>

<style scoped>
.dashboard {
    height: 100%;
    display: flex;
    flex-direction: column;
    color: #dbe6ff;
    font-family: "Microsoft YaHei", sans-serif;
}
.dash-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 12px 24px;
    background: #101a30;
}
.dash-header h1 {
    font-size: 20px;
    margin: 0;
}
.status {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 13px;
}
.dot {
    width: 10px;
    height: 10px;
    border-radius: 50%;
    display: inline-block;
}
.dot.on {
    background: #22c55e;
}
.dot.off {
    background: #ef4444;
}
.degraded {
    color: #fbbf24;
    font-weight: 600;
}
.ts {
    color: #7c8db5;
}
.dash-grid {
    flex: 1;
    display: grid;
    grid-template-columns: 1.2fr 1fr 1fr;
    gap: 12px;
    padding: 12px;
    overflow: hidden;
}
.panel {
    background: #101a30;
    border-radius: 8px;
    padding: 12px 16px;
    overflow: auto;
}
.panel h2 {
    font-size: 15px;
    margin: 0 0 10px;
    color: #8fb4ff;
}
.empty {
    color: #5b6b8f;
    padding: 24px 0;
    text-align: center;
}
pre {
    font-size: 12px;
    white-space: pre-wrap;
}
ul {
    margin: 0;
    padding-left: 16px;
    font-size: 13px;
}
.alert-list .lv-critical {
    color: #f87171;
}
.alert-list .lv-warning {
    color: #fbbf24;
}
</style>
