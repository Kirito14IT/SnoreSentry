import axios from "axios";
import { ElMessage } from "element-plus";

const snoreRequest = axios.create({
  baseURL: "http://localhost:9090",
  timeout: 30000,
});

snoreRequest.interceptors.request.use((config) => {
  config.headers["Content-Type"] = "application/json;charset=utf-8";
  return config;
});

snoreRequest.interceptors.response.use(
  (response) => response.data,
  (error) => {
    if (!error.config?.silentError) {
      if (error.response?.data?.detail) {
        ElMessage.error(error.response.data.detail);
      } else if (error.message) {
        ElMessage.error(error.message);
      }
    }
    return Promise.reject(error);
  }
);

export function fetchOnlineSnoreDevices(config = {}) {
  return snoreRequest.get("/snore/devices/online", config);
}

export function selectSnoreViewerDevice(data, config = {}) {
  return snoreRequest.post("/snore/device-bindings/select", data, config);
}

export function fetchSnoreViewerDevice(userID, config = {}) {
  return snoreRequest.get("/snore/device-bindings/current", { params: { userID }, ...config });
}

export function startSnoreDetect(data, config = {}) {
  return snoreRequest.post("/snore/control/start", data, config);
}

export function stopSnoreDetect(data, config = {}) {
  return snoreRequest.post("/snore/control/stop", data, config);
}

export function fetchSnoreLive(params, config = {}) {
  return snoreRequest.get("/snore/live", { params, ...config });
}

export function fetchSnoreEvents(params, config = {}) {
  return snoreRequest.get("/snore/events/selectPage", { params, ...config });
}

export default snoreRequest;
