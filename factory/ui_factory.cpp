/**
 * @file      ui_factory.cpp
 * @author    Lewis He (lewishe@outlook.com)
 * @license   MIT
 * @copyright Copyright (c) 2025  ShenZhen XinYuan Electronic Technology Co., Ltd
 * @date      2025-01-05
 *
 */
#include "ui_define.h"
#ifdef ARDUINO
#include "UniversalMesh.h"
#include <esp_wifi.h>
#endif

static uint8_t *image_data = NULL;
static lv_timer_t *timer = NULL;
static uint8_t nextColors = 0;

static void generate_gray_gradient(uint8_t *image_data, uint16_t width, uint16_t height)
{
    for (uint16_t y = 0; y < height; y++) {
        for (uint16_t x = 0; x < width; x++) {
            uint8_t gray_value = (uint8_t)((float)x / width * 255);
            image_data[y * width + x] = gray_value;
        }
    }
}


static void display_gray_image(lv_obj_t *parent)
{
    uint16_t width = lv_disp_get_hor_res(NULL);
    uint16_t height = lv_disp_get_ver_res(NULL);

    image_data = (uint8_t *)lv_mem_alloc(width * height);
    if (image_data == NULL) {
        printf("memory failed!\n");
        return;
    }
    generate_gray_gradient(image_data, width, height);
    static lv_img_dsc_t gray_image;
    gray_image.header.cf = LV_IMG_CF_ALPHA_8BIT;
    gray_image.header.w = width;
    gray_image.header.h = height;
    gray_image.data_size = width * height;
    gray_image.data = image_data;

    lv_obj_set_style_bg_img_src(parent, &gray_image, LV_PART_MAIN);
}

static void factory_timer_callback(lv_timer_t *t)
{
    lv_obj_t *obj = (lv_obj_t *)lv_timer_get_user_data(t);
    lv_obj_clean(obj);
    switch (nextColors) {
    case 0:
        display_gray_image(obj);
        break;
    case 1:
        lv_obj_set_style_bg_img_src(obj, NULL, LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        break;
    case 2:
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 255, 0), LV_PART_MAIN);
        break;
    case 3:
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 0, 255), LV_PART_MAIN);
        break;
    case 4:
        lv_obj_set_style_border_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(255, 255, 255), LV_PART_MAIN);
        break;
    case 5:
        lv_obj_set_style_border_color(obj, lv_color_make(255, 0, 0), LV_PART_MAIN);
        lv_obj_set_style_bg_color(obj, lv_color_make(0, 0, 0), LV_PART_MAIN);
        break;
    case 6:
        if (timer) {
            if (image_data) {
                lv_mem_free(image_data);
            }
            lv_obj_del(obj);
            lv_timer_del(timer);
            timer = NULL;
            nextColors = 0;
            enable_input_devices();
            menu_show();
            set_low_power_mode_flag(true);
        }
        return;
    case 7:
    default:
        break;
    }
    nextColors++;
}

// -------------------------------------------------------
// Factory exit helper (click-to-dismiss for factory test)
// -------------------------------------------------------
static void _event_cb(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target_obj(e);
    if (timer) {
        if (image_data) {
            lv_mem_free(image_data);
        }
        lv_obj_del(obj);
        lv_timer_del(timer);
        timer = NULL;
        nextColors = 0;
        enable_input_devices();
        menu_show();
        set_low_power_mode_flag(true);
    }
}

void ui_factory_enter(lv_obj_t *parent)
{
    if (timer) {
        return;
    }
    disable_input_devices();
    set_low_power_mode_flag(false);

    lv_obj_t *obj = lv_obj_create(lv_scr_act());
    lv_obj_set_size(obj, lv_pct(100), lv_pct(100));
    lv_obj_center(obj);
    timer = lv_timer_create(factory_timer_callback, 3000, obj);
    lv_timer_ready(timer);
    lv_group_add_obj(lv_group_get_default(), obj);
    lv_obj_add_event(obj, _event_cb, LV_EVENT_CLICKED, NULL);
}

void ui_factory_exit(lv_obj_t *parent)
{
}

// -------------------------------------------------------
// UniversalMesh screen
// -------------------------------------------------------
#ifdef ARDUINO

