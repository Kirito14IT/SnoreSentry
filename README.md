# radar-monitor-system recovery README

## Background

This repository was partially damaged after a previous implementation round. The deleted scope was larger than the snore feature itself:

- `backend` lost the main service source files.
- `frontend` lost `package.json`, Vite config, and the whole `src` tree.
- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi` lost the board-side snore integration source and also lost the original large source files that had been patched.

This recovery focuses on restoring the snore-detection integration work so the project can continue evolving, while being explicit about the parts that cannot be faithfully reconstructed from the remaining workspace.

## Deleted code analysis

### Backend

The following source files were no longer present and were restored:

- `backend/db.py`
- `backend/snore_service.py`
- `backend/realtime_radar_processing.py`
- `backend/requirements.txt`

What changed in recovery:

- restored the snore device, control, event, and history APIs
- restored the radar-side API surface used by the frontend
- restored a local data layer using SQLite so the repo can run again without relying on missing database setup files

### Frontend

The entire source tree had been removed and was rebuilt.

Restored files include:

- `frontend/package.json`
- `frontend/vite.config.js`
- `frontend/index.html`
- `frontend/src/main.js`
- `frontend/src/App.vue`
- `frontend/src/router.js`
- `frontend/src/userStore.js`
- `frontend/src/request.js`
- `frontend/src/snoreRequest.js`
- `frontend/src/LoginView.vue`
- `frontend/src/LayoutView.vue`
- `frontend/src/heart_pic.vue`
- `frontend/src/data.vue`
- `frontend/src/breath_pic.vue`
- `frontend/src/HeartRateMonitor.vue`
- `frontend/src/BreathRateMonitor.vue`
- `frontend/src/SnoreDeviceSelector.vue`
- `frontend/src/SnoreScoreChart.vue`

What changed in recovery:

- restored the independent radar/snore control UI
- restored online device selection and snore score trend visualization
- restored history tabs for vitals and snore events
- restored a minimal login/store flow so the front end can continue using `userID`

### Edgi_Talk_M55_XiaoZhi

This was the most severe deletion area. The original patched files themselves were gone:

- `applications/xiaozhi/xiaozhi.cpp`
- `applications/xiaozhi/ui/xiaozhi_ui.h`
- `applications/xiaozhi/ui/xiaozhi_ui.c`
- `applications/xiaozhi/wake_word/meow_detect_once.cpp`

What could be fully restored:

- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/snore_backend_client.h`
- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/snore_backend_client.cpp`

What was preserved as recovery patches instead of fake full files:

- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/recovery_patches/xiaozhi.cpp.patch`
- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/recovery_patches/ui_xiaozhi_ui.h.patch`
- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/recovery_patches/ui_xiaozhi_ui.c.patch`
- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/recovery_patches/wake_word_meow_detect_once.cpp.patch`

Why patches were used:

- the original large source files are missing from the repository
- recreating fake full replacements would be misleading and risky
- the patches preserve the exact integration intent so they can be re-applied once the original board source tree is restored

See:

- `Edgi_Talk_M55_XiaoZhi/applications/xiaozhi/README_RECOVERY.md`

## Current restored structure

### Radar backend

- port: `8081`
- file: `backend/realtime_radar_processing.py`
- role:
  - `GET /target`
  - `GET /detailed`
  - `POST /save-vitals-with-user`
  - `GET /heartdata/selectPage`

### Snore backend

- port: `9090`
- file: `backend/snore_service.py`
- role:
  - online device listing
  - device viewer binding
  - start/stop control
  - device heartbeat
  - device command polling / ack
  - live snore state
  - snore event history

### Data layer

- file: `backend/db.py`
- default persistence: local SQLite file `backend/radar_monitor.db`
- note:
  - this is a recovery-safe fallback
  - if you later restore the original MySQL-based environment, you can migrate this layer again

## How to run

### Frontend

```powershell
cd frontend
npm run build
```

For local dev:

```powershell
cd frontend
npm run dev
```

### Backend

Radar service:

```powershell
cd backend
python realtime_radar_processing.py
```

Snore service:

```powershell
cd backend
python snore_service.py
```

## Board-side next step

To complete the board recovery:

1. Restore the original `applications/xiaozhi` source files from your own backup or other machine.
2. Keep `snore_backend_client.h/.cpp`.
3. Re-apply the patch files in `applications/xiaozhi/recovery_patches/`.
4. Rebuild in RRT Studio.

## Validation completed in this session

- `frontend` was rebuilt successfully with `npm run build`.

## Validation not completed in this session

- Python runtime in this Codex session still exits abnormally, so backend syntax/runtime startup was not re-verified here.
- `Edgi_Talk_M55_XiaoZhi` was not rebuilt in this session.
- Because the original `xiaozhi` patched source files are still absent, the board-side recovery is currently preserved as:
  - restored standalone module
  - re-apply patches

## Practical status

The repository is no longer in the previous “source mostly gone” state.

What is now usable again:

- frontend source and build path
- backend service source for radar/snore integration
- board-side snore backend bridge source
- board-side missing integration edits captured as patch artifacts

What still needs external recovery input:

- the original deleted `xiaozhi` base source files
- optional original radar algorithm sources if you want byte-for-byte parity with the earlier prototype rather than the recovered compatible API layer
