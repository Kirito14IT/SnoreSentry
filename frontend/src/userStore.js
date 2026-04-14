import { defineStore } from "pinia";

const STORAGE_KEY = "radar-monitor-user";

function loadUser() {
  try {
    const raw = window.localStorage.getItem(STORAGE_KEY);
    return raw ? JSON.parse(raw) : null;
  } catch {
    return null;
  }
}

export const useUserStore = defineStore("user", {
  state: () => ({
    userInfo: loadUser(),
  }),
  actions: {
    setUserInfo(userInfo) {
      this.userInfo = userInfo;
      window.localStorage.setItem(STORAGE_KEY, JSON.stringify(userInfo));
    },
    logout() {
      this.userInfo = null;
      window.localStorage.removeItem(STORAGE_KEY);
    },
  },
});
