#include "ancs_client.h"
#include "ancs_advertising.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>

#include "ancs_lifecycle.h"
#include "ancs_parser.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "host/ble_gap.h"
#include "host/ble_gatt.h"
#include "host/ble_hs.h"
#include "host/ble_store.h"
#include "host/util/util.h"
#include "nimble/ble.h"
#include "nimble/nimble_npl.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "nvs_flash.h"
#include "os/os_mbuf.h"
#include "services/gap/ble_svc_gap.h"

#define ANCS_DEVICE_NAME "M5 ANCS"
#define ANCS_NOTIFICATION_SOURCE_LENGTH 8U
#define ANCS_EVENT_ADDED 0U
#define ANCS_EVENT_MODIFIED 1U
#define ANCS_EVENT_REMOVED 2U
#define ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES 0U
#define ANCS_ATTRIBUTE_APP_IDENTIFIER 0U
#define ANCS_ATTRIBUTE_TITLE 1U
#define ANCS_ATTRIBUTE_SUBTITLE 2U
#define ANCS_ATTRIBUTE_MESSAGE 3U
#define GATT_SERVICE_UUID 0x1801U
#define GATT_SERVICE_CHANGED_UUID 0x2a05U

static const char *const TAG = "ancs_client";

static const ble_uuid128_t ANCS_SERVICE_UUID = BLE_UUID128_INIT(
    0xd0, 0x00, 0x2d, 0x12, 0x1e, 0x4b, 0x0f, 0xa4,
    0x99, 0x4e, 0xce, 0xb5, 0x31, 0xf4, 0x05, 0x79);
static const ble_uuid128_t ANCS_NOTIFICATION_SOURCE_UUID = BLE_UUID128_INIT(
    0xbd, 0x1d, 0xa2, 0x99, 0xe6, 0x25, 0x58, 0x8c,
    0xd9, 0x42, 0x01, 0x63, 0x0d, 0x12, 0xbf, 0x9f);
static const ble_uuid128_t ANCS_CONTROL_POINT_UUID = BLE_UUID128_INIT(
    0xd9, 0xd9, 0xaa, 0xfd, 0xbd, 0x9b, 0x21, 0x98,
    0xa8, 0x49, 0xe1, 0x45, 0xf3, 0xd8, 0xd1, 0x69);
static const ble_uuid128_t ANCS_DATA_SOURCE_UUID = BLE_UUID128_INIT(
    0xfb, 0x7b, 0x7c, 0xce, 0x6a, 0xb3, 0x44, 0xbe,
    0xb5, 0x4b, 0xd6, 0x24, 0xe9, 0xc6, 0xea, 0x22);

typedef struct {
    uint16_t start_handle;
    uint16_t end_handle;
    uint16_t service_changed_handle;
    uint16_t service_changed_end_handle;
    uint16_t service_changed_cccd;
    uint16_t notification_source_def_handle;
    uint16_t notification_source_handle;
    uint16_t notification_source_cccd;
    uint16_t data_source_def_handle;
    uint16_t data_source_handle;
    uint16_t data_source_cccd;
    uint16_t control_point_def_handle;
    uint16_t control_point_handle;
} discovery_handles_t;

typedef struct {
    ancs_client_config_t config;
    ancs_parser_t parser;
    struct ble_npl_callout parser_timeout;
    struct ble_npl_callout reconnect;
    struct ble_npl_event forget_event;
    struct ble_npl_event bond_restart_event;
    discovery_handles_t gatt;
    discovery_handles_t ancs;
    ancs_transport_t transport;
    ancs_bond_reset_t bond_reset;
    uint16_t conn_handle;
    uint8_t own_addr_type;
    uint8_t descriptor_step;
    uint8_t subscription_step;
    ancs_client_state_t state;
    bool started;
    bool synced;
    bool state_valid;
    atomic_bool bond_reset_pending;
} ancs_client_context_t;

static ancs_client_context_t s_client = {
    .conn_handle = BLE_HS_CONN_HANDLE_NONE,
};

void ble_store_config_init(void);

static int gap_event(struct ble_gap_event *event, void *arg);
static void start_next_request(void);
static void discover_ancs_service(void);

static uint32_t now_ms(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000LL);
}

static uint32_t read_le32(const uint8_t *value)
{
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) |
           ((uint32_t)value[3] << 24U);
}

static void emit_state(ancs_client_state_t state)
{
    if (s_client.state_valid && s_client.state == state) {
        return;
    }
    s_client.state = state;
    s_client.state_valid = true;
    if (s_client.config.on_state != NULL) {
        s_client.config.on_state(state, s_client.config.ctx);
    }
}

static void finish_bond_reset(bool success)
{
    const ancs_bond_result_t result =
        ancs_bond_reset_finish(&s_client.bond_reset, success);
    const bool was_pending = atomic_exchange_explicit(
        &s_client.bond_reset_pending, false, memory_order_acq_rel);
    if (!was_pending) {
        return;
    }

    if (s_client.config.on_bond_reset_result != NULL) {
        s_client.config.on_bond_reset_result(
            result == ANCS_BOND_RESULT_SUCCEEDED ?
                ANCS_BOND_RESET_SUCCEEDED : ANCS_BOND_RESET_FAILED,
            s_client.config.ctx);
    }
}

