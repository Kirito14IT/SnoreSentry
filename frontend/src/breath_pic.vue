<template>
  <div class="page-shell">
    <div class="toolbar">
      <el-button type="primary" @click="toggleMonitor">{{ monitoring ? "停止监测" : "开始监测" }}</el-button>
    </div>
    <div class="content-grid">
      <div ref="chartRef" class="chart-box"></div>
      <BreathRateMonitor :rate="breathRate" />
    </div>
  </div>
</template>

<script setup>
import { onBeforeUnmount, onMounted, ref } from "vue";
import * as echarts from "echarts";

import { fetchRadarDetailed } from "./request.js";
import BreathRateMonitor from "./BreathRateMonitor.vue";

const monitoring = ref(false);
const breathRate = ref(0);
const chartRef = ref(null);

let chart = null;
let timer = null;

const option = {
  title: { text: "实时呼吸波形 (相位)" },
  tooltip: { trigger: "axis" },
  xAxis: { type: "category", show: false },
  yAxis: { type: "value", min: -2, max: 2 },
  series: [
    {
      name: "呼吸波形",
      type: "line",
      smooth: true,
      showSymbol: false,
      data: [],
      lineStyle: { color: "#2563eb", width: 3 },
      areaStyle: { color: "rgba(37, 99, 235, 0.1)" },
    },
  ],
};

async function loadData() {
  const payload = await fetchRadarDetailed({ silentError: true });
  breathRate.value = payload.breath_rate || 0;
  const points = Array.isArray(payload.phase_values) ? payload.phase_values : [];
  option.xAxis.data = Array.from({ length: points.length }, (_, index) => index);
  option.series[0].data = points;
  chart?.setOption(option);
}

function toggleMonitor() {
  if (monitoring.value) {
    clearInterval(timer);
    timer = null;
    monitoring.value = false;
    return;
  }
  loadData();
  timer = setInterval(loadData, 1000);
  monitoring.value = true;
}

function handleResize() {
  chart?.resize();
}

onMounted(() => {
  if (chartRef.value) {
    chart = echarts.init(chartRef.value);
    chart.setOption(option);
    window.addEventListener("resize", handleResize);
  }
});

onBeforeUnmount(() => {
  clearInterval(timer);
  window.removeEventListener("resize", handleResize);
  chart?.dispose();
  chart = null;
});
</script>

<style scoped>
.page-shell {
  display: flex;
  flex-direction: column;
  gap: 18px;
}

.toolbar {
  display: flex;
  justify-content: flex-start;
}

.content-grid {
  display: grid;
  grid-template-columns: minmax(0, 2fr) minmax(320px, 1fr);
  gap: 20px;
}

.chart-box {
  min-height: 420px;
  border-radius: 24px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.08);
}

@media (max-width: 960px) {
  .content-grid {
    grid-template-columns: 1fr;
  }
}
</style>
