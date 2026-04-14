<template>
  <div class="selector-shell">
    <el-select
      class="selector-input"
      :model-value="modelValue"
      clearable
      filterable
      placeholder="选择在线开发板"
      @change="emit('update:modelValue', $event)"
    >
      <el-option
        v-for="device in devices"
        :key="device.device_id"
        :label="deviceLabel(device)"
        :value="device.device_id"
      >
        <div class="option-row">
          <span class="device-name">{{ device.device_name || "未命名设备" }}</span>
          <span class="device-id">{{ device.device_id }}</span>
        </div>
      </el-option>
    </el-select>
    <el-button type="primary" text :loading="loading" @click="emit('refresh')">刷新设备</el-button>
  </div>
</template>

<script setup>
const props = defineProps({
  modelValue: { type: String, default: "" },
  devices: { type: Array, default: () => [] },
  loading: { type: Boolean, default: false },
});

const emit = defineEmits(["update:modelValue", "refresh"]);

function deviceLabel(device) {
  return `${device.device_name || "未命名设备"} · ${device.device_id}`;
}
</script>

<style scoped>
.selector-shell {
  display: flex;
  gap: 12px;
  align-items: center;
}

.selector-input {
  flex: 1;
}

.option-row {
  display: flex;
  justify-content: space-between;
  gap: 16px;
}

.device-name {
  color: #0f172a;
  font-weight: 600;
}

.device-id {
  color: #64748b;
  font-size: 12px;
}
</style>
