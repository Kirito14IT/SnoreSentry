#include "snore_backend_client.h"

#include <cJSON.h>
#include <rtthread.h>
#include <string.h>
#include <stdlib.h>

#include "webclient.h"

/*
 * Recovery note:
 * This file reconstructs the dedicated board->backend snore bridge that was
 * added in the previous integration round. The exact webclient helper
 * signatures may need a final pass against the restored SDK headers, but the
 * state model, payload schema, threads, and control flow match the deleted
 * implementation.
 */

#ifndef SNORE_BACKEND_BASE_URL
#define SNORE_BACKEND_BASE_URL "http://192.168.118.149:9090"
#endif

#ifndef SNORE_BACKEND_DEVICE_ID
#define SNORE_BACKEND_DEVICE_ID "M55-Snore"
#endif

#ifndef SNORE_BACKEND_DEVICE_NAME
#define SNORE_BACKEND_DEVICE_NAME "M55-Snore"
#endif

#define SNORE_HEARTBEAT_PERIOD_MS 1000
#define SNORE_COMMAND_PERIOD_MS 1000
#define SNORE_HTTP_BUFFER_SIZE 2048

extern "C" void xiaozhi_ui_enter_meow_mode_from_voice(void);
extern "C" void xiaozhi_ui_exit_meow_mode_remote(void);

typedef struct snore_backend_state
{
    rt_mutex_t lock;
    rt_bool_t detecting;
    float latest_score;
    int64_t latest_window_start_local_ms;
    int64_t latest_window_end_local_ms;
    int64_t last_event_server_ms;
    int64_t server_offset_ms;
    rt_bool_t started;
} snore_backend_state_t;

static snore_backend_state_t g_snore_backend = {0};

static int64_t local_ms_now(void)
{
    return (int64_t)rt_tick_get_millisecond();
}

static int64_t local_to_server_ms(int64_t local_ms)
{
    return local_ms + g_snore_backend.server_offset_ms;
}

static void snore_backend_state_lock(void)
{
    if (g_snore_backend.lock)
    {
        rt_mutex_take(g_snore_backend.lock, RT_WAITING_FOREVER);
    }
}

static void snore_backend_state_unlock(void)
{
    if (g_snore_backend.lock)
    {
        rt_mutex_release(g_snore_backend.lock);
    }
}

static int http_post_json(const char *path, const char *json_body)
{
    char url[256] = {0};
    struct webclient_session *session = RT_NULL;
    int result = -1;

    rt_snprintf(url, sizeof(url), "%s%s", SNORE_BACKEND_BASE_URL, path);
    session = webclient_session_create(SNORE_HTTP_BUFFER_SIZE);
    if (session == RT_NULL)
    {
        return -1;
    }

    webclient_header_fields_add(session, "Content-Type: application/json\r\n");
    webclient_header_fields_add(session, "Accept: application/json\r\n");

    /*
     * The local package previously compiled with a working webclient POST path.
     * If the SDK signature differs after source restoration, only this call
     * site should need adjustment.
     */
    result = webclient_post(session, url, json_body, strlen(json_body));
    webclient_close(session);
    return result;
}

static int http_get_text(const char *path, char *response, size_t response_size)
{
    char url[256] = {0};
    int total = 0;
    int read_len = 0;
    struct webclient_session *session = RT_NULL;

    if (response == RT_NULL || response_size == 0)
    {
        return -1;
    }

    response[0] = '\0';
    rt_snprintf(url, sizeof(url), "%s%s", SNORE_BACKEND_BASE_URL, path);
    session = webclient_session_create(SNORE_HTTP_BUFFER_SIZE);
    if (session == RT_NULL)
    {
        return -1;
    }

    if (webclient_get(session, url) != 200)
    {
        webclient_close(session);
        return -1;
    }

    while ((read_len = webclient_read(session, response + total, (int)(response_size - total - 1))) > 0)
    {
        total += read_len;
        if ((size_t)total >= response_size - 1)
        {
            break;
        }
    }

    response[total] = '\0';
    webclient_close(session);
    return total;
}

static void update_server_offset_from_response(cJSON *data_obj)
{
    cJSON *server_time = cJSON_GetObjectItem(data_obj, "server_time_ms");
    if (!cJSON_IsNumber(server_time))
    {
        return;
    }

    snore_backend_state_lock();
    g_snore_backend.server_offset_ms = (int64_t)server_time->valuedouble - local_ms_now();
    snore_backend_state_unlock();
}

