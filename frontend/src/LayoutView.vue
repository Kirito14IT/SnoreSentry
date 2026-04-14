<template>
  <div class="layout-shell">
    <aside class="sidebar">
      <div class="brand">
        <span class="brand-mark">RM</span>
        <div>
          <strong>Radar Monitor</strong>
          <p>前后端 + 呼噜检测恢复版</p>
        </div>
      </div>
      <router-link class="nav-link" to="/manage/heart_pic">实时监测</router-link>
      <router-link class="nav-link" to="/manage/data">历史数据</router-link>
      <router-link class="nav-link" to="/manage/breath_pic">呼吸波形</router-link>
    </aside>
    <main class="content-area">
      <header class="topbar">
        <div>
          <h2>监测工作台</h2>
          <p>当前用户：{{ displayName }}</p>
        </div>
        <el-button @click="logout">退出</el-button>
      </header>
      <router-view />
    </main>
  </div>
</template>

<script setup>
import { computed } from "vue";
import { useRouter } from "vue-router";

import { useUserStore } from "./userStore.js";

const router = useRouter();
const userStore = useUserStore();

const displayName = computed(() => {
  if (!userStore.userInfo) {
    return "未登录";
  }
  return `${userStore.userInfo.nickname || "未命名用户"} (#${userStore.userInfo.userID})`;
});

function logout() {
  userStore.logout();
  router.push("/login");
}
</script>

<style scoped>
.layout-shell {
  display: grid;
  grid-template-columns: 260px 1fr;
  min-height: 100vh;
}

.sidebar {
  padding: 28px 22px;
  background: linear-gradient(180deg, #0f172a 0%, #16243f 100%);
  color: #e2e8f0;
}

.brand {
  display: flex;
  gap: 14px;
  align-items: center;
  margin-bottom: 28px;
}

.brand-mark {
  display: inline-flex;
  align-items: center;
  justify-content: center;
  width: 48px;
  height: 48px;
  border-radius: 16px;
  background: linear-gradient(135deg, #38bdf8 0%, #f59e0b 100%);
  color: #0f172a;
  font-weight: 800;
}

.brand strong {
  display: block;
  font-size: 18px;
}

.brand p {
  margin: 4px 0 0;
  color: #94a3b8;
  font-size: 12px;
}

.nav-link {
  display: block;
  margin-bottom: 10px;
  padding: 12px 14px;
  border-radius: 14px;
  color: #cbd5e1;
  text-decoration: none;
  transition: background 0.2s ease, color 0.2s ease;
}

.nav-link.router-link-active {
  background: rgba(56, 189, 248, 0.18);
  color: #f8fafc;
}

.content-area {
  padding: 24px;
}

.topbar {
  display: flex;
  justify-content: space-between;
  align-items: center;
  padding: 20px 22px;
  margin-bottom: 22px;
  border-radius: 22px;
  background: rgba(255, 255, 255, 0.88);
  box-shadow: 0 16px 40px rgba(15, 23, 42, 0.08);
}

.topbar h2 {
  margin: 0 0 6px;
}

.topbar p {
  margin: 0;
  color: #64748b;
}

@media (max-width: 960px) {
  .layout-shell {
    grid-template-columns: 1fr;
  }

  .sidebar {
    padding-bottom: 12px;
  }
}
</style>