#define UM_NODE_NAME    "lora-pager"
#define UM_HB_INTERVAL  60000UL
#define UM_LOG_ROWS     6

enum UMState { UM_DISCOVERING, UM_CONNECTED, UM_NO_COORD };

static UniversalMesh     um_mesh;
static volatile UMState  um_state       = UM_DISCOVERING;
static uint8_t           um_myMac[6]    = {};
static uint8_t           um_coordMac[6] = {};
static uint8_t           um_channel     = 0;
static unsigned long     um_lastHB      = 0;
static lv_timer_t       *um_timer       = NULL;
static TaskHandle_t      um_task        = NULL;
static SemaphoreHandle_t um_mutex       = NULL;

static char    um_log[UM_LOG_ROWS][72]  = {};
static uint8_t um_logHead               = 0;
static uint8_t um_logCount              = 0;

static lv_obj_t *um_root       = NULL;
static lv_obj_t *um_status_lbl = NULL;
static lv_obj_t *um_info_lbl   = NULL;
static lv_obj_t *um_log_cont   = NULL;

static void um_log_push(const char *line)
{
    if (!um_mutex) return;
    if (xSemaphoreTake(um_mutex, pdMS_TO_TICKS(10)) != pdTRUE) return;
    strncpy(um_log[um_logHead], line, 71);
    um_log[um_logHead][71] = '\0';
    um_logHead = (um_logHead + 1) % UM_LOG_ROWS;
    if (um_logCount < UM_LOG_ROWS) um_logCount++;
    xSemaphoreGive(um_mutex);
}

static void um_on_receive(MeshPacket *pkt, uint8_t *senderMac)
{
    char line[72];
    uint8_t plen = pkt->payloadLen > 16 ? 16 : pkt->payloadLen;
    char payload[17] = {};
    for (int i = 0; i < plen; i++) {
        uint8_t b = pkt->payload[i];
        payload[i] = (b >= 0x20 && b < 0x7F) ? (char)b : '.';
    }
    snprintf(line, sizeof(line), "%02X%02X>%02X%02X T%02X A%02X [%s]",
             senderMac[4], senderMac[5],
             pkt->destMac[4], pkt->destMac[5],
             pkt->type, pkt->appId, payload);
    um_log_push(line);
}

static void um_discovery_task(void *param)
{
    char line[72];
    um_log_push("Scanning channels 1-13...");
    um_channel = um_mesh.findCoordinatorChannel(UM_NODE_NAME);

    if (um_channel == 0) {
        um_log_push("No coordinator found");
        um_state = UM_NO_COORD;
        um_task  = NULL;
        vTaskDelete(NULL);
        return;
    }

    snprintf(line, sizeof(line), "Found coordinator ch%d", um_channel);
    um_log_push(line);

    um_mesh.getCoordinatorMac(um_coordMac);
    snprintf(line, sizeof(line), "Coord %02X:%02X:%02X:%02X:%02X:%02X",
             um_coordMac[0], um_coordMac[1], um_coordMac[2],
             um_coordMac[3], um_coordMac[4], um_coordMac[5]);
    um_log_push(line);

    um_mesh.begin(um_channel);
    um_mesh.setCoordinatorMac(um_coordMac);
    um_mesh.onReceive(um_on_receive);
    esp_wifi_get_mac(WIFI_IF_STA, um_myMac);

    snprintf(line, sizeof(line), "My MAC %02X:%02X:%02X:%02X:%02X:%02X",
             um_myMac[0], um_myMac[1], um_myMac[2],
             um_myMac[3], um_myMac[4], um_myMac[5]);
    um_log_push(line);

    // Announce presence - same as sensor_node
    um_mesh.send(um_coordMac, MESH_TYPE_PING, 0x00,
                 (const uint8_t *)UM_NODE_NAME, strlen(UM_NODE_NAME), 4);
    um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x06,
                 (const uint8_t *)UM_NODE_NAME, strlen(UM_NODE_NAME), 4);
    um_log_push("Announced. Listening...");

    um_lastHB = millis() - UM_HB_INTERVAL;  // fire HB on first tick
    um_state  = UM_CONNECTED;
    um_task   = NULL;
    vTaskDelete(NULL);
}