static void post_heartbeat(void)
{
    char *json_text = RT_NULL;
    cJSON *root = cJSON_CreateObject();
    cJSON *response_root = RT_NULL;
    char response[SNORE_HTTP_BUFFER_SIZE] = {0};
    rt_bool_t detecting = RT_FALSE;
    float latest_score = 0.0f;
    int64_t latest_window_start_local_ms = 0;
    int64_t latest_window_end_local_ms = 0;
    int64_t last_event_server_ms = 0;

    snore_backend_state_lock();
    detecting = g_snore_backend.detecting;
    latest_score = g_snore_backend.latest_score;
    latest_window_start_local_ms = g_snore_backend.latest_window_start_local_ms;
    latest_window_end_local_ms = g_snore_backend.latest_window_end_local_ms;
    last_event_server_ms = g_snore_backend.last_event_server_ms;
    snore_backend_state_unlock();

    cJSON_AddStringToObject(root, "deviceID", SNORE_BACKEND_DEVICE_ID);
    cJSON_AddStringToObject(root, "deviceName", SNORE_BACKEND_DEVICE_NAME);
    cJSON_AddBoolToObject(root, "detecting", detecting ? 1 : 0);
    cJSON_AddNumberToObject(root, "latest_score", latest_score);
    cJSON_AddNumberToObject(root, "latest_window_start_ms", (double)local_to_server_ms(latest_window_start_local_ms));
    cJSON_AddNumberToObject(root, "latest_window_end_ms", (double)local_to_server_ms(latest_window_end_local_ms));
    cJSON_AddNumberToObject(root, "last_event_at_ms", (double)last_event_server_ms);
    cJSON_AddNumberToObject(root, "timestamp", (double)local_to_server_ms(local_ms_now()));

    json_text = cJSON_PrintUnformatted(root);
    if (json_text != RT_NULL && http_post_json("/snore/device/heartbeat", json_text) >= 0)
    {
        if (http_get_text("/snore/device/commands/next?deviceID=" SNORE_BACKEND_DEVICE_ID, response, sizeof(response)) > 0)
        {
            response_root = cJSON_Parse(response);
            if (response_root)
            {
                cJSON *data_obj = cJSON_GetObjectItem(response_root, "data");
                if (cJSON_IsObject(data_obj))
                {
                    update_server_offset_from_response(data_obj);
                }
                cJSON_Delete(response_root);
            }
        }
    }

    if (json_text)
    {
        cJSON_free(json_text);
    }
    cJSON_Delete(root);
}

static void post_command_ack(int command_id, const char *status, const char *message)
{
    cJSON *root = cJSON_CreateObject();
    char *json_text = RT_NULL;

    cJSON_AddStringToObject(root, "deviceID", SNORE_BACKEND_DEVICE_ID);
    cJSON_AddNumberToObject(root, "commandID", command_id);
    cJSON_AddStringToObject(root, "status", status);
    cJSON_AddStringToObject(root, "message", message ? message : "");

    json_text = cJSON_PrintUnformatted(root);
    if (json_text)
    {
        http_post_json("/snore/device/commands/ack", json_text);
        cJSON_free(json_text);
    }
    cJSON_Delete(root);
}

static void post_detect_event(float score, int64_t window_start_local_ms, int64_t window_end_local_ms)
{
    cJSON *root = cJSON_CreateObject();
    char *json_text = RT_NULL;
    int64_t detected_at_ms = local_to_server_ms(local_ms_now());

    cJSON_AddStringToObject(root, "deviceID", SNORE_BACKEND_DEVICE_ID);
    cJSON_AddNumberToObject(root, "score", score);
    cJSON_AddNumberToObject(root, "detected_at_ms", (double)detected_at_ms);
    cJSON_AddNumberToObject(root, "window_start_ms", (double)local_to_server_ms(window_start_local_ms));
    cJSON_AddNumberToObject(root, "window_end_ms", (double)local_to_server_ms(window_end_local_ms));

    json_text = cJSON_PrintUnformatted(root);
    if (json_text)
    {
        http_post_json("/snore/device/events", json_text);
        cJSON_free(json_text);
    }
    cJSON_Delete(root);

    snore_backend_state_lock();
    g_snore_backend.last_event_server_ms = detected_at_ms;
    snore_backend_state_unlock();
}

