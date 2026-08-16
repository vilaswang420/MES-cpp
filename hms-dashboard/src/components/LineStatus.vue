<!-- 产线状态网格: 各产线实时工单 + 进度条 + 设备状态指示灯 -->
<script setup lang="ts">
import { computed } from "vue";

interface ProductionData {
    line_id: number;
    line_name: string;
    work_order_no: string;
    product_name: string;
    target_qty: number;
    completed_qty: number;
    good_qty: number;
    defect_qty: number;
    yield_rate: number;
    status: number;
    timestamp: string;
}

interface DeviceStatus {
    device_id: number;
    device_code: string;
    value: number | string;
    quality: number;
    ts: string;
}

const props = defineProps<{
    lines: ProductionData[];
    devices: Record<string, DeviceStatus>;
}>();

// 按 line_id 聚合 (WS 1Hz 推 20 条在制工单, 同产线取最新一条)
const lineList = computed(() => {
    const map = new Map<number, ProductionData>();
    for (const p of props.lines) {
        if (p.line_id > 0 && !map.has(p.line_id)) {
            map.set(p.line_id, p);
        }
    }
    return Array.from(map.values()).slice(0, 12);
});

function progress(p: ProductionData): number {
    return Math.min(100, (p.completed_qty / Math.max(p.target_qty, 1)) * 100);
}

function yieldColor(rate: number): string {
    if (rate >= 98) return "#67c23a";
    if (rate >= 90) return "#e6a23c";
    return "#f56c6c";
}
</script>

<template>
    <div class="line-grid">
        <div v-if="lineList.length === 0" class="empty">等待产线数据推送</div>
        <div v-for="l in lineList" :key="l.line_id" class="line-card">
            <div class="line-header">
                <span class="line-name">{{ l.line_name || `产线 ${l.line_id}` }}</span>
                <span class="wo-no">{{ l.work_order_no }}</span>
            </div>
            <div class="line-product">{{ l.product_name || "—" }}</div>
            <div class="progress-bar">
                <div class="progress-fill" :style="{ width: progress(l) + '%' }"></div>
            </div>
            <div class="line-stats">
                <span>完工 {{ l.completed_qty }}/{{ l.target_qty }}</span>
                <span :style="{ color: yieldColor(l.yield_rate) }">
                    合格率 {{ l.yield_rate.toFixed(1) }}%
                </span>
            </div>
        </div>
    </div>
</template>

<style scoped>
.line-grid {
    display: grid;
    grid-template-columns: repeat(auto-fill, minmax(160px, 1fr));
    gap: 8px;
    padding: 8px;
}
.empty {
    color: #5b6b8f;
    text-align: center;
    padding: 40px 0;
    grid-column: 1 / -1;
}
.line-card {
    background: #0d1b2a;
    border: 1px solid #1a3a5c;
    border-radius: 6px;
    padding: 10px 12px;
}
.line-header {
    display: flex;
    justify-content: space-between;
    align-items: center;
    margin-bottom: 4px;
}
.line-name {
    font-size: 13px;
    font-weight: 600;
    color: #8fb4ff;
}
.wo-no {
    font-size: 11px;
    color: #5b6b8f;
}
.line-product {
    font-size: 11px;
    color: #7c8db5;
    margin-bottom: 8px;
    white-space: nowrap;
    overflow: hidden;
    text-overflow: ellipsis;
}
.progress-bar {
    height: 6px;
    background: #1a2a3e;
    border-radius: 3px;
    overflow: hidden;
    margin-bottom: 6px;
}
.progress-fill {
    height: 100%;
    background: linear-gradient(90deg, #409eff, #67c23a);
    border-radius: 3px;
    transition: width 0.5s ease;
}
.line-stats {
    display: flex;
    justify-content: space-between;
    font-size: 11px;
    color: #7c8db5;
}
</style>
