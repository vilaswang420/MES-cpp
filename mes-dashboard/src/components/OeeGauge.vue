<!-- OEE 仪表盘: 5.4 真 OEE 消费者落地后, 优先展示 A×P×Q 三因子结果 (prod_oee_stats)。
     后端未计算出 OEE (null) 时回退显示 yield_rate (合格率), 并标注 "(yield)"。 -->
<script setup lang="ts">
import { computed, ref } from "vue";
import { useChart, type EChartsOption } from "../composables/useChart";

const props = defineProps<{
    yieldRate: number;
    oee?: number | null;
    availability?: number | null;
    performance?: number | null;
    quality?: number | null;
}>();

const isRealOee = computed(
    () => typeof props.oee === "number" && Number.isFinite(props.oee),
);

const chartEl = ref<HTMLElement | null>(null);

const option = computed<EChartsOption>(() => {
    const raw = isRealOee.value ? (props.oee as number) : props.yieldRate;
    const val = Number(Math.min(100, Math.max(0, raw)).toFixed(1));
    return {
        backgroundColor: "transparent",
        series: [
            {
                type: "gauge",
                startAngle: 200,
                endAngle: -20,
                min: 0,
                max: 100,
                radius: "90%",
                center: ["50%", "55%"],
                progress: {
                    show: true,
                    width: 14,
                    itemStyle: {
                        color: val >= 95 ? "#67c23a" : val >= 85 ? "#e6a23c" : "#f56c6c",
                    },
                },
                pointer: {
                    length: "60%",
                    width: 4,
                    itemStyle: { color: "#8fb4ff" },
                },
                axisLine: {
                    lineStyle: {
                        width: 14,
                        color: [[1, "#1a2a3e"]],
                    },
                },
                axisTick: { show: false },
                splitLine: {
                    length: 8,
                    distance: 4,
                    lineStyle: { color: "#3a4a6e" },
                },
                axisLabel: {
                    color: "#5b6b8f",
                    fontSize: 9,
                    distance: 10,
                },
                anchor: {
                    show: true,
                    size: 10,
                    itemStyle: { color: "#8fb4ff" },
                },
                title: {
                    show: true,
                    offsetCenter: [0, "70%"],
                    color: "#7c8db5",
                    fontSize: 12,
                },
                detail: {
                    valueAnimation: true,
                    offsetCenter: [0, "25%"],
                    formatter: "{value}%",
                    color: "#dbe6ff",
                    fontSize: 28,
                    fontWeight: 700,
                },
                data: [
                    {
                        value: val,
                        name: isRealOee.value ? "OEE" : "OEE (yield)",
                    },
                ],
            },
        ],
    };
});

useChart(chartEl, option);

const fmtPct = (v?: number | null) =>
    typeof v === "number" && Number.isFinite(v) ? `${v.toFixed(1)}%` : "—";
</script>

<template>
    <div class="gauge-container">
        <div ref="chartEl" class="gauge-el"></div>
        <!-- 三因子明细: A 时间稼动率 / P 性能稼动率 / Q 质量指数 (真 OEE 时展示) -->
        <div v-if="isRealOee" class="factors">
            <div class="factor">
                <span class="f-label">A</span>
                <span class="f-val">{{ fmtPct(availability) }}</span>
            </div>
            <div class="factor">
                <span class="f-label">P</span>
                <span class="f-val">{{ fmtPct(performance) }}</span>
            </div>
            <div class="factor">
                <span class="f-label">Q</span>
                <span class="f-val">{{ fmtPct(quality) }}</span>
            </div>
        </div>
    </div>
</template>

<style scoped>
.gauge-container {
    height: 100%;
    min-height: 200px;
    display: flex;
    align-items: center;
    justify-content: center;
    position: relative;
}
.gauge-el {
    width: 100%;
    height: 100%;
    min-height: 200px;
}
.factors {
    position: absolute;
    bottom: 2px;
    left: 0;
    right: 0;
    display: flex;
    justify-content: center;
    gap: 18px;
    font-size: 11px;
}
.factor {
    display: flex;
    align-items: baseline;
    gap: 4px;
}
.f-label {
    color: #7c8db5;
    font-weight: 600;
}
.f-val {
    color: #dbe6ff;
}
</style>
