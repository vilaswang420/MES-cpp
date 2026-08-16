// ECharts composable: tree-shaking 按需引入 + 自动 init/resize/dispose。
// 构建体积增量目标 < 300KB (gzip); 仅引入 Gauge/Line/Bar + Grid/Tooltip/Title + Canvas。
import { onMounted, onUnmounted, watch, type Ref } from "vue";
import * as echarts from "echarts/core";
import { GaugeChart, LineChart, BarChart } from "echarts/charts";
import {
    GridComponent,
    TooltipComponent,
    TitleComponent,
    LegendComponent,
} from "echarts/components";
import { CanvasRenderer } from "echarts/renderers";

echarts.use([
    GaugeChart,
    LineChart,
    BarChart,
    GridComponent,
    TooltipComponent,
    TitleComponent,
    LegendComponent,
    CanvasRenderer,
]);

export type EChartsOption = echarts.EChartsCoreOption;
export type EChartsInstance = echarts.ECharts;

/**
 * ECharts 封装: 在 onMounted 时 init, watch option 时 setOption(notMerge: true),
 * 窗口 resize 时自适应, onUnmounted 时 dispose。
 *
 * @param el     挂载容器 Ref (HTMLElement | null)
 * @param option 图表配置 Ref (响应式)
 * @returns      ECharts 实例 Ref
 */
export function useChart(el: Ref<HTMLElement | null>, option: Ref<EChartsOption>) {
    let chart: EChartsInstance | null = null;

    function resize() {
        chart?.resize();
    }

    onMounted(() => {
        if (!el.value) return;
        chart = echarts.init(el.value, "dark", { renderer: "canvas" });
        chart.setOption(option.value, true);
        window.addEventListener("resize", resize);
    });

    watch(
        option,
        (newOpt) => {
            if (chart) chart.setOption(newOpt, true);
        },
        { deep: true },
    );

    onUnmounted(() => {
        window.removeEventListener("resize", resize);
        chart?.dispose();
        chart = null;
    });

    return {
        getInstance: () => chart,
    };
}
