<template>
  <div class="dashboard-container">
    <div class="control-grid">
      <section class="control-card">
        <div class="control-title">雷达生命体征</div>
        <div class="control-text">保持现有雷达原型逻辑，独立控制实时轮询和生命体征保存。</div>
        <div class="control-actions">
          <el-button :type="radarMonitoring ? 'danger' : 'primary'" size="large" @click="toggleRadarMonitor">
            {{ radarMonitoring ? "停止雷达监测" : "开始雷达监测" }}
          </el-button>
          <span class="meta-text">最近测量: {{ formatTime(radarMeta.lastMeasurementAtMs) }}</span>
        </div>
      </section>

      <section class="control-card snore-card">
        <div class="control-title">呼噜检测</div>
        <div class="control-text">选择在线开发板后可独立启动呼噜检测，控制权按设备单用户独占。</div>

        <SnoreDeviceSelector
          v-model="snoreState.selectedDeviceId"
          :devices="snoreState.devices"
          :loading="snoreState.loadingDevices"
          @refresh="loadSnoreDevices"
        />

        <div class="control-actions snore-actions">
          <el-button
            :type="snoreState.detecting ? 'danger' : 'warning'"
            size="large"
            :loading="snoreState.controlBusy"
            :disabled="snoreButtonDisabled"
            @click="toggleSnoreDetect"
          >
            {{ snoreState.detecting ? "停止呼噜检测" : "开始呼噜检测" }}
          </el-button>
          <el-tag :type="snoreState.deviceOnline ? 'success' : 'info'">
            {{ snoreState.deviceOnline ? "设备在线" : "设备离线" }}
          </el-tag>
          <el-tag :type="snoreState.detecting ? 'danger' : 'warning'">
            {{ snoreState.detecting ? "检测中" : "已停止" }}
          </el-tag>
          <el-tag v-if="snoreState.controllerUserId && snoreState.controllerUserId !== userID" type="danger">
            设备被其他用户占用
          </el-tag>
          <el-tag v-else-if="snoreState.controllerUserId === userID && snoreState.deviceOnline" type="success">
            当前由你控制
          </el-tag>
        </div>

        <div class="snore-summary">
          <div class="summary-item">
            <span class="summary-label">当前设备</span>
            <span class="summary-value">{{ snoreState.deviceName || snoreState.selectedDeviceId || "未选择设备" }}</span>
          </div>
          <div class="summary-item">
            <span class="summary-label">最新分数</span>
            <span class="summary-value score">{{ formatScore(snoreState.latestScore) }}</span>
          </div>
          <div class="summary-item">
            <span class="summary-label">最近事件</span>
            <span class="summary-value">{{ formatTime(snoreState.lastEventAtMs) }}</span>
          </div>
        </div>
      </section>
    </div>

    <div class="charts-layout">
      <section class="module-card">
        <div class="chart-header">
          <span class="dot red"></span>
          <span>心率趋势 (Heart Rate)</span>
        </div>
        <div ref="heartChartRef" class="chart-box"></div>
        <div class="monitor-box">
          <HeartRateMonitor :rate="heartRate" :is-present="isPresent" />
        </div>
      </section>

      <section class="module-card">
        <div class="chart-header">
          <span class="dot blue"></span>
          <span>呼吸趋势 (Breath Rate)</span>
        </div>
        <div ref="breathChartRef" class="chart-box"></div>
        <div class="monitor-box">
          <BreathRateMonitor :rate="breathRate" :is-present="isPresent" />
        </div>
      </section>
    </div>

    <section class="module-card snore-trend-card">
      <div class="chart-header">
        <span class="dot amber"></span>
        <span>呼噜得分趋势 (Snore Score)</span>
      </div>
      <SnoreScoreChart :points="snoreState.trendPoints" />
    </section>
  </div>
</template>

<script setup>
import { computed, onBeforeUnmount, onMounted, reactive, ref, watch } from "vue";
import { ElMessage } from "element-plus";
import * as echarts from "echarts";