static void reset_requests(void)
{
    const uint16_t conn_handle = s_client.conn_handle;
    const uint32_t active_uid = ancs_transport_active_uid(&s_client.transport);

    ble_npl_callout_stop(&s_client.parser_timeout);
    if (ancs_transport_has_active(&s_client.transport)) {
        ancs_parser_cancel(&s_client.parser, active_uid);
    }
    ancs_transport_disconnected(&s_client.transport);
    if (conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        ancs_transport_connected(&s_client.transport, conn_handle);
    }
}

static void reset_discovery(void)
{
    memset(&s_client.gatt, 0, sizeof(s_client.gatt));
    memset(&s_client.ancs, 0, sizeof(s_client.ancs));
    s_client.descriptor_step = 0U;
    s_client.subscription_step = 0U;
}

static uint16_t ancs_characteristic_end(uint16_t value_handle)
{
    uint16_t end_handle = s_client.ancs.end_handle;
    const uint16_t definitions[] = {
        s_client.ancs.notification_source_def_handle,
        s_client.ancs.data_source_def_handle,
        s_client.ancs.control_point_def_handle,
    };

    for (size_t index = 0U; index < sizeof(definitions) / sizeof(definitions[0]);
         ++index) {
        if (definitions[index] > value_handle &&
            (uint16_t)(definitions[index] - 1U) < end_handle) {
            end_handle = (uint16_t)(definitions[index] - 1U);
        }
    }
    return end_handle;
}

static void fail_connection(const char *operation, int rc)
{
    ESP_LOGE(TAG, "%s failed: rc=%d", operation, rc);
    emit_state(ANCS_CLIENT_ERROR);
    if (s_client.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        (void)ble_gap_terminate(s_client.conn_handle, BLE_ERR_REM_USER_CONN_TERM);
    }
}

static void parser_notification(const notification_t *notification, void *ctx)
{
    (void)ctx;
    if (s_client.config.on_notification != NULL) {
        s_client.config.on_notification(notification, s_client.config.ctx);
    }
}

static int request_write_complete(uint16_t conn_handle,
                                  const struct ble_gatt_error *error,
                                  struct ble_gatt_attr *attr, void *arg)
{
    (void)attr;
    const uint32_t generation = (uint32_t)(uintptr_t)arg;
    const ancs_write_result_t result = ancs_transport_write_complete(
        &s_client.transport, conn_handle, generation, error->status == 0);

    if (result == ANCS_WRITE_STALE) {
        return 0;
    }
    if (result == ANCS_WRITE_ADVANCE) {
        start_next_request();
    } else if (result == ANCS_WRITE_ERROR) {
        fail_connection("ANCS control point write", error->status);
    } else {
        const uint32_t uid = ancs_transport_active_uid(&s_client.transport);

        ancs_parser_expect(&s_client.parser, uid, now_ms());
        const int rc = ble_npl_callout_reset(
            &s_client.parser_timeout,
            ble_npl_time_ms_to_ticks32(ANCS_PARSER_TIMEOUT_MS));
        if (rc != 0) {
            ancs_parser_cancel(&s_client.parser, uid);
            (void)ancs_transport_cancel_active(&s_client.transport);
            fail_connection("parser timeout scheduling", rc);
        }
    }
    return 0;
}

static void start_next_request(void)
{
    ancs_write_token_t token;
    uint8_t command[15];
    size_t offset = 0U;
    int rc;

    if (s_client.ancs.control_point_handle == 0U ||
        !ancs_transport_begin_write(&s_client.transport, &token)) {
        return;
    }

    command[offset++] = ANCS_COMMAND_GET_NOTIFICATION_ATTRIBUTES;
    command[offset++] = (uint8_t)(token.uid & 0xffU);
    command[offset++] = (uint8_t)((token.uid >> 8U) & 0xffU);
    command[offset++] = (uint8_t)((token.uid >> 16U) & 0xffU);
    command[offset++] = (uint8_t)((token.uid >> 24U) & 0xffU);
    command[offset++] = ANCS_ATTRIBUTE_APP_IDENTIFIER;
    command[offset++] = ANCS_ATTRIBUTE_TITLE;
    command[offset++] = (uint8_t)((NOTIFICATION_TITLE_MAX - 1U) & 0xffU);
    command[offset++] = (uint8_t)((NOTIFICATION_TITLE_MAX - 1U) >> 8U);
    command[offset++] = ANCS_ATTRIBUTE_SUBTITLE;
    command[offset++] = (uint8_t)((NOTIFICATION_SUBTITLE_MAX - 1U) & 0xffU);
    command[offset++] = (uint8_t)((NOTIFICATION_SUBTITLE_MAX - 1U) >> 8U);
    command[offset++] = ANCS_ATTRIBUTE_MESSAGE;
    command[offset++] = (uint8_t)((NOTIFICATION_MESSAGE_MAX - 1U) & 0xffU);
    command[offset++] = (uint8_t)((NOTIFICATION_MESSAGE_MAX - 1U) >> 8U);

    rc = ble_gattc_write_flat(token.conn_handle,
                              s_client.ancs.control_point_handle,
                              command, (uint16_t)offset,
                              request_write_complete,
                              (void *)(uintptr_t)token.generation);
    if (rc != 0) {
        (void)ancs_transport_write_complete(&s_client.transport,
                                            token.conn_handle,
                                            token.generation, false);
        fail_connection("ANCS control point write start", rc);
    }
}

