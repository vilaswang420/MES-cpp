<!-- 质量趋势折线图: 合格率 + 缺陷数双 Y 轴时间序列 (REST /quality/statistics 30s 轮询) -->
<script setup lang="ts">
import { computed, ref, onMounted, onUnmounted } from "vue";
import { useChart, type EChartsOption } from "../composables/useChart";
import { acquireToken } from "../composables/useChannel";

interface TrendItem {
    day: string;
    total: number;
    pass_cnt: number;
    defect_qty: number;
}

const chartEl = ref<HTMLElement | null>(null);

const trendData = ref<TrendItem[]>([]);
const pollTimer = ref<ReturnType<typeof setInterval> | undefined>(undefined);

async function fetchTrend() {
    const token = await acquireToken();
    if (!token) return;
    try {
        const r = await fetch("/api/v1/quality/statistics", {
            headers: { Authorization: `Bearer ${token}` },
        });
        const j = await r.json();
        if (j.code === 200 && j.data?.daily_trend) {
            trendData.value = j.data.daily_trend as TrendItem[];
        }
    } catch {
        // 静默失败, 下一轮重试
    }
}

onMounted(() => {
    void fetchTrend();
    pollTimer.value = setInterval(() => void fetchTrend(), 30000);
});

onUnmounted(() => {
    if (pollTimer.value) clearInterval(pollTimer.value);
});

const option = computed<EChartsOption>(() => {
    const days = trendData.value.map((t) => t.day);
    const passRates = trendData.value.map((t) =>
        t.total > 0 ? Number(((t.pass_cnt / t.total) * 100).toFixed(1)) : 0,
    );
    const defects = trendData.value.map((t) => t.defect_qty);

    return {
        backgroundColor: "transparent",
        tooltip: {
            trigger: "axis",
            axisPointer: { type: "cross" },
        },
        legend: {
            data: ["合格率 (%)", "缺陷数"],
            textStyle: { color: "#8fb4ff" },
            top: 0,
        },
        grid: { left: 50, right: 50, top: 40, bottom: 30 },
        xAxis: {
            type: "category",
            data: days,
            axisLabel: { color: "#7c8db5", fontSize: 10 },
            axisLine: { lineStyle: { color: "#1a3a5c" } },
        },
        yAxis: [
            {
                type: "value",
                name: "合格率%",
                min: 0,
                max: 100,
                axisLabel: { color: "#7c8db5", fontSize: 10 },
                splitLine: { lineStyle: { color: "#1a2a3e" } },
            },
            {
                type: "value",
                name: "缺陷数",
                axisLabel: { color: "#7c8db5", fontSize: 10 },
                splitLine: { show: false },
            },
        ],
        series: [
            {
                name: "合格率 (%)",
                type: "line",
                smooth: true,
                data: passRates,
                itemStyle: { color: "#67c23a" },
                lineStyle: { width: 2 },
                areaStyle: {
                    color: {
                        type: "linear",
                        x: 0, y: 0, x2: 0, y2: 1,
                        colorStops: [
                            { offset: 0, color: "rgba(103,194,58,0.3)" },
                            { offset: 1, color: "rgba(103,194,58,0)" },
                        ],
                    },
                },
            },
            {
                name: "缺陷数",
                type: "bar",
                yAxisIndex: 1,
                data: defects,
                itemStyle: { color: "rgba(245,108,108,0.6)" },
                barWidth: "40%",
            },
        ],
    };
});

useChart(chartEl, option);
</script>

<template>
    <div class="chart-container">
        <div ref="chartEl" class="chart-el"></div>
        <div v-if="trendData.length === 0" class="chart-empty">加载质量趋势数据...</div>
    </div>
</template>

<style scoped>
.chart-container {
    position: relative;
    height: 100%;
    min-height: 200px;
}
.chart-el {
    width: 100%;
    height: 100%;
    min-height: 200px;
}
.chart-empty {
    position: absolute;
    top: 50%;
    left: 50%;
    transform: translate(-50%, -50%);
    color: #5b6b8f;
    font-size: 13px;
}
</style>
