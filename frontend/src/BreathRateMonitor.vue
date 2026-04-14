<template>
  <div class="monitor-card">
    <div class="monitor-header">
      <span class="monitor-title">呼吸监测</span>
      <span class="monitor-subtitle">实时呼吸频率</span>
    </div>
    <div class="data-row">
      <div class="value-group">
        <span class="value" :style="{ color: statusColor }">{{ displayRate }}</span>
        <span class="unit">RPM</span>
      </div>
      <div class="status-badge" :class="statusClass">{{ statusText }}</div>
    </div>
    <div class="scale-box">
      <div class="scale-track">
        <div class="scale-bar" :style="{ width: `${barWidth}%`, backgroundColor: statusColor }"></div>
      </div>
      <div class="scale-labels">
        <span>0</span>
        <span>20</span>
        <span>40</span>
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
  if (props.rate === 0 || props.rate < 10) return "偏慢";
  if (props.rate > 24) return "急促";
  return "正常";
});
const statusColor = computed(() => {
  if (!props.isPresent) return "#9ca3af";
  if (props.rate === 0 || props.rate < 10 || props.rate > 24) return "#0ea5e9";
  return "#2563eb";
});
const statusClass = computed(() => {
  if (!props.isPresent) return "status-offline";
  if (props.rate === 0 || props.rate < 10 || props.rate > 24) return "status-warn";
  return "status-ok";
});
const barWidth = computed(() => {
  if (!props.isPresent || props.rate <= 0) return 0;
  return Math.max(5, Math.min((props.rate / 40) * 100, 100));
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
  color: #1d4ed8;
  background: #dbeafe;
}

.status-warn {
  color: #0369a1;
  background: #e0f2fe;
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
