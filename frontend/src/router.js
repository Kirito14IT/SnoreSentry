import { createRouter, createWebHistory } from "vue-router";

import LayoutView from "./LayoutView.vue";
import LoginView from "./LoginView.vue";
import HeartPicView from "./heart_pic.vue";
import DataView from "./data.vue";
import BreathPicView from "./breath_pic.vue";

const router = createRouter({
  history: createWebHistory(),
  routes: [
    { path: "/", redirect: "/login" },
    { path: "/login", name: "login", component: LoginView },
    {
      path: "/manage",
      component: LayoutView,
      children: [
        { path: "heart_pic", name: "heart_pic", component: HeartPicView },
        { path: "data", name: "data", component: DataView },
        { path: "breath_pic", name: "breath_pic", component: BreathPicView },
      ],
    },
  ],
});

export default router;
