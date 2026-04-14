#ifndef SNORE_BACKEND_CLIENT_H
#define SNORE_BACKEND_CLIENT_H

#include <rtthread.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void snore_backend_client_init(void);
void snore_backend_set_detecting(rt_bool_t detecting);
void snore_backend_on_score(
    float score,
    rt_bool_t detected,
    int64_t window_start_local_ms,
    int64_t window_end_local_ms,
    int64_t inference_done_local_ms
);
int64_t snore_backend_get_server_offset_ms(void);

#ifdef __cplusplus
}
#endif

#endif