static void abort_active_request_and_advance(void)
{
    const uint32_t uid = ancs_transport_active_uid(&s_client.transport);

    if (!ancs_transport_has_active(&s_client.transport)) {
        return;
    }
    ble_npl_callout_stop(&s_client.parser_timeout);
    ancs_parser_cancel(&s_client.parser, uid);
    if (ancs_transport_cancel_active(&s_client.transport)) {
        start_next_request();
    }
}

static void parser_timeout_event(struct ble_npl_event *event)
{
    (void)event;
    const uint32_t uid = ancs_transport_active_uid(&s_client.transport);

    if (!ancs_transport_has_active(&s_client.transport)) {
        return;
    }
    ancs_parser_expire(&s_client.parser, now_ms());
    if (!ancs_parser_is_pending(&s_client.parser)) {
        ESP_LOGW(TAG, "attribute response timed out for UID %" PRIu32,
                 uid);
        abort_active_request_and_advance();
    }
}

static int subscribe_write_complete(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    struct ble_gatt_attr *attr, void *arg);

static void write_subscription(uint16_t handle, bool indicate)
{
    const uint8_t value[2] = {indicate ? 0U : 1U, indicate ? 2U : 0U};
    const int rc = ble_gattc_write_flat(s_client.conn_handle, handle,
                                        value, sizeof(value),
                                        subscribe_write_complete, NULL);
    if (rc != 0) {
        fail_connection("CCCD write start", rc);
    }
}