static uint8_t um_dotPhase = 0;

static void universalmesh_timer_callback(lv_timer_t *t)
{
    // Only drive mesh ops once connected (avoids racing with begin() in task)
    if (um_state == UM_CONNECTED) {
        um_mesh.update();

        unsigned long now = millis();
        if (now - um_lastHB >= UM_HB_INTERVAL) {
            um_lastHB = now;
            uint8_t hb = 0x01;
            um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x05, &hb, 1, 4);
            um_mesh.send(um_coordMac, MESH_TYPE_DATA, 0x06,
                         (const uint8_t *)UM_NODE_NAME, strlen(UM_NODE_NAME), 4);
            um_log_push("[HB] Heartbeat sent");
        }
    }

    // Status label
    if (um_status_lbl) {
        if (um_state == UM_DISCOVERING) {
            static const char *dots[] = {".", "..", "..."};
            char buf[24];
            snprintf(buf, sizeof(buf), "Scanning%s", dots[um_dotPhase % 3]);
            um_dotPhase++;
            lv_label_set_text(um_status_lbl, buf);
            lv_obj_set_style_text_color(um_status_lbl, lv_color_make(255, 160, 0), LV_PART_MAIN);
        } else if (um_state == UM_NO_COORD) {
            lv_label_set_text(um_status_lbl, "No coordinator");
            lv_obj_set_style_text_color(um_status_lbl, lv_color_make(255, 60, 60), LV_PART_MAIN);
        } else {
            lv_label_set_text(um_status_lbl, "Connected");
            lv_obj_set_style_text_color(um_status_lbl, lv_color_make(0, 220, 0), LV_PART_MAIN);
        }
    }

    // Info label: channel + own MAC
    if (um_info_lbl && um_state == UM_CONNECTED) {
        char buf[56];
        snprintf(buf, sizeof(buf), "Ch:%d  %02X:%02X:%02X:%02X:%02X:%02X",
                 um_channel,
                 um_myMac[0], um_myMac[1], um_myMac[2],
                 um_myMac[3], um_myMac[4], um_myMac[5]);
        lv_label_set_text(um_info_lbl, buf);
        lv_obj_set_style_text_color(um_info_lbl, lv_color_make(150, 150, 150), LV_PART_MAIN);
    }

    // Rebuild traffic log from ring buffer
    if (um_log_cont && um_mutex) {
        lv_obj_clean(um_log_cont);
        if (xSemaphoreTake(um_mutex, pdMS_TO_TICKS(5)) == pdTRUE) {
            int start = (UM_LOG_ROWS + um_logHead - um_logCount) % UM_LOG_ROWS;
            for (int i = 0; i < um_logCount; i++) {
                lv_obj_t *lbl = lv_label_create(um_log_cont);
                lv_label_set_text(lbl, um_log[(start + i) % UM_LOG_ROWS]);
                lv_obj_set_style_text_color(lbl, lv_color_make(0, 200, 0), LV_PART_MAIN);
                lv_obj_set_width(lbl, lv_pct(100));
            }
            xSemaphoreGive(um_mutex);
        }
    }
}

static void um_cleanup(void)
{
    if (um_timer) { lv_timer_del(um_timer); um_timer = NULL; }
    if (um_task)  { vTaskDelete(um_task);   um_task  = NULL; }
    if (um_mutex) { vSemaphoreDelete(um_mutex); um_mutex = NULL; }
    um_status_lbl = NULL;
    um_info_lbl   = NULL;
    um_log_cont   = NULL;
    if (um_root)  { lv_obj_del(um_root); um_root = NULL; }
}

static void um_exit_event_cb(lv_event_t *e)
{
    um_cleanup();
    enable_input_devices();
    menu_show();
    set_low_power_mode_flag(true);
}

