<template>
  <div ref="chartRef" class="score-chart"></div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref, watch } from "vue";
import * as echarts from "echarts";

const props = defineProps({
  points: { type: Array, default: () => [] },
});

const chartRef = ref(null);
let chart = null;

function formatLabel(tsMs) {
  return tsMs ? new Date(tsMs).toLocaleTimeString("zh-CN", { hour12: false }) : "--";
}

function renderChart() {
  if (!chart) {
    return;
  }
  chart.setOption({
    tooltip: { trigger: "axis" },
    grid: { top: 30, right: 20, bottom: 30, left: 40 },
    xAxis: {
      type: "category",
      boundaryGap: false,
      data: props.points.map((item) => formatLabel(item.ts_ms)),
      axisLabel: { color: "#64748b" },
    },
    yAxis: {
      type: "value",
      min: 0,
      max: 1,
      axisLabel: { color: "#64748b" },
      splitLine: { lineStyle: { type: "dashed", color: "#dbeafe" } },
    },
    series: [
      {
        type: "line",
        smooth: true,
        symbol: "circle",
        symbolSize: 7,
        data: props.points.map((item) => (typeof item.score === "number" ? item.score : 0)),
        lineStyle: { width: 3, color: "#f59e0b" },
        itemStyle: { color: "#d97706" },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: "rgba(245, 158, 11, 0.35)" },
            { offset: 1, color: "rgba(245, 158, 11, 0.03)" },
          ]),
        },
      },
    ],
  });
}

function handleResize() {
  chart?.resize();
}

onMounted(() => {
  if (chartRef.value) {
    chart = echarts.init(chartRef.value);
    renderChart();
    window.addEventListener("resize", handleResize);
  }
});

watch(
  () => props.points,
  () => renderChart(),
  { deep: true }
);

onBeforeUnmount(() => {
  window.removeEventListener("resize", handleResize);
  if (chart) {
    chart.dispose();
    chart = null;
  }
});
</script>

<style scoped>
.score-chart {
  width: 100%;
  height: 320px;
}
</style>
