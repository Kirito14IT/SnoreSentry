# Edgi_Talk_M55_XiaoZhi Recovery Notes

## What was restored

- `snore_backend_client.h`
- `snore_backend_client.cpp`

These two files are the board-side backend bridge that was added for snore detection v1:

- device heartbeat to `http://<backend>:9090/snore/device/heartbeat`
- command polling from `/snore/device/commands/next`
- command ack to `/snore/device/commands/ack`
- snore event upload to `/snore/device/events`
- server time offset maintenance for radar/snore time alignment

## What could not be restored as full source files

The original source files below are no longer present in the repository, so they were not recreated as fake full files:

- `applications/xiaozhi/xiaozhi.cpp`
- `applications/xiaozhi/ui/xiaozhi_ui.h`
- `applications/xiaozhi/ui/xiaozhi_ui.c`
- `applications/xiaozhi/wake_word/meow_detect_once.cpp`

Instead, the required integration changes are preserved in `recovery_patches/`.

## How to use the recovery patches

1. Restore the original `xiaozhi` source tree.
2. Re-apply the patches in `recovery_patches/`.
3. Verify `SNORE_BACKEND_BASE_URL` matches the actual snore backend host.
4. Rebuild in RRT Studio and re-run board download + serial verification.

## Validation status

- Recovered from prior implementation context, not from original deleted source.
- Not compiled in this session.
- The exact `webclient` POST/GET helper signature may need one final adjustment against the restored local SDK headers.