void ui_universalmesh_enter(lv_obj_t *parent)
{
    if (um_timer) return;

    um_state    = UM_DISCOVERING;
    um_channel  = 0;
    um_logHead  = 0;
    um_logCount = 0;
    um_dotPhase = 0;
    memset(um_myMac, 0, sizeof(um_myMac));
    memset(um_coordMac, 0, sizeof(um_coordMac));

    disable_input_devices();
    set_low_power_mode_flag(false);

    um_mutex = xSemaphoreCreateMutex();

    // Full-screen black container with column flex layout
    um_root = lv_obj_create(lv_scr_act());
    lv_obj_set_size(um_root, lv_pct(100), lv_pct(100));
    lv_obj_center(um_root);
    lv_obj_set_style_bg_color(um_root, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_border_width(um_root, 0, LV_PART_MAIN);
    lv_obj_set_style_radius(um_root, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_root, 6, LV_PART_MAIN);
    lv_obj_set_flex_flow(um_root, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_root, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(um_root, LV_OBJ_FLAG_SCROLLABLE);

    // Header row: title left, status right
    lv_obj_t *hdr = lv_obj_create(um_root);
    lv_obj_set_width(hdr, lv_pct(100));
    lv_obj_set_height(hdr, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(hdr, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(hdr, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(hdr, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(hdr, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(hdr, LV_FLEX_ALIGN_SPACE_BETWEEN, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *title = lv_label_create(hdr);
    lv_label_set_text(title, LV_SYMBOL_WIFI " UniversalMesh");
    lv_obj_set_style_text_color(title, lv_color_make(0, 200, 255), LV_PART_MAIN);

    um_status_lbl = lv_label_create(hdr);
    lv_label_set_text(um_status_lbl, "Scanning.");
    lv_obj_set_style_text_color(um_status_lbl, lv_color_make(255, 160, 0), LV_PART_MAIN);

    // Thin divider
    lv_obj_t *div = lv_obj_create(um_root);
    lv_obj_set_size(div, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_border_width(div, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div, 0, LV_PART_MAIN);

    // Info line: channel + own MAC
    um_info_lbl = lv_label_create(um_root);
    lv_label_set_text(um_info_lbl, "Ch:--  --:--:--:--:--:--");
    lv_obj_set_style_text_color(um_info_lbl, lv_color_make(80, 80, 80), LV_PART_MAIN);
    lv_obj_set_width(um_info_lbl, lv_pct(100));

    // Thin divider
    lv_obj_t *div2 = lv_obj_create(um_root);
    lv_obj_set_size(div2, lv_pct(100), 1);
    lv_obj_set_style_bg_color(div2, lv_color_make(50, 50, 50), LV_PART_MAIN);
    lv_obj_set_style_border_width(div2, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(div2, 0, LV_PART_MAIN);

    // Traffic log container - expands to fill remaining height
    um_log_cont = lv_obj_create(um_root);
    lv_obj_set_width(um_log_cont, lv_pct(100));
    lv_obj_set_flex_grow(um_log_cont, 1);
    lv_obj_set_style_bg_opa(um_log_cont, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(um_log_cont, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(um_log_cont, 2, LV_PART_MAIN);
    lv_obj_set_flex_flow(um_log_cont, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(um_log_cont, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_clear_flag(um_log_cont, LV_OBJ_FLAG_SCROLLABLE);

    lv_group_add_obj(lv_group_get_default(), um_root);
    lv_obj_add_event(um_root, um_exit_event_cb, LV_EVENT_CLICKED, NULL);

    // Background task for blocking channel scan
    xTaskCreate(um_discovery_task, "um_disc", 4096, NULL, 5, &um_task);

    // 500ms refresh timer
    um_timer = lv_timer_create(universalmesh_timer_callback, 500, NULL);
    lv_timer_ready(um_timer);
}

void ui_universalmesh_exit(lv_obj_t *parent)
{
    um_cleanup();
    enable_input_devices();
}

#else  // emulator stubs

static void universalmesh_timer_callback(lv_timer_t *t) {}
void ui_universalmesh_enter(lv_obj_t *parent) {}
void ui_universalmesh_exit(lv_obj_t *parent) {}

#endif  // ARDUINO

app_t ui_factory_main = {
    .setup_func_cb = ui_factory_enter,
    .exit_func_cb = ui_factory_exit,
    .user_data = nullptr,
};

app_t ui_universalmesh_main = {
    .setup_func_cb = ui_universalmesh_enter,
    .exit_func_cb = ui_universalmesh_exit,
    .user_data = nullptr,
};
