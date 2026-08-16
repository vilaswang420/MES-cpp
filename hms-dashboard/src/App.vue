<!-- 大屏根组件 (P4-5.3): ECharts 可视化布局, 替换 JSON dump。
     布局: KPI 行 + 2x2 图表网格 (LineStatus / OeeGauge / QualityTrend / AlertTimeline)
     降级策略: WS 连续重连失败 ≥3 → REST 轮询 (10s) 历史数据。 -->
<script setup lang="ts">
import { computed, reactive, ref, watch } from "vue";
import { acquireToken, useChannel } from "./composables/useChannel";
import DashboardKpi from "./components/DashboardKpi.vue";
import LineStatus from "./components/LineStatus.vue";
import QualityTrend from "./components/QualityTrend.vue";
import AlertTimeline from "./components/AlertTimeline.vue";
import OeeGauge from "./components/OeeGauge.vue";

interface ProductionRealtime {
    line_id: number;
    line_name: string;
    work_order_no: string;
    product_name: string;
    target_qty: number;
    completed_qty: number;
    good_qty: number;
    defect_qty: number;
    yield_rate: number;
    // 5.4 真 OEE: 后端 prod_oee_stats 计算结果 (0-100), 无数据时为 null (回退 yield_rate)
    availability?: number | null;
    performance?: number | null;
    quality?: number | null;
    oee?: number | null;
    status: number;
    timestamp: string;
}

interface DeviceStatus {
    device_id: number;
    device_code: string;
    sensor_id: number;
    value: number | string;
    quality: number;
    ts: string;
}

interface AlertItem {
    ts: string;
    device_code: string;
    level: string;
    message: string;
}

// ---- WS 频道订阅 ----
const realtimeLines = ref<ProductionRealtime[]>([]);
const devices = reactive<Record<string, DeviceStatus>>({});
const alerts = ref<AlertItem[]>([]);
const lastTs = ref("");

const p1 = useChannel("production.realtime", (payload) => {
    const p = payload as unknown as ProductionRealtime;
    // 按 line_id 去重, 同产线仅保留最新一条
    const idx = realtimeLines.value.findIndex((l) => l.line_id === p.line_id);
    if (idx >= 0) {
        realtimeLines.value[idx] = p;
    } else {
        realtimeLines.value.push(p);
    }
    lastTs.value = p.timestamp;
});

useChannel("device.status", (payload) => {
    const d = payload as unknown as DeviceStatus;
    if (d.device_id) devices[String(d.device_id)] = d;
});

useChannel("alert", (payload, ts) => {
    const a = payload as unknown as {
        device_code?: string;
        level?: string;
        message?: string;
        timestamp?: string;
    };
    alerts.value.unshift({
        ts: a.timestamp ?? ts,
        device_code: String(a.device_code ?? ""),
        level: String(a.level ?? "warning"),
        message: String(a.message ?? ""),
    });
    if (alerts.value.length > 50) alerts.value.pop();
});

// ---- KPI 汇总: 取最新一条在制工单作为主 KPI ----
const currentProduction = computed<ProductionRealtime | null>(() => {
    return realtimeLines.value.length > 0 ? realtimeLines.value[0] : null;
});

const activeAlertCount = computed(() => alerts.value.length);

// ---- 降级模式: WS 不可达时轮询 REST 历史数据 ----
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
    // 生产实时降级源: 在制工单列表
    const wo = (await api("/api/v1/production/work-orders?page=1&page_size=20&status=3")) as
        { list?: Record<string, unknown>[] } | null;
    if (wo?.list?.length) {
        realtimeLines.value = wo.list.map((w) => ({
            line_id: Number(w.line_id ?? 0),
            line_name: String(w.line_name ?? ""),
            work_order_no: String(w.work_order_no ?? ""),
            product_name: String(w.product_name ?? ""),
            target_qty: Number(w.plan_qty ?? w.target_qty ?? 0),
            completed_qty: Number(w.completed_qty ?? 0),
            good_qty: Number(w.good_qty ?? 0),
            defect_qty: Number(w.defect_qty ?? 0),
            yield_rate: Number(w.plan_qty ?? 0) > 0
                ? (Number(w.good_qty ?? 0) * 100) / Number(w.plan_qty ?? 1)
                : 0,
            status: Number(w.status ?? 3),
            timestamp: new Date().toISOString(),
        }));
        lastTs.value = new Date().toISOString();
    }
    // 告警降级源: 最近告警历史
    const al = (await api("/api/v1/iot/alerts?page=1&page_size=20")) as
        { list?: Record<string, unknown>[] } | null;
    if (al?.list?.length) {
        alerts.value = al.list.map((a) => ({
            ts: String(a.created_at ?? ""),
            device_code: String(a.device_code ?? a.device_name ?? ""),
            level: Number(a.alert_level ?? 1) >= 2 ? "critical" : "warning",
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
                <span v-if="p1.degraded.value" class="degraded">降级模式 · REST 轮询 10s</span>
                <span v-else>{{ p1.connected.value ? "实时连接正常" : "连接断开, 重连中..." }}</span>
                <span v-if="lastTs" class="ts">最后更新: {{ lastTs }}</span>
            </div>
        </header>

        <DashboardKpi
            :production="currentProduction"
            :alert-count="activeAlertCount"
            :degraded="p1.degraded.value"
            :last-ts="lastTs"
        />

        <main class="dash-grid">
            <section class="panel panel-line">
                <h2>产线状态</h2>
                <LineStatus :lines="realtimeLines" :devices="devices" />
            </section>
            <section class="panel panel-oee">
                <h2>OEE 仪表盘</h2>
                <OeeGauge
                    :yield-rate="currentProduction?.yield_rate ?? 0"
                    :oee="currentProduction?.oee ?? null"
                    :availability="currentProduction?.availability ?? null"
                    :performance="currentProduction?.performance ?? null"
                    :quality="currentProduction?.quality ?? null"
                />
            </section>
            <section class="panel panel-quality">
                <h2>质量趋势 (30s 轮询)</h2>
                <QualityTrend />
            </section>
            <section class="panel panel-alert">
                <h2>告警时间线</h2>
                <AlertTimeline :alerts="alerts" />
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
    background: #0a1525;
}
.dash-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    padding: 10px 24px;
    background: #0d1b2a;
    border-bottom: 1px solid #1a3a5c;
}
.dash-header h1 {
    font-size: 18px;
    margin: 0;
    color: #8fb4ff;
}
.status {
    display: flex;
    align-items: center;
    gap: 8px;
    font-size: 12px;
}
.dot {
    width: 8px;
    height: 8px;
    border-radius: 50%;
}
.dot.on { background: #22c55e; box-shadow: 0 0 4px #22c55e; }
.dot.off { background: #ef4444; }
.degraded { color: #fbbf24; font-weight: 600; }
.ts { color: #5b6b8f; }

.dash-grid {
    flex: 1;
    display: grid;
    grid-template-columns: 1.3fr 1fr;
    grid-template-rows: 1fr 1fr;
    gap: 10px;
    padding: 10px;
    overflow: hidden;
}
.panel {
    background: #0d1b2a;
    border: 1px solid #1a3a5c;
    border-radius: 8px;
    padding: 8px 12px;
    display: flex;
    flex-direction: column;
    overflow: hidden;
}
.panel h2 {
    font-size: 13px;
    margin: 0 0 6px;
    color: #8fb4ff;
    flex-shrink: 0;
}
.panel > :deep(*) {
    flex: 1;
    overflow: auto;
}
</style>