import HeartRateMonitor from "./HeartRateMonitor.vue";
import BreathRateMonitor from "./BreathRateMonitor.vue";
import SnoreDeviceSelector from "./SnoreDeviceSelector.vue";
import SnoreScoreChart from "./SnoreScoreChart.vue";
import { useUserStore } from "./userStore.js";
import { fetchRadarTarget, saveVitalsWithUser } from "./request.js";
import {
  fetchOnlineSnoreDevices,
  fetchSnoreLive,
  fetchSnoreViewerDevice,
  selectSnoreViewerDevice,
  startSnoreDetect,
  stopSnoreDetect,
} from "./snoreRequest.js";

const userStore = useUserStore();
const userID = computed(() => userStore.userInfo?.userID ?? null);

const radarMonitoring = ref(false);
const heartRate = ref(0);
const breathRate = ref(0);
const isPresent = ref(true);
const radarMeta = reactive({
  lastMeasurementAtMs: null,
});

const heartChartRef = ref(null);
const breathChartRef = ref(null);
const heartSeries = reactive([]);
const breathSeries = reactive([]);

const snoreState = reactive({
  devices: [],
  loadingDevices: false,
  selectedDeviceId: "",
  deviceName: "",
  deviceOnline: false,
  detecting: false,
  latestScore: null,
  latestWindowEndMs: null,
  lastEventAtMs: null,
  controllerUserId: null,
  canControl: false,
  controlBusy: false,
  trendPoints: [],
});

let heartChart = null;
let breathChart = null;
let radarTimer = null;
let snoreTimer = null;
let deviceTimer = null;

const snoreButtonDisabled = computed(() => {
  if (!userID.value) return true;
  if (snoreState.controlBusy) return true;
  if (!snoreState.selectedDeviceId) return true;
  if (!snoreState.deviceOnline && !snoreState.detecting) return true;
  if (!snoreState.canControl && !snoreState.detecting) return true;
  return false;
});

function formatTime(value) {
  return value ? new Date(value).toLocaleString("zh-CN", { hour12: false }) : "--";
}

function formatScore(value) {
  return typeof value === "number" ? value.toFixed(3) : "--";
}

function buildLineOption(title, color, data) {
  const colorStops = {
    "rgb(255, 87, 87)": ["rgba(255, 87, 87, 0.32)", "rgba(255, 87, 87, 0.02)"],
    "rgb(37, 99, 235)": ["rgba(37, 99, 235, 0.28)", "rgba(37, 99, 235, 0.02)"],
  };
  return {
    title: { text: title, left: "center", top: 10 },
    grid: { top: 50, right: 20, bottom: 24, left: 40 },
    xAxis: { type: "category", show: false, boundaryGap: false, data: data.map((_, index) => index) },
    yAxis: { type: "value", scale: true, splitLine: { lineStyle: { type: "dashed" } } },
    series: [
      {
        type: "line",
        smooth: true,
        symbol: "none",
        data,
        lineStyle: { width: 3, color },
        areaStyle: {
          color: new echarts.graphic.LinearGradient(0, 0, 0, 1, [
            { offset: 0, color: colorStops[color][0] },
            { offset: 1, color: colorStops[color][1] },
          ]),
        },
      },
    ],
  };
}

function updateRadarCharts() {
  heartChart?.setOption(buildLineOption("心率趋势", "rgb(255, 87, 87)", [...heartSeries]));
  breathChart?.setOption(buildLineOption("呼吸趋势", "rgb(37, 99, 235)", [...breathSeries]));
}