static void handle_command_payload(const char *response)
{
    cJSON *root = RT_NULL;
    cJSON *data_obj = RT_NULL;
    cJSON *command_id_obj = RT_NULL;
    cJSON *command_obj = RT_NULL;
    int command_id = 0;
    const char *command = RT_NULL;

    root = cJSON_Parse(response);
    if (root == RT_NULL)
    {
        return;
    }

    data_obj = cJSON_GetObjectItem(root, "data");
    if (!cJSON_IsObject(data_obj))
    {
        cJSON_Delete(root);
        return;
    }

    command_id_obj = cJSON_GetObjectItem(data_obj, "command_id");
    command_obj = cJSON_GetObjectItem(data_obj, "command");
    if (!cJSON_IsNumber(command_id_obj) || !cJSON_IsString(command_obj))
    {
        cJSON_Delete(root);
        return;
    }

    command_id = command_id_obj->valueint;
    command = command_obj->valuestring;

    if (strcmp(command, "start") == 0)
    {
        xiaozhi_ui_enter_meow_mode_from_voice();
        post_command_ack(command_id, "success", "entered snore mode");
    }
    else if (strcmp(command, "stop") == 0)
    {
        xiaozhi_ui_exit_meow_mode_remote();
        post_command_ack(command_id, "success", "exited snore mode");
    }
    else
    {
        post_command_ack(command_id, "rejected", "unknown command");
    }

    cJSON_Delete(root);
}

static void heartbeat_thread_entry(void *parameter)
{
    RT_UNUSED(parameter);
    while (1)
    {
        post_heartbeat();
        rt_thread_mdelay(SNORE_HEARTBEAT_PERIOD_MS);
    }
}

static void command_thread_entry(void *parameter)
{
    char response[SNORE_HTTP_BUFFER_SIZE] = {0};
    RT_UNUSED(parameter);

    while (1)
    {
        if (http_get_text("/snore/device/commands/next?deviceID=" SNORE_BACKEND_DEVICE_ID, response, sizeof(response)) > 0)
        {
            handle_command_payload(response);
        }
        rt_thread_mdelay(SNORE_COMMAND_PERIOD_MS);
    }
}

void snore_backend_client_init(void)
{
    if (g_snore_backend.started)
    {
      return;
    }

    g_snore_backend.lock = rt_mutex_create("snorebk", RT_IPC_FLAG_PRIO);
    if (g_snore_backend.lock == RT_NULL)
    {
        return;
    }

    g_snore_backend.detecting = RT_FALSE;
    g_snore_backend.latest_score = 0.0f;
    g_snore_backend.latest_window_start_local_ms = 0;
    g_snore_backend.latest_window_end_local_ms = 0;
    g_snore_backend.last_event_server_ms = 0;
    g_snore_backend.server_offset_ms = 0;
    g_snore_backend.started = RT_TRUE;

    rt_thread_t heartbeat_thread = rt_thread_create(
        "snore_hb",
        heartbeat_thread_entry,
        RT_NULL,
        4096,
        18,
        10
    );
    rt_thread_t command_thread = rt_thread_create(
        "snore_cmd",
        command_thread_entry,
        RT_NULL,
        4096,
        18,
        10
    );

    if (heartbeat_thread)
    {
        rt_thread_startup(heartbeat_thread);
    }
    if (command_thread)
    {
        rt_thread_startup(command_thread);
    }
}

void snore_backend_set_detecting(rt_bool_t detecting)
{
    snore_backend_state_lock();
    g_snore_backend.detecting = detecting;
    snore_backend_state_unlock();
}

void snore_backend_on_score(
    float score,
    rt_bool_t detected,
    int64_t window_start_local_ms,
    int64_t window_end_local_ms,
    int64_t inference_done_local_ms
)
{
    RT_UNUSED(inference_done_local_ms);

    snore_backend_state_lock();
    g_snore_backend.latest_score = score;
    g_snore_backend.latest_window_start_local_ms = window_start_local_ms;
    g_snore_backend.latest_window_end_local_ms = window_end_local_ms;
    snore_backend_state_unlock();

    if (detected)
    {
        post_detect_event(score, window_start_local_ms, window_end_local_ms);
    }
}

int64_t snore_backend_get_server_offset_ms(void)
{
    int64_t offset_ms = 0;
    snore_backend_state_lock();
    offset_ms = g_snore_backend.server_offset_ms;
    snore_backend_state_unlock();
    return offset_ms;
}
