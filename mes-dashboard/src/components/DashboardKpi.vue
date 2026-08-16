<!-- 顶部 KPI 行: 在制工单数 / 完工率 / 合格率 / 活跃告警数 -->
<script setup lang="ts">
import { computed } from "vue";

interface ProductionData {
    work_order_no: string;
    product_name: string;
    line_name: string;
    target_qty: number;
    completed_qty: number;
    good_qty: number;
    defect_qty: number;
    yield_rate: number;
    status: number;
}

const props = defineProps<{
    production: ProductionData | null;
    alertCount: number;
    degraded: boolean;
    lastTs: string;
}>();

const kpis = computed(() => {
    const p = props.production;
    return [
        {
            label: "在制工单",
            value: p?.work_order_no ?? "—",
            sub: p?.product_name ?? "",
            color: "#409eff",
        },
        {
            label: "完工进度",
            value: p
                ? `${p.completed_qty} / ${p.target_qty}`
                : "— / —",
            sub: p ? `${((p.completed_qty / Math.max(p.target_qty, 1)) * 100).toFixed(1)}%` : "",
            color: "#67c23a",
        },
        {
            label: "合格率",
            value: p ? `${p.yield_rate.toFixed(1)}%` : "—",
            sub: p ? `合格 ${p.good_qty} · 缺陷 ${p.defect_qty}` : "",
            color: p && p.yield_rate >= 95 ? "#67c23a" : "#e6a23c",
        },
        {
            label: "活跃告警",
            value: String(props.alertCount),
            sub: props.alertCount > 0 ? "需关注" : "正常",
            color: props.alertCount > 0 ? "#f56c6c" : "#67c23a",
        },
    ];
});
</script>

<template>
    <div class="kpi-row">
        <div v-for="k in kpis" :key="k.label" class="kpi-card">
            <div class="kpi-label">{{ k.label }}</div>
            <div class="kpi-value" :style="{ color: k.color }">{{ k.value }}</div>
            <div class="kpi-sub">{{ k.sub }}</div>
        </div>
    </div>
</template>

<style scoped>
.kpi-row {
    display: flex;
    gap: 12px;
    padding: 0 12px;
}
.kpi-card {
    flex: 1;
    background: linear-gradient(135deg, #0d1b2a 0%, #13293d 100%);
    border: 1px solid #1a3a5c;
    border-radius: 8px;
    padding: 14px 18px;
    min-height: 76px;
}
.kpi-label {
    font-size: 12px;
    color: #7c8db5;
    margin-bottom: 4px;
}
.kpi-value {
    font-size: 24px;
    font-weight: 700;
    line-height: 1.2;
}
.kpi-sub {
    font-size: 11px;
    color: #5b6b8f;
    margin-top: 4px;
}
</style>