static int ancs_descriptor_discovered(uint16_t conn_handle,
                                      const struct ble_gatt_error *error,
                                      uint16_t chr_val_handle,
                                      const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (dsc != NULL &&
            ble_uuid_cmp(&dsc->uuid.u,
                         BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            if (chr_val_handle == s_client.ancs.notification_source_handle) {
                s_client.ancs.notification_source_cccd = dsc->handle;
            } else if (chr_val_handle == s_client.ancs.data_source_handle) {
                s_client.ancs.data_source_cccd = dsc->handle;
            }
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("ANCS descriptor discovery", error->status);
        return 0;
    }
    if (s_client.descriptor_step == 1U) {
        if (s_client.ancs.notification_source_cccd == 0U) {
            fail_connection("Notification Source CCCD discovery", BLE_HS_ENOENT);
            return 0;
        }
        s_client.descriptor_step = 2U;
        const int rc = ble_gattc_disc_all_dscs(
            s_client.conn_handle, s_client.ancs.data_source_handle,
            ancs_characteristic_end(s_client.ancs.data_source_handle),
            ancs_descriptor_discovered, NULL);
        if (rc != 0) {
            fail_connection("Data Source descriptor discovery start", rc);
        }
    } else {
        if (s_client.ancs.data_source_cccd == 0U) {
            fail_connection("Data Source CCCD discovery", BLE_HS_ENOENT);
            return 0;
        }
        s_client.subscription_step = 1U;
        write_subscription(s_client.ancs.notification_source_cccd, false);
    }
    return 0;
}

static int ancs_characteristic_discovered(uint16_t conn_handle,
                                          const struct ble_gatt_error *error,
                                          const struct ble_gatt_chr *chr,
                                          void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (chr == NULL) {
            return 0;
        }
        if (ble_uuid_cmp(&chr->uuid.u, &ANCS_NOTIFICATION_SOURCE_UUID.u) == 0 &&
            (chr->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0U) {
            s_client.ancs.notification_source_def_handle = chr->def_handle;
            s_client.ancs.notification_source_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &ANCS_DATA_SOURCE_UUID.u) == 0 &&
                   (chr->properties & BLE_GATT_CHR_PROP_NOTIFY) != 0U) {
            s_client.ancs.data_source_def_handle = chr->def_handle;
            s_client.ancs.data_source_handle = chr->val_handle;
        } else if (ble_uuid_cmp(&chr->uuid.u, &ANCS_CONTROL_POINT_UUID.u) == 0 &&
                   (chr->properties & BLE_GATT_CHR_PROP_WRITE) != 0U) {
            s_client.ancs.control_point_def_handle = chr->def_handle;
            s_client.ancs.control_point_handle = chr->val_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("ANCS characteristic discovery", error->status);
        return 0;
    }
    if (s_client.ancs.notification_source_handle == 0U ||
        s_client.ancs.data_source_handle == 0U ||
        s_client.ancs.control_point_handle == 0U) {
        fail_connection("required ANCS characteristic discovery", BLE_HS_ENOENT);
        return 0;
    }
    s_client.descriptor_step = 1U;
    const int rc = ble_gattc_disc_all_dscs(
        s_client.conn_handle, s_client.ancs.notification_source_handle,
        ancs_characteristic_end(s_client.ancs.notification_source_handle),
        ancs_descriptor_discovered, NULL);
    if (rc != 0) {
        fail_connection("ANCS descriptor discovery start", rc);
    }
    return 0;
}

static int ancs_service_discovered(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (svc != NULL) {
            s_client.ancs.start_handle = svc->start_handle;
            s_client.ancs.end_handle = svc->end_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("ANCS service discovery", error->status);
        return 0;
    }
    if (s_client.ancs.start_handle == 0U) {
        fail_connection("ANCS service discovery", BLE_HS_ENOENT);
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(s_client.conn_handle,
                                            s_client.ancs.start_handle,
                                            s_client.ancs.end_handle,
                                            ancs_characteristic_discovered,
                                            NULL);
    if (rc != 0) {
        fail_connection("ANCS characteristic discovery start", rc);
    }
    return 0;
}

static void discover_ancs_service(void)
{
    memset(&s_client.ancs, 0, sizeof(s_client.ancs));
    reset_requests();
    const int rc = ble_gattc_disc_svc_by_uuid(s_client.conn_handle,
                                               &ANCS_SERVICE_UUID.u,
                                               ancs_service_discovered, NULL);
    if (rc != 0) {
        fail_connection("ANCS service discovery start", rc);
    }
}

static int service_changed_descriptor_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    uint16_t chr_val_handle, const struct ble_gatt_dsc *dsc, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (dsc != NULL && chr_val_handle == s_client.gatt.service_changed_handle &&
            ble_uuid_cmp(&dsc->uuid.u,
                         BLE_UUID16_DECLARE(BLE_GATT_DSC_CLT_CFG_UUID16)) == 0) {
            s_client.gatt.service_changed_cccd = dsc->handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("Service Changed descriptor discovery", error->status);
        return 0;
    }
    if (s_client.gatt.service_changed_cccd == 0U) {
        fail_connection("Service Changed CCCD discovery", BLE_HS_ENOENT);
        return 0;
    }
    s_client.subscription_step = 0U;
    write_subscription(s_client.gatt.service_changed_cccd, true);
    return 0;
}

static int service_changed_characteristic_discovered(
    uint16_t conn_handle, const struct ble_gatt_error *error,
    const struct ble_gatt_chr *chr, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (chr != NULL) {
            if (ble_uuid_cmp(&chr->uuid.u,
                             BLE_UUID16_DECLARE(GATT_SERVICE_CHANGED_UUID)) == 0 &&
                (chr->properties & BLE_GATT_CHR_PROP_INDICATE) != 0U) {
                s_client.gatt.service_changed_handle = chr->val_handle;
                s_client.gatt.service_changed_end_handle =
                    s_client.gatt.end_handle;
            } else if (s_client.gatt.service_changed_handle != 0U &&
                       chr->def_handle > s_client.gatt.service_changed_handle &&
                       (uint16_t)(chr->def_handle - 1U) <
                           s_client.gatt.service_changed_end_handle) {
                s_client.gatt.service_changed_end_handle =
                    (uint16_t)(chr->def_handle - 1U);
            }
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("Service Changed characteristic discovery", error->status);
        return 0;
    }
    if (s_client.gatt.service_changed_handle == 0U) {
        fail_connection("Service Changed characteristic discovery", BLE_HS_ENOENT);
        return 0;
    }
    const int rc = ble_gattc_disc_all_dscs(s_client.conn_handle,
                                            s_client.gatt.service_changed_handle,
                                            s_client.gatt.service_changed_end_handle,
                                            service_changed_descriptor_discovered,
                                            NULL);
    if (rc != 0) {
        fail_connection("Service Changed descriptor discovery start", rc);
    }
    return 0;
}

static int gatt_service_discovered(uint16_t conn_handle,
                                   const struct ble_gatt_error *error,
                                   const struct ble_gatt_svc *svc, void *arg)
{
    (void)conn_handle;
    (void)arg;
    if (error->status == 0) {
        if (svc != NULL) {
            s_client.gatt.start_handle = svc->start_handle;
            s_client.gatt.end_handle = svc->end_handle;
        }
        return 0;
    }
    if (error->status != BLE_HS_EDONE) {
        fail_connection("GATT service discovery", error->status);
        return 0;
    }
    if (s_client.gatt.start_handle == 0U) {
        fail_connection("GATT service discovery", BLE_HS_ENOENT);
        return 0;
    }
    const int rc = ble_gattc_disc_all_chrs(
        s_client.conn_handle, s_client.gatt.start_handle, s_client.gatt.end_handle,
        service_changed_characteristic_discovered, NULL);
    if (rc != 0) {
        fail_connection("Service Changed characteristic discovery start", rc);
    }
    return 0;
}

static void begin_discovery(void)
{
    reset_discovery();
    const int rc = ble_gattc_disc_svc_by_uuid(
        s_client.conn_handle, BLE_UUID16_DECLARE(GATT_SERVICE_UUID),
        gatt_service_discovered, NULL);
    if (rc != 0) {
        fail_connection("GATT service discovery start", rc);
    }
}

static int subscribe_write_complete(uint16_t conn_handle,
                                    const struct ble_gatt_error *error,
                                    struct ble_gatt_attr *attr, void *arg)
{
    (void)conn_handle;
    (void)attr;
    (void)arg;
    const ancs_subscription_action_t action =
        ancs_subscription_decide(s_client.subscription_step, error->status);
    if (action == ANCS_SUBSCRIPTION_FAIL) {
        fail_connection("CCCD write", error->status);
        return 0;
    }
    if (action == ANCS_SUBSCRIPTION_DISCOVER_ANCS) {
        if (error->status != 0U) {
            ESP_LOGW(TAG,
                     "Service Changed CCCD rejected: rc=%u; continuing without it",
                     error->status);
        }
        discover_ancs_service();
    } else if (action == ANCS_SUBSCRIPTION_SUBSCRIBE_DATA_SOURCE) {
        s_client.subscription_step = 2U;
        write_subscription(s_client.ancs.data_source_cccd, false);
    } else {
        ancs_transport_subscriptions_ready(&s_client.transport);
        emit_state(ANCS_CLIENT_CONNECTED);
        start_next_request();
    }
    return 0;
}

static void handle_notification_source(const struct os_mbuf *om)
{
    uint8_t packet[ANCS_NOTIFICATION_SOURCE_LENGTH];
    if (om == NULL || OS_MBUF_PKTLEN(om) != sizeof(packet) ||
        os_mbuf_copydata(om, 0, sizeof(packet), packet) != 0) {
        ESP_LOGW(TAG, "discarding malformed Notification Source packet");
        return;
    }

    const uint32_t uid = read_le32(&packet[4]);
    if (packet[0] == ANCS_EVENT_REMOVED) {
        if (ancs_transport_active_uid(&s_client.transport) == uid) {
            ble_npl_callout_stop(&s_client.parser_timeout);
            ancs_parser_cancel(&s_client.parser, uid);
        }
        if (ancs_transport_cancel_uid(&s_client.transport, uid)) {
            start_next_request();
        }
    } else if (packet[0] == ANCS_EVENT_ADDED ||
               packet[0] == ANCS_EVENT_MODIFIED) {
        if (!ancs_transport_enqueue(&s_client.transport, uid)) {
            ESP_LOGE(TAG, "notification request queue overflow at UID %" PRIu32,
                     uid);
            fail_connection("ANCS request queue overflow", BLE_HS_ENOMEM);
            return;
        }
        start_next_request();
    }
}

static void handle_data_source(const struct os_mbuf *om)
{
    uint8_t fragment[32];
    int offset = 0;
    const int length = om == NULL ? 0 : OS_MBUF_PKTLEN(om);

    if (length <= 0) {
        ESP_LOGW(TAG, "discarding empty Data Source mbuf");
        abort_active_request_and_advance();
        return;
    }

    while (offset < length &&
           ancs_transport_response_waiting(&s_client.transport)) {
        const int remaining = length - offset;
        const int chunk = remaining < (int)sizeof(fragment) ? remaining :
                                                                    (int)sizeof(fragment);
        if (os_mbuf_copydata(om, offset, chunk, fragment) != 0) {
            ESP_LOGW(TAG, "discarding malformed Data Source mbuf");
            abort_active_request_and_advance();
            return;
        }
        ancs_parser_feed(&s_client.parser, fragment, (size_t)chunk, now_ms());
        offset += chunk;
        if (!ancs_parser_is_pending(&s_client.parser)) {
            break;
        }
    }
    if (ancs_transport_response_waiting(&s_client.transport) &&
        !ancs_parser_is_pending(&s_client.parser) &&
        ancs_transport_finish_response(&s_client.transport)) {
        ble_npl_callout_stop(&s_client.parser_timeout);
        start_next_request();
    }
}

static void handle_service_changed(const struct os_mbuf *om)
{
    uint8_t value[4];
    ancs_handle_range_t range;
    const int length = om == NULL ? 0 : OS_MBUF_PKTLEN(om);
    ancs_service_changed_decision_t decision;

    if (length != (int)sizeof(value) ||
        os_mbuf_copydata(om, 0, sizeof(value), value) != 0) {
        decision = ANCS_SERVICE_CHANGED_RECOVER;
    } else {
        decision = ancs_service_changed_decide(value, sizeof(value), &range);
    }

    reset_requests();
    reset_discovery();
    if (decision == ANCS_SERVICE_CHANGED_REDISCOVER) {
        ESP_LOGI(TAG, "Service Changed handles 0x%04x-0x%04x; reconnecting",
                 range.start_handle, range.end_handle);
        emit_state(ANCS_CLIENT_RECONNECTING);
    } else {
        ESP_LOGE(TAG, "malformed Service Changed indication");
        emit_state(ANCS_CLIENT_ERROR);
    }
    if (s_client.conn_handle != BLE_HS_CONN_HANDLE_NONE) {
        const int rc = ble_gap_terminate(s_client.conn_handle,
                                         BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            fail_connection("Service Changed recovery disconnect", rc);
        }
    }
}

static int advertise(void)
{
    struct ble_hs_adv_fields fields = {0};
    struct ble_gap_adv_params params = {0};
    const ancs_advertising_policy_t policy = ancs_advertising_policy();
    ble_uuid16_t advertised_service = BLE_UUID16_INIT(0);
    const char *name = ble_svc_gap_device_name();
    int rc;

    if (ble_gap_adv_active()) {
        return 0;
    }
    fields.flags = BLE_HS_ADV_F_DISC_GEN | BLE_HS_ADV_F_BREDR_UNSUP;
    fields.name = (const uint8_t *)name;
    fields.name_len = (uint8_t)strlen(name);
    fields.name_is_complete = 1U;
    if (policy.include_tx_power) {
        fields.tx_pwr_lvl_is_present = 1U;
        fields.tx_pwr_lvl = BLE_HS_ADV_TX_PWR_LVL_AUTO;
    }
    advertised_service.value = policy.service_uuid16;
    fields.uuids16 = &advertised_service;
    fields.num_uuids16 = 1U;
    fields.uuids16_is_complete = 1U;
    if (policy.include_ancs_solicitation) {
        fields.sol_uuids128 = &ANCS_SERVICE_UUID;
        fields.sol_num_uuids128 = 1U;
    }
    rc = ble_gap_adv_set_fields(&fields);
    if (rc != 0) {
        return rc;
    }
    params.conn_mode = BLE_GAP_CONN_MODE_UND;
    params.disc_mode = BLE_GAP_DISC_MODE_GEN;
    return ble_gap_adv_start(s_client.own_addr_type, NULL, BLE_HS_FOREVER,
                             &params, gap_event, NULL);
}

static void schedule_reconnect(void)
{
    const uint32_t delay =
        ancs_transport_take_reconnect_delay(&s_client.transport);

    emit_state(ANCS_CLIENT_RECONNECTING);
    const int rc = ble_npl_callout_reset(
        &s_client.reconnect,
        ble_npl_time_ms_to_ticks32(delay));
    if (rc != 0) {
        fail_connection("reconnect scheduling", rc);
        return;
    }
}

static void reconnect_event(struct ble_npl_event *event)
{
    (void)event;
    const int rc = advertise();
    if (rc != 0) {
        ESP_LOGW(TAG, "advertising restart failed: rc=%d", rc);
        schedule_reconnect();
    }
}

static void bond_restart_event(struct ble_npl_event *event)
{
    (void)event;
    if (ancs_bond_reset_restart(&s_client.bond_reset) !=
        ANCS_BOND_ACTION_ADVERTISE) {
        return;
    }
    const int rc = advertise();
    if (rc != 0) {
        finish_bond_reset(false);
        fail_connection("pairing advertising start", rc);
        return;
    }
    finish_bond_reset(true);
    emit_state(ANCS_CLIENT_PAIRING);
}

static ancs_peer_identity_t peer_identity(const ble_addr_t *address)
{
    ancs_peer_identity_t identity = {.type = address->type};
    memcpy(identity.value, address->val, sizeof(identity.value));
    return identity;
}

static int unpair_peer(const ancs_peer_identity_t *peer, void *context)
{
    ble_addr_t address = {.type = peer->type};

    (void)context;
    memcpy(address.val, peer->value, sizeof(address.val));
    return ble_gap_unpair(&address);
}

static int unpair_bonded_peers(void)
{
    ble_addr_t stored_peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    ancs_peer_identity_t bonded_peers[CONFIG_BT_NIMBLE_MAX_BONDS];
    int bonded_peer_count = 0;
    int rc = ble_store_util_bonded_peers(
        stored_peers, &bonded_peer_count, CONFIG_BT_NIMBLE_MAX_BONDS);
    if (rc != 0) {
        return rc;
    }
    if (bonded_peer_count < 0 ||
        bonded_peer_count > CONFIG_BT_NIMBLE_MAX_BONDS) {
        return BLE_HS_EINVAL;
    }
    for (int index = 0; index < bonded_peer_count; ++index) {
        bonded_peers[index] = peer_identity(&stored_peers[index]);
    }
    return ancs_unpair_peers(bonded_peers, (size_t)bonded_peer_count,
                             unpair_peer, NULL);
}

static void clear_bonds_and_queue_restart(void)
{
    ancs_bond_action_t action;

    reset_requests();
    reset_discovery();
    const int rc = unpair_bonded_peers();
    action = ancs_bond_reset_cleared(&s_client.bond_reset, rc == 0);
    if (rc != 0) {
        finish_bond_reset(false);
        fail_connection("bond unpair", rc);
        return;
    }
    if (action == ANCS_BOND_ACTION_QUEUE_RESTART) {
        ble_npl_eventq_put(nimble_port_get_dflt_eventq(),
                           &s_client.bond_restart_event);
    }
}

static bool connected_peer_is_admitted(uint16_t conn_handle)
{
    ble_addr_t bonded_addresses[1];
    ancs_peer_identity_t bonded_peers[1];
    struct ble_gap_conn_desc desc;
    int bonded_peer_count = 0;
    int rc = ble_store_util_bonded_peers(bonded_addresses, &bonded_peer_count,
                                         1);
    if (rc != 0) {
        ESP_LOGE(TAG, "bond lookup for peer admission failed: rc=%d", rc);
        return false;
    }
    rc = ble_gap_conn_find(conn_handle, &desc);
    if (rc != 0) {
        ESP_LOGE(TAG, "resolved peer identity lookup failed: rc=%d", rc);
        return false;
    }
    for (int index = 0; index < bonded_peer_count; ++index) {
        bonded_peers[index] = peer_identity(&bonded_addresses[index]);
    }
    const ancs_peer_identity_t connected = peer_identity(&desc.peer_id_addr);
    const ancs_peer_admission_t admission = ancs_peer_admission_decide(
        bonded_peers, (size_t)bonded_peer_count, &connected);
    if (admission == ANCS_PEER_REJECT_UNKNOWN) {
        ESP_LOGW(TAG, "rejecting connection from unknown identity while bonded");
        return false;
    }
    return true;
}

static void forget_event(struct ble_npl_event *event)
{
    ancs_bond_action_t action;

    (void)event;
    if (!atomic_load_explicit(&s_client.bond_reset_pending,
                              memory_order_acquire)) {
        return;
    }
    ble_npl_callout_stop(&s_client.reconnect);
    action = ancs_bond_reset_request(
        &s_client.bond_reset,
        s_client.conn_handle != BLE_HS_CONN_HANDLE_NONE,
        ble_gap_adv_active());
    if (action == ANCS_BOND_ACTION_DISCONNECT) {
        const int rc = ble_gap_terminate(s_client.conn_handle,
                                         BLE_ERR_REM_USER_CONN_TERM);
        if (rc != 0) {
            finish_bond_reset(false);
            fail_connection("disconnect for bond reset", rc);
        }
        return;
    }
    if (action == ANCS_BOND_ACTION_STOP_ADVERTISING) {
        const int rc = ble_gap_adv_stop();
        if (rc != 0) {
            finish_bond_reset(false);
            fail_connection("advertising stop for bond reset", rc);
        }
        return;
    }
    if (action == ANCS_BOND_ACTION_CLEAR_BONDS) {
        clear_bonds_and_queue_restart();
    }
}

static int gap_event(struct ble_gap_event *event, void *arg)
{
    (void)arg;
    int rc;

    switch (event->type) {
    case BLE_GAP_EVENT_CONNECT:
        if (event->connect.status != 0) {
            schedule_reconnect();
            return 0;
        }
        if (!connected_peer_is_admitted(event->connect.conn_handle)) {
            rc = ble_gap_terminate(event->connect.conn_handle,
                                   BLE_ERR_REM_USER_CONN_TERM);
            if (rc != 0) {
                ESP_LOGE(TAG, "unadmitted peer termination failed: rc=%d", rc);
                emit_state(ANCS_CLIENT_ERROR);
            }
            return 0;
        }
        s_client.conn_handle = event->connect.conn_handle;
        ancs_transport_connected(&s_client.transport, s_client.conn_handle);
        rc = ble_gap_security_initiate(s_client.conn_handle);
        if (rc != 0) {
            fail_connection("security initiation", rc);
        }
        return 0;

    case BLE_GAP_EVENT_DISCONNECT:
        s_client.conn_handle = BLE_HS_CONN_HANDLE_NONE;
        reset_requests();
        reset_discovery();
        if (ancs_bond_reset_link_stopped(&s_client.bond_reset) ==
            ANCS_BOND_ACTION_CLEAR_BONDS) {
            clear_bonds_and_queue_restart();
        } else {
            schedule_reconnect();
        }
        return 0;

    case BLE_GAP_EVENT_ADV_COMPLETE:
        if (ancs_bond_reset_link_stopped(&s_client.bond_reset) ==
            ANCS_BOND_ACTION_CLEAR_BONDS) {
            clear_bonds_and_queue_restart();
            return 0;
        }
        if (s_client.conn_handle == BLE_HS_CONN_HANDLE_NONE) {
            schedule_reconnect();
        }
        return 0;

    case BLE_GAP_EVENT_ENC_CHANGE:
        if (event->enc_change.status != 0) {
            fail_connection("link encryption", event->enc_change.status);
            return 0;
        }
        begin_discovery();
        return 0;

    case BLE_GAP_EVENT_NOTIFY_RX:
        if (event->notify_rx.attr_handle ==
            s_client.ancs.notification_source_handle) {
            handle_notification_source(event->notify_rx.om);
        } else if (event->notify_rx.attr_handle ==
                   s_client.ancs.data_source_handle) {
            handle_data_source(event->notify_rx.om);
        } else if (event->notify_rx.attr_handle ==
                   s_client.gatt.service_changed_handle) {
            handle_service_changed(event->notify_rx.om);
        }
        return 0;

    case BLE_GAP_EVENT_REPEAT_PAIRING:
        ESP_LOGW(TAG, "repeat pairing refused; hold B for 5 seconds to clear bond");
        return BLE_GAP_REPEAT_PAIRING_IGNORE;

    case BLE_GAP_EVENT_PASSKEY_ACTION:
        ESP_LOGW(TAG, "unexpected passkey action %u",
                 event->passkey.params.action);
        return 0;

    default:
        return 0;
    }
}

static void host_reset(int reason)
{
    ESP_LOGE(TAG, "NimBLE host reset: reason=%d", reason);
    s_client.synced = false;
    s_client.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    reset_requests();
    reset_discovery();
    finish_bond_reset(false);
    emit_state(ANCS_CLIENT_ERROR);
}

static void host_sync(void)
{
    ble_addr_t peers[1];
    int peer_count = 0;
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        fail_connection("identity address creation", rc);
        return;
    }
    rc = ble_hs_id_infer_auto(0, &s_client.own_addr_type);
    if (rc != 0) {
        fail_connection("identity address selection", rc);
        return;
    }
    rc = ble_store_util_bonded_peers(peers, &peer_count, 1);
    if (rc != 0) {
        fail_connection("bond lookup", rc);
        return;
    }
    s_client.synced = true;
    emit_state(peer_count == 0 ? ANCS_CLIENT_PAIRING :
                                 ANCS_CLIENT_RECONNECTING);
    rc = advertise();
    if (rc != 0) {
        fail_connection("advertising start", rc);
    }
}

