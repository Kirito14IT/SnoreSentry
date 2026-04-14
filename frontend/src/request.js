import axios from "axios";
import { ElMessage } from "element-plus";

const request = axios.create({
  baseURL: "http://localhost:8081",
  timeout: 30000,
});

request.interceptors.request.use((config) => {
  config.headers["Content-Type"] = "application/json;charset=utf-8";
  return config;
});

request.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (!error.config?.silentError) {
      if (error.response?.status === 404) {
        ElMessage.error("未找到请求接口");
      } else if (error.response?.status === 500) {
        ElMessage.error("系统异常，请查看后端控制台");
      } else if (error.message) {
        ElMessage.error(error.message);
      }
    }
    return Promise.reject(error);
  }
);

export function fetchRadarTarget(config = {}) {
  return request.get("/target", config);
}

export function fetchRadarDetailed(config = {}) {
  return request.get("/detailed", config);
}

export function saveVitalsWithUser(data, config = {}) {
  return request.post("/save-vitals-with-user", data, config);
}

export function fetchHeartHistory(params, config = {}) {
  return request.get("/heartdata/selectPage", { params, ...config });
}

export default request;