async function loadRadarState() {
  try {
    const payload = await fetchRadarTarget({ silentError: true });
    const targetDistance = payload.target_distance || 0;
    const heart = payload.heart_rate;
    const breath = payload.breath_rate;
    const validHeart = typeof heart === "number" && heart > 0 && heart < 300;
    const validBreath = typeof breath === "number" && breath > 0 && breath < 60;

    isPresent.value = validHeart && targetDistance > 0.1;
    heartRate.value = validHeart ? heart : 0;
    breathRate.value = validBreath ? breath : 0;
    radarMeta.lastMeasurementAtMs = payload.measurement_at_ms || null;

    heartSeries.push(validHeart ? heart : 0);
    breathSeries.push(validBreath ? breath : 0);
    if (heartSeries.length > 30) heartSeries.shift();
    if (breathSeries.length > 30) breathSeries.shift();
    updateRadarCharts();

    if (radarMonitoring.value && userID.value && validHeart) {
      await saveVitalsWithUser(
        {
          userID: userID.value,
          heart_rate: heart,
          breath_rate: validBreath ? breath : null,
          target_distance: targetDistance,
          timestamp: new Date().toISOString().replace("T", " ").slice(0, 19),
          measurement_at_ms: payload.measurement_at_ms || null,
          response_at_ms: payload.response_at_ms || null,
        },
        { silentError: true }
      );
    }
  } catch (error) {
    console.error("获取雷达数据失败:", error);
    isPresent.value = false;
    heartRate.value = 0;
    breathRate.value = 0;
  }
}

function toggleRadarMonitor() {
  if (radarMonitoring.value) {
    clearInterval(radarTimer);
    radarTimer = null;
    radarMonitoring.value = false;
    return;
  }
  loadRadarState();
  radarTimer = setInterval(loadRadarState, 1000);
  radarMonitoring.value = true;
}

async function loadSnoreDevices() {
  snoreState.loadingDevices = true;
  try {
    const payload = await fetchOnlineSnoreDevices({ silentError: true });
    snoreState.devices = payload.data || [];
  } catch (error) {
    console.warn("加载在线呼噜设备失败:", error);
    snoreState.devices = [];
  } finally {
    snoreState.loadingDevices = false;
  }
}

async function loadSnoreViewerDevice() {
  if (!userID.value) return;
  try {
    const payload = await fetchSnoreViewerDevice(userID.value, { silentError: true });
    const data = payload.data;
    if (data?.device_id) {
      snoreState.selectedDeviceId = data.device_id;
      snoreState.deviceName = data.device_name || "";
    }
  } catch (error) {
    console.warn("加载当前查看设备失败:", error);
  }
}

async function loadSnoreLive() {
  try {
    const payload = await fetchSnoreLive(
      {
        userID: userID.value || undefined,
        deviceID: snoreState.selectedDeviceId || undefined,
      },
      { silentError: true }
    );
    const data = payload.data || {};
    snoreState.deviceName = data.device_name || snoreState.deviceName || "";
    snoreState.deviceOnline = !!data.device_online;
    snoreState.detecting = !!data.detecting;
    snoreState.latestScore = typeof data.latest_score === "number" ? data.latest_score : null;
    snoreState.latestWindowEndMs = data.latest_window_end_ms || null;
    snoreState.lastEventAtMs = data.last_event_at_ms || null;
    snoreState.controllerUserId = data.controller_user_id || null;
    snoreState.canControl = !!data.can_control;
    snoreState.trendPoints = Array.isArray(data.trend_points) ? data.trend_points : [];
    if (!snoreState.selectedDeviceId && data.device_id) {
      snoreState.selectedDeviceId = data.device_id;
    }
  } catch (error) {
    console.warn("加载呼噜实时状态失败:", error);
  }
}

async function toggleSnoreDetect() {
  if (!userID.value) {
    ElMessage.warning("请先登录后再控制设备");
    return;
  }
  if (!snoreState.selectedDeviceId) {
    ElMessage.warning("请先选择一台在线开发板");
    return;
  }

  snoreState.controlBusy = true;
  try {
    const payload = { userID: userID.value, deviceID: snoreState.selectedDeviceId };
    if (snoreState.detecting) {
      await stopSnoreDetect(payload);
      ElMessage.success("停止呼噜检测命令已下发");
    } else {
      await startSnoreDetect(payload);
      ElMessage.success("开始呼噜检测命令已下发");
    }
    await loadSnoreLive();
    await loadSnoreDevices();
  } finally {
    snoreState.controlBusy = false;
  }
}