static void host_task(void *param)
{
    (void)param;
    nimble_port_run();
    nimble_port_freertos_deinit();
}

esp_err_t ancs_client_start(const ancs_client_config_t *config)
{
    esp_err_t err;
    int rc;

    if (config == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_client.started) {
        return ESP_ERR_INVALID_STATE;
    }

    err = nvs_flash_init();
    if (err != ESP_OK) {
        return err;
    }
    err = nimble_port_init();
    if (err != ESP_OK) {
        return err;
    }

    s_client.config = *config;
    s_client.conn_handle = BLE_HS_CONN_HANDLE_NONE;
    ancs_transport_init(&s_client.transport);
    ancs_bond_reset_init(&s_client.bond_reset);
    atomic_init(&s_client.bond_reset_pending, false);
    ancs_parser_init(&s_client.parser, parser_notification, NULL);
    ble_hs_cfg.reset_cb = host_reset;
    ble_hs_cfg.sync_cb = host_sync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;
    ble_hs_cfg.sm_bonding = 1;
    ble_hs_cfg.sm_sc = 1;
    ble_hs_cfg.sm_mitm = 0;
    ble_hs_cfg.sm_io_cap = BLE_HS_IO_NO_INPUT_OUTPUT;
    ble_hs_cfg.sm_our_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                 BLE_SM_PAIR_KEY_DIST_ID;
    ble_hs_cfg.sm_their_key_dist = BLE_SM_PAIR_KEY_DIST_ENC |
                                   BLE_SM_PAIR_KEY_DIST_ID;

    rc = ble_svc_gap_device_name_set(ANCS_DEVICE_NAME);
    if (rc != 0) {
        nimble_port_deinit();
        return ESP_FAIL;
    }
    ble_store_config_init();
    ble_npl_callout_init(&s_client.parser_timeout,
                         nimble_port_get_dflt_eventq(), parser_timeout_event,
                         NULL);
    ble_npl_callout_init(&s_client.reconnect, nimble_port_get_dflt_eventq(),
                         reconnect_event, NULL);
    ble_npl_event_init(&s_client.forget_event, forget_event, NULL);
    ble_npl_event_init(&s_client.bond_restart_event, bond_restart_event, NULL);
    s_client.started = true;
    nimble_port_freertos_init(host_task);
    return ESP_OK;
}

esp_err_t ancs_client_forget_bond(void)
{
    if (!s_client.started || !s_client.synced) {
        return ESP_ERR_INVALID_STATE;
    }
    bool expected = false;
    if (!atomic_compare_exchange_strong_explicit(
            &s_client.bond_reset_pending, &expected, true,
            memory_order_acq_rel, memory_order_acquire)) {
        return ESP_ERR_INVALID_STATE;
    }
    ble_npl_eventq_put(nimble_port_get_dflt_eventq(), &s_client.forget_event);
    return ESP_OK;
}
