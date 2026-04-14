<template>
  <div class="login-page">
    <div class="login-card">
      <div class="headline">
        <span class="eyebrow">Radar + Snore Prototype</span>
        <h1>监测工作台登录</h1>
        <p>当前源码已按恢复模式重建。这里保留一个最小登录入口，用于前端继续联调。</p>
      </div>
      <el-form label-position="top" @submit.prevent>
        <el-form-item label="用户 ID">
          <el-input v-model="form.userID" placeholder="输入当前用户 ID，例如 1" />
        </el-form-item>
        <el-form-item label="显示名称">
          <el-input v-model="form.nickname" placeholder="输入显示名称，例如 张三" />
        </el-form-item>
        <el-button type="primary" size="large" class="submit-btn" @click="handleLogin">进入工作台</el-button>
      </el-form>
    </div>
  </div>
</template>

<script setup>
import { reactive } from "vue";
import { useRouter } from "vue-router";
import { ElMessage } from "element-plus";

import { useUserStore } from "./userStore.js";

const router = useRouter();
const userStore = useUserStore();

const form = reactive({
  userID: userStore.userInfo?.userID ? String(userStore.userInfo.userID) : "",
  nickname: userStore.userInfo?.nickname || "",
});

function handleLogin() {
  const userID = Number(form.userID);
  if (!Number.isFinite(userID) || userID <= 0) {
    ElMessage.warning("请输入有效的用户 ID");
    return;
  }
  userStore.setUserInfo({
    userID,
    nickname: form.nickname || `用户${userID}`,
  });
  router.push("/manage/heart_pic");
}
</script>

<style scoped>
.login-page {
  min-height: 100vh;
  display: flex;
  align-items: center;
  justify-content: center;
  padding: 24px;
}

.login-card {
  width: min(460px, 100%);
  padding: 36px;
  border-radius: 28px;
  background: rgba(255, 255, 255, 0.92);
  box-shadow: 0 24px 60px rgba(15, 23, 42, 0.12);
  backdrop-filter: blur(18px);
}

.headline {
  margin-bottom: 24px;
}

.eyebrow {
  display: inline-flex;
  margin-bottom: 10px;
  padding: 6px 10px;
  border-radius: 999px;
  background: #e0f2fe;
  color: #0369a1;
  font-size: 12px;
  font-weight: 700;
}

.headline h1 {
  margin: 0 0 12px;
  font-size: 30px;
  line-height: 1.1;
}

.headline p {
  margin: 0;
  color: #64748b;
  line-height: 1.6;
}

.submit-btn {
  width: 100%;
  margin-top: 8px;
}
</style>
