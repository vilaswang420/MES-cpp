<!-- 大屏根组件 (计划任务 21): 三频道布局 = 生产实时 / 设备状态 / 告警。
     WS 链路 (WsBroadcastManager) 在 M2 交付前, 页面保持空态 + 连接状态指示。 -->
<script setup lang="ts">
import { reactive, ref } from "vue";
import { useChannel } from "./composables/useChannel";

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
</script>

<template>
    <div class="dashboard">
        <header class="dash-header">
            <h1>HMS 生产实时看板</h1>
            <div class="status">
                <span :class="['dot', p1.connected.value ? 'on' : 'off']"></span>
                <span>{{ p1.connected.value ? "实时连接正常" : "连接断开, 重连中..." }}</span>
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
