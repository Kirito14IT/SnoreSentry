<template>
  <div class="monitor-card">
    <div class="monitor-header">
      <span class="monitor-title">心率监测</span>
      <span class="monitor-subtitle">实时心率数据</span>
    </div>
    <div class="data-row">
      <div class="value-group">
        <span class="value" :style="{ color: statusColor }">{{ displayRate }}</span>
        <span class="unit">BPM</span>
      </div>
      <div class="status-badge" :class="statusClass">{{ statusText }}</div>
    </div>
    <div class="scale-box">
      <div class="scale-track">
        <div class="scale-bar" :style="{ width: `${barWidth}%`, backgroundColor: statusColor }"></div>
      </div>
      <div class="scale-labels">
        <span>60</span>
        <span>80</span>
        <span>100</span>
        <span>120</span>
      </div>
    </div>
  </div>
</template>

<script setup>
import { computed } from "vue";

const props = defineProps({
  rate: { type: Number, default: 0 },
  isPresent: { type: Boolean, default: true },
});

const displayRate = computed(() => (props.isPresent ? props.rate.toFixed(2) : "--"));
const statusText = computed(() => {
  if (!props.isPresent) return "未检测到生命体征";
  if (props.rate === 0 || props.rate < 60) return "异常偏慢";
  if (props.rate < 100) return "正常";
  return "过快";
});
const statusColor = computed(() => {
  if (!props.isPresent) return "#9ca3af";
  if (props.rate === 0 || props.rate < 60) return "#f59e0b";
  if (props.rate < 100) return "#16a34a";
  return "#ef4444";
});
const statusClass = computed(() => {
  if (!props.isPresent) return "status-offline";
  if (props.rate === 0 || props.rate < 60) return "status-warn";
  if (props.rate < 100) return "status-ok";
  return "status-danger";
});
const barWidth = computed(() => {
  if (!props.isPresent || props.rate <= 0) return 0;
  return Math.max(5, Math.min(((props.rate - 40) / 100) * 100, 100));
});
</script>

<style scoped>
.monitor-card {
  padding: 20px;
  border-radius: 20px;
  background: linear-gradient(180deg, #ffffff 0%, #f8fbff 100%);
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.08);
}

.monitor-header {
  display: flex;
  flex-direction: column;
  gap: 6px;
  margin-bottom: 18px;
}

.monitor-title {
  font-size: 18px;
  font-weight: 700;
  color: #0f172a;
}

.monitor-subtitle {
  font-size: 13px;
  color: #64748b;
}

.data-row {
  display: flex;
  align-items: center;
  justify-content: space-between;
  gap: 16px;
}

.value-group {
  display: flex;
  align-items: baseline;
  gap: 8px;
}

.value {
  font-size: 42px;
  font-weight: 800;
}

.unit {
  font-size: 14px;
  color: #64748b;
}

.status-badge {
  padding: 8px 12px;
  border-radius: 999px;
  font-size: 13px;
  font-weight: 600;
}

.status-ok {
  color: #15803d;
  background: #dcfce7;
}

.status-warn {
  color: #b45309;
  background: #fef3c7;
}

.status-danger {
  color: #b91c1c;
  background: #fee2e2;
}

.status-offline {
  color: #475569;
  background: #e2e8f0;
}

.scale-box {
  margin-top: 18px;
}

.scale-track {
  overflow: hidden;
  height: 10px;
  border-radius: 999px;
  background: #e2e8f0;
}

.scale-bar {
  height: 100%;
  border-radius: 999px;
  transition: width 0.25s ease;
}

.scale-labels {
  display: flex;
  justify-content: space-between;
  margin-top: 10px;
  font-size: 12px;
  color: #94a3b8;
}
</style>
