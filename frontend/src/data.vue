<template>
  <div class="page-shell">
    <el-tabs v-model="activeTab">
      <el-tab-pane label="生命体征历史" name="vitals">
        <section class="history-card">
          <div class="toolbar">
            <el-date-picker v-model="vitalsQuery.date" type="date" value-format="YYYY-MM-DD" placeholder="选择日期" />
            <el-button type="primary" @click="loadVitals">查询</el-button>
          </div>
          <el-table :data="vitalsRecords" stripe>
            <el-table-column prop="id" label="ID" width="80" />
            <el-table-column prop="userID" label="用户 ID" width="100" />
            <el-table-column prop="heart_rate" label="心率" />
            <el-table-column prop="breath_rate" label="呼吸率" />
            <el-table-column prop="target_distance" label="距离" />
            <el-table-column prop="timestamp" label="时间" min-width="180" />
          </el-table>
          <div class="pagination-row">
            <el-pagination
              background
              layout="total, prev, pager, next"
              :total="vitalsTotal"
              :page-size="vitalsQuery.pageSize"
              :current-page="vitalsQuery.pageNum"
              @current-change="handleVitalsPageChange"
            />
          </div>
        </section>
      </el-tab-pane>

      <el-tab-pane label="呼噜历史" name="snore">
        <section class="history-card">
          <div class="toolbar">
            <el-date-picker v-model="snoreQuery.date" type="date" value-format="YYYY-MM-DD" placeholder="选择日期" />
            <el-input v-model="snoreQuery.deviceID" placeholder="筛选设备 ID" clearable style="max-width: 220px" />
            <el-button type="primary" @click="loadSnore">查询</el-button>
          </div>
          <el-table :data="snoreRecords" stripe>
            <el-table-column prop="event_id" label="事件 ID" width="90" />
            <el-table-column prop="user_id" label="用户 ID" width="100" />
            <el-table-column prop="device_id" label="设备 ID" min-width="160" />
            <el-table-column prop="score" label="分数" />
            <el-table-column label="检测时间" min-width="180">
              <template #default="{ row }">
                {{ formatTime(row.detected_at_ms) }}
              </template>
            </el-table-column>
            <el-table-column label="窗口结束" min-width="180">
              <template #default="{ row }">
                {{ formatTime(row.window_end_ms) }}
              </template>
            </el-table-column>
          </el-table>
          <div class="pagination-row">
            <el-pagination
              background
              layout="total, prev, pager, next"
              :total="snoreTotal"
              :page-size="snoreQuery.pageSize"
              :current-page="snoreQuery.pageNum"
              @current-change="handleSnorePageChange"
            />
          </div>
        </section>
      </el-tab-pane>
    </el-tabs>
  </div>
</template>

<script setup>
import { onMounted, reactive, ref } from "vue";

import { useUserStore } from "./userStore.js";
import { fetchHeartHistory } from "./request.js";
import { fetchSnoreEvents } from "./snoreRequest.js";

const userStore = useUserStore();
const activeTab = ref("vitals");

const vitalsQuery = reactive({
  pageNum: 1,
  pageSize: 10,
  date: "",
});
const snoreQuery = reactive({
  pageNum: 1,
  pageSize: 10,
  date: "",
  deviceID: "",
});

const vitalsRecords = ref([]);
const vitalsTotal = ref(0);
const snoreRecords = ref([]);
const snoreTotal = ref(0);

function formatTime(value) {
  return value ? new Date(value).toLocaleString("zh-CN", { hour12: false }) : "--";
}

async function loadVitals() {
  const payload = await fetchHeartHistory(
    {
      pageNum: vitalsQuery.pageNum,
      pageSize: vitalsQuery.pageSize,
      date: vitalsQuery.date || undefined,
      userID: userStore.userInfo?.userID || undefined,
    },
    { silentError: true }
  );
  const data = payload.data || {};
  vitalsRecords.value = data.records || [];
  vitalsTotal.value = data.total || 0;
}

async function loadSnore() {
  const payload = await fetchSnoreEvents(
    {
      pageNum: snoreQuery.pageNum,
      pageSize: snoreQuery.pageSize,
      date: snoreQuery.date || undefined,
      userID: userStore.userInfo?.userID || undefined,
      deviceID: snoreQuery.deviceID || undefined,
    },
    { silentError: true }
  );
  const data = payload.data || {};
  snoreRecords.value = data.records || [];
  snoreTotal.value = data.total || 0;
}

function handleVitalsPageChange(page) {
  vitalsQuery.pageNum = page;
  loadVitals();
}

function handleSnorePageChange(page) {
  snoreQuery.pageNum = page;
  loadSnore();
}

onMounted(async () => {
  await loadVitals();
  await loadSnore();
});
</script>

<style scoped>
.page-shell {
  display: flex;
  flex-direction: column;
  gap: 16px;
}

.history-card {
  padding: 22px;
  border-radius: 24px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 20px 40px rgba(15, 23, 42, 0.08);
}

.toolbar {
  display: flex;
  flex-wrap: wrap;
  gap: 12px;
  margin-bottom: 18px;
}

.pagination-row {
  display: flex;
  justify-content: flex-end;
  margin-top: 20px;
}
</style>