function handleResize() {
  heartChart?.resize();
  breathChart?.resize();
}

watch(
  () => snoreState.selectedDeviceId,
  async (value, previous) => {
    if (!value || !userID.value || value === previous) {
      return;
    }
    try {
      await selectSnoreViewerDevice({ userID: userID.value, deviceID: value }, { silentError: true });
      const device = snoreState.devices.find((item) => item.device_id === value);
      snoreState.deviceName = device?.device_name || "";
      await loadSnoreLive();
    } catch (error) {
      console.error("更新查看设备失败:", error);
    }
  }
);

onMounted(async () => {
  if (heartChartRef.value) {
    heartChart = echarts.init(heartChartRef.value);
    heartChart.setOption(buildLineOption("心率趋势", "rgb(255, 87, 87)", []));
  }
  if (breathChartRef.value) {
    breathChart = echarts.init(breathChartRef.value);
    breathChart.setOption(buildLineOption("呼吸趋势", "rgb(37, 99, 235)", []));
  }
  window.addEventListener("resize", handleResize);

  await loadSnoreDevices();
  await loadSnoreViewerDevice();
  await loadSnoreLive();

  snoreTimer = setInterval(loadSnoreLive, 1000);
  deviceTimer = setInterval(loadSnoreDevices, 5000);
});

onBeforeUnmount(() => {
  clearInterval(radarTimer);
  clearInterval(snoreTimer);
  clearInterval(deviceTimer);
  window.removeEventListener("resize", handleResize);
  heartChart?.dispose();
  breathChart?.dispose();
  heartChart = null;
  breathChart = null;
});
</script>

<style scoped>
.dashboard-container {
  display: flex;
  flex-direction: column;
  gap: 20px;
}

.control-grid {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 20px;
}

.control-card,
.module-card {
  padding: 22px;
  border-radius: 24px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.08);
}

.control-title {
  margin-bottom: 10px;
  font-size: 20px;
  font-weight: 700;
  color: #0f172a;
}

.control-text {
  margin-bottom: 18px;
  line-height: 1.7;
  color: #64748b;
}

.control-actions {
  display: flex;
  align-items: center;
  flex-wrap: wrap;
  gap: 14px;
}

.snore-actions {
  margin-top: 16px;
}

.meta-text {
  font-size: 13px;
  color: #64748b;
}

.snore-summary {
  display: grid;
  grid-template-columns: repeat(3, minmax(0, 1fr));
  gap: 12px;
  margin-top: 18px;
}

.summary-item {
  padding: 16px;
  border-radius: 18px;
  background: #f8fafc;
}

.summary-label {
  display: block;
  margin-bottom: 8px;
  font-size: 12px;
  color: #64748b;
}

.summary-value {
  display: block;
  font-size: 16px;
  font-weight: 700;
  color: #0f172a;
}

.summary-value.score {
  color: #d97706;
}

.charts-layout {
  display: grid;
  grid-template-columns: repeat(2, minmax(0, 1fr));
  gap: 20px;
}

.chart-header {
  display: flex;
  align-items: center;
  gap: 10px;
  margin-bottom: 14px;
  font-size: 16px;
  font-weight: 700;
  color: #0f172a;
}

.dot {
  display: inline-flex;
  width: 12px;
  height: 12px;
  border-radius: 999px;
}

.dot.red {
  background: #ef4444;
}

.dot.blue {
  background: #2563eb;
}

.dot.amber {
  background: #f59e0b;
}

.chart-box {
  min-height: 340px;
}

.monitor-box {
  margin-top: 18px;
}

.snore-trend-card {
  overflow: hidden;
}

@media (max-width: 1100px) {
  .control-grid,
  .charts-layout,
  .snore-summary {
    grid-template-columns: 1fr;
  }
}
</style>
