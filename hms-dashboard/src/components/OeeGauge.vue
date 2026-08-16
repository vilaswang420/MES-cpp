<!-- OEE 仪表盘占位: 当前显示 yield_rate (合格率) 作为 OEE 替代。
     5.4 真 OEE 消费者落地后, 替换为 A×P×Q 三因子计算结果。 -->
<script setup lang="ts">
import { computed, ref } from "vue";
import { useChart, type EChartsOption } from "../composables/useChart";

const props = defineProps<{
    yieldRate: number;
}>();

const chartEl = ref<HTMLElement | null>(null);

const option = computed<EChartsOption>(() => {
    const val = Number(props.yieldRate.toFixed(1));
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
                data: [{ value: val, name: "OEE (yield)" }],
            },
        ],
    };
});

useChart(chartEl, option);
</script>

<template>
    <div class="gauge-container">
        <div ref="chartEl" class="gauge-el"></div>
    </div>
</template>

<style scoped>
.gauge-container {
    height: 100%;
    min-height: 200px;
    display: flex;
    align-items: center;
    justify-content: center;
}
.gauge-el {
    width: 100%;
    height: 100%;
    min-height: 200px;
}
</style>
