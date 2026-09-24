#include "ble_midi.h"

#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "nvs_flash.h"
#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "host/util/util.h"
#include "services/gap/ble_svc_gap.h"
#include "board_config.h"

namespace pocketpan::hardware {

namespace {
constexpr const char* kTag = "BleMidi";

// Standard MIDI Service UUID: 03B80E5A-EDE8-4B33-A751-6CE34EC4C700 (128-bit)
const ble_uuid128_t kMidiServiceUuid = BLE_UUID128_INIT(
    0x00, 0xc7, 0xc4, 0x4e, 0xe3, 0x6c, 0x51, 0xa7,
    0x33, 0x4b, 0xe8, 0xed, 0x5a, 0x0e, 0xb8, 0x03
);

// Standard MIDI Characteristic UUID: 7772E5DB-3868-4112-A1A9-F2669D106BF3 (128-bit)
// Note: byte 4 from MSB is 0x66 (little-endian index 4: 0xf3, 0x6b, 0x10, 0x9d, 0x66...)
const ble_uuid128_t kMidiCharUuid = BLE_UUID128_INIT(
    0xf3, 0x6b, 0x10, 0x9d, 0x66, 0xf2, 0xa9, 0xa1,
    0x12, 0x41, 0x68, 0x38, 0xdb, 0xe5, 0x72, 0x77
);

BleMidi* sInstance = nullptr;
uint16_t sConnHandle = BLE_HS_CONN_HANDLE_NONE;
uint16_t sMidiValHandle = 0;
uint16_t sMidiServiceEndHandle = 0;
bool sCccdFound = false;
bool sServiceFound = false;
bool sCharacteristicFound = false;

int bleGapEvent(struct ble_gap_event* event, void* arg);
void startScan();

// Descriptor write callback after subscribing to notifications
int onDscWrite(uint16_t conn_handle, const struct ble_gatt_error* error,
               struct ble_gatt_attr* attr, void* arg) {
    if (error->status == 0) {
        ESP_LOGI(kTag, "CCCD subscription successful; BLE MIDI ready");
        if (sInstance) sInstance->setState(BleMidiState::Ready);
    } else {
        if (sInstance) sInstance->failAndRecover("CCCD subscription failed");
    }
    return 0;
}

// Descriptor discovery callback: explicitly discovers 0x2902 CCCD
int onDscDiscovery(uint16_t conn_handle, const struct ble_gatt_error* error,
                   uint16_t chr_val_handle, const struct ble_gatt_dsc* dsc, void* arg) {
    if (error->status == 0 && dsc) {
        if (ble_uuid_u16(&dsc->uuid.u) == board::ble::kCccdUuid16) {
            ESP_LOGI(kTag, "Discovered CCCD (0x2902) handle=%d", dsc->handle);
            sCccdFound = true;
            if (sInstance) sInstance->setState(BleMidiState::Subscribing);
            uint8_t notifyEnable[2] = {0x01, 0x00};
            int rc = ble_gattc_write_flat(conn_handle, dsc->handle, notifyEnable,
                                          sizeof(notifyEnable), onDscWrite, nullptr);
            if (rc != 0) {
                if (sInstance) sInstance->failAndRecover("CCCD write could not start");
            }
        }
    } else if (error->status == BLE_HS_EDONE) {
        if (!sCccdFound) {
            ESP_LOGE(kTag, "Error: CCCD (0x2902) not found for MIDI characteristic in range [%d..%d]",
                     chr_val_handle, sMidiServiceEndHandle);
            if (sInstance) sInstance->failAndRecover("MIDI CCCD not found");
        }
    } else if (error->status != 0) {
        ESP_LOGE(kTag, "Descriptor discovery error: status=%d", error->status);
        if (sInstance) sInstance->failAndRecover("CCCD discovery failed");
    }
    return 0;
}

// Find characteristic callback
int onCharDiscovery(uint16_t conn_handle, const struct ble_gatt_error* error,
                    const struct ble_gatt_chr* chr, void* arg) {
    if (error->status == 0 && chr) {
        if (ble_uuid_cmp(&chr->uuid.u, &kMidiCharUuid.u) == 0) {
            sCharacteristicFound = true;
            if (sInstance) sInstance->setState(BleMidiState::DiscoveringCccd);
            ESP_LOGI(kTag, "Found BLE MIDI Characteristic! def_handle=%d val_handle=%d",
                     chr->def_handle, chr->val_handle);
            sMidiValHandle = chr->val_handle;
            sCccdFound = false;

            // Discover descriptors explicitly between val_handle and service end handle
            int rc = ble_gattc_disc_all_dscs(conn_handle, chr->val_handle, sMidiServiceEndHandle,
                                            onDscDiscovery, nullptr);
            if (rc != 0) {
                if (sInstance) sInstance->failAndRecover("CCCD discovery could not start");
            }
        }
    } else if (error->status == BLE_HS_EDONE && !sCharacteristicFound && sInstance) {
        sInstance->failAndRecover("MIDI characteristic not found");
    } else if (error->status != 0 && error->status != BLE_HS_EDONE && sInstance) {
        sInstance->failAndRecover("MIDI characteristic discovery failed");
    }
    return 0;
}

// Service discovery callback
int onServiceDiscovery(uint16_t conn_handle, const struct ble_gatt_error* error,
                       const struct ble_gatt_svc* service, void* arg) {
    if (error->status == 0 && service) {
        if (ble_uuid_cmp(&service->uuid.u, &kMidiServiceUuid.u) == 0) {
            sServiceFound = true;
            sCharacteristicFound = false;
            if (sInstance) sInstance->setState(BleMidiState::DiscoveringCharacteristic);
            ESP_LOGI(kTag, "Found BLE MIDI Service! start_handle=%d end_handle=%d",
                     service->start_handle, service->end_handle);
            sMidiServiceEndHandle = service->end_handle;
            // Discover characteristics within this service
            int rc = ble_gattc_disc_all_chrs(conn_handle, service->start_handle, service->end_handle,
                                             onCharDiscovery, nullptr);
            if (rc != 0 && sInstance) sInstance->failAndRecover("MIDI characteristic discovery could not start");
        }
    } else if (error->status == BLE_HS_EDONE && !sServiceFound && sInstance) {
        sInstance->failAndRecover("MIDI service not found");
    } else if (error->status != 0 && error->status != BLE_HS_EDONE && sInstance) {
        sInstance->failAndRecover("MIDI service discovery failed");
    }
    return 0;
}

int bleGapEvent(struct ble_gap_event* event, void* arg) {
    switch (event->type) {
        case BLE_GAP_EVENT_DISC: {
            struct ble_hs_adv_fields fields;
            int rc = ble_hs_adv_parse_fields(&fields, event->disc.data, event->disc.length_data);
            if (rc != 0) return 0;

            bool matchesMidi = false;

            // 1. Check if advertising payload specifies standard 128-bit MIDI Service UUID
            for (int i = 0; i < fields.num_uuids128; ++i) {
                if (ble_uuid_cmp(&fields.uuids128[i].u, &kMidiServiceUuid.u) == 0) {
                    matchesMidi = true;
                    break;
                }
            }

            // 2. Or check device name matching target M-VAVE SMC-PAD Pocket
            if (!matchesMidi && fields.name && fields.name_len > 0) {
                std::string devName(reinterpret_cast<const char*>(fields.name), fields.name_len);
                if (devName.find("SMC-PAD") != std::string::npos ||
                    devName.find("M-VAVE") != std::string::npos ||
                    devName.find("MIDI") != std::string::npos) {
                    matchesMidi = true;
                }
            }

            if (matchesMidi) {
                ESP_LOGI(kTag, "BLE MIDI device found; connecting");
                if (sInstance) sInstance->setState(BleMidiState::Connecting);
                ble_gap_disc_cancel();

                rc = ble_gap_connect(BLE_OWN_ADDR_PUBLIC, &event->disc.addr, 30000, nullptr,
                                    bleGapEvent, nullptr);
                if (rc != 0) {
                    ESP_LOGE(kTag, "ble_gap_connect failed: rc=%d", rc);
                    startScan();
                }
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONNECT: {
            if (event->connect.status == 0) {
                sConnHandle = event->connect.conn_handle;
                ESP_LOGI(kTag, "BLE Connected! conn_handle=%d", sConnHandle);
                if (sInstance) sInstance->onConnected(sConnHandle);

                struct ble_gap_upd_params params = {};
                params.itvl_min = 6;  // 7.5 ms
                params.itvl_max = 12; // 15 ms
                params.latency = 0;
                params.supervision_timeout = 200; // 2 seconds
                params.min_ce_len = 0; params.max_ce_len = 0;
                int updateRc = ble_gap_update_params(sConnHandle, &params);
                ESP_LOGI(kTag, "Requested connection params 7.5-15ms latency=0 timeout=2s rc=%d", updateRc);

                // Discover services on the peripheral
                sServiceFound = false;
                if (sInstance) sInstance->setState(BleMidiState::DiscoveringService);
                int discoverRc = ble_gattc_disc_all_svcs(sConnHandle, onServiceDiscovery, nullptr);
                if (discoverRc != 0 && sInstance) sInstance->failAndRecover("MIDI service discovery could not start");
            } else {
                ESP_LOGW(kTag, "Connection failed: status=%d", event->connect.status);
                startScan();
            }
            return 0;
        }

        case BLE_GAP_EVENT_DISCONNECT: {
            ESP_LOGI(kTag, "BLE disconnected reason=%d; retry after backoff", event->disconnect.reason);
            sConnHandle = BLE_HS_CONN_HANDLE_NONE;
            if (sInstance) { sInstance->onDisconnectReason(event->disconnect.reason); sInstance->onDisconnected(); }
            const BaseType_t retryCreated = xTaskCreatePinnedToCore(
                [](void*) { vTaskDelay(pdMS_TO_TICKS(750)); startScan(); vTaskDelete(nullptr); },
                "ble_retry", board::ble::kRetryTaskStackBytes, nullptr,
                board::ble::kRetryTaskPriority, nullptr, board::ble::kTaskCore);
            if (retryCreated != pdPASS) {
                ESP_LOGW(kTag, "Could not create BLE retry task; scanning immediately");
                startScan();
            }
            return 0;
        }

        case BLE_GAP_EVENT_CONN_UPDATE: {
            struct ble_gap_conn_desc desc;
            if (ble_gap_conn_find(event->conn_update.conn_handle, &desc) == 0) {
                ESP_LOGI(kTag, "Negotiated params interval=%.2fms latency=%u timeout=%ums",
                         desc.conn_itvl * 1.25f, desc.conn_latency, desc.supervision_timeout * 10);
                if (sInstance) sInstance->onConnectionUpdate(desc.conn_itvl, desc.conn_latency, desc.supervision_timeout);
            }
            return 0;
        }

        case BLE_GAP_EVENT_NOTIFY_RX: {
            if (event->notify_rx.attr_handle == sMidiValHandle && sInstance) {
                uint8_t buffer[64];
                uint16_t len = OS_MBUF_PKTLEN(event->notify_rx.om);
                if (len > sizeof(buffer)) len = sizeof(buffer);
                os_mbuf_copydata(event->notify_rx.om, 0, len, buffer);
                sInstance->onMidiDataReceived(buffer, len);
            }
            return 0;
        }

        default:
            break;
    }
    return 0;
}

void startScan() {
    if (sInstance) sInstance->setState(BleMidiState::Scanning);
    uint8_t own_addr_type;
    int rc = ble_hs_id_infer_auto(0, &own_addr_type);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_hs_id_infer_auto failed: %d", rc);
        return;
    }

    struct ble_gap_disc_params disc_params = {};
    disc_params.passive = 0; // Active scan to request scan response (device name)
    disc_params.filter_duplicates = 1;
    disc_params.itvl = 0x20; // 20 ms
    disc_params.window = 0x18; // 15 ms

    rc = ble_gap_disc(own_addr_type, BLE_HS_FOREVER, &disc_params, bleGapEvent, nullptr);
    if (rc != 0) {
        ESP_LOGE(kTag, "Failed to start BLE scan: %d", rc);
    } else {
        ESP_LOGI(kTag, "Scanning for BLE MIDI devices...");
    }
}

void onSync() {
    int rc = ble_hs_util_ensure_addr(0);
    if (rc != 0) {
        ESP_LOGE(kTag, "ble_hs_util_ensure_addr failed: %d", rc);
        return;
    }
    startScan();
}

} // namespace

BleMidi::BleMidi() {
    sInstance = this;
    parser_.setCallback([](void* ctx, const midi::MidiEvent& ev) {
        auto* self = static_cast<BleMidi*>(ctx);
        if (self && self->queue_) {
            self->queue_->push(ev);
        }
    }, this);
}

BleMidi::~BleMidi() {
    sInstance = nullptr;
}

void BleMidi::bleHostTaskEntry(void* param) {
    ESP_LOGI(kTag, "NimBLE Host task running on Core %d", board::ble::kTaskCore);
    nimble_port_run();
    nimble_port_freertos_deinit();
}

bool BleMidi::begin() {
    // 1. Initialize NVS (required for Bluetooth storage)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    // 2. Initialize NimBLE
    ret = nimble_port_init();
    if (ret != ESP_OK) {
        ESP_LOGE(kTag, "Failed to init NimBLE port: %s", esp_err_to_name(ret));
        return false;
    }

    ble_hs_cfg.sync_cb = onSync;
    ble_hs_cfg.store_status_cb = ble_store_util_status_rr;

    // 3. Pin NimBLE Host task strictly to Core 1 (isolated from audio on Core 0)
    xTaskCreatePinnedToCore(
        bleHostTaskEntry,
        "nimble_host_task",
        4096,
        this,
        5,
        nullptr,
        board::ble::kTaskCore // Core 1
    );

    ESP_LOGI(kTag, "NimBLE BLE MIDI Central initialized and pinned to Core %d", board::ble::kTaskCore);
    return true;
}

void BleMidi::poll() {
    // UI/Core 1 samples the controller's most recently measured RSSI. This
    // query is deliberately never made from the audio task.
    static uint32_t lastRssiRequestMs = 0;
    const uint32_t nowMs = static_cast<uint32_t>(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (isConnected() && nowMs - lastRssiRequestMs >= 1000 && sConnHandle != BLE_HS_CONN_HANDLE_NONE) {
        lastRssiRequestMs = nowMs;
        int8_t value = 0;
        const int rc = ble_gap_conn_rssi(sConnHandle, &value);
        if (rc == 0) onRssi(value);
        else ESP_LOGD(kTag, "RSSI query failed: %d", rc);
    }
}

void BleMidi::onConnected(uint16_t connHandle) {
    if (hasConnectedBefore_.exchange(true, std::memory_order_acq_rel) &&
        disconnectedSinceConnect_.exchange(false, std::memory_order_acq_rel)) {
        reconnectCount_.fetch_add(1, std::memory_order_relaxed);
    }
    connected_.store(true, std::memory_order_release);
    setState(BleMidiState::Connected);
}

void BleMidi::onDisconnected() {
    connected_.store(false, std::memory_order_release);
    parser_.reset();
    disconnectedSinceConnect_.store(true, std::memory_order_release);
    if (state() != BleMidiState::Error) setState(BleMidiState::Idle);
}

void BleMidi::onConnectionUpdate(uint16_t intervalUnits, uint16_t latency, uint16_t supervisionTimeout) {
    intervalUnits_.store(intervalUnits, std::memory_order_release);
    latency_.store(latency, std::memory_order_release);
    supervisionTimeout_.store(supervisionTimeout, std::memory_order_release);
}
void BleMidi::onRssi(int8_t rssi) { rssi_.store(rssi, std::memory_order_release); }
void BleMidi::onDisconnectReason(uint8_t reason) { lastDisconnectReason_.store(reason, std::memory_order_release); }

void BleMidi::failAndRecover(const char* reason) {
    ESP_LOGE(kTag, "%s; disconnecting for recovery", reason);
    setState(BleMidiState::Error);
    if (sConnHandle != BLE_HS_CONN_HANDLE_NONE) ble_gap_terminate(sConnHandle, BLE_ERR_REM_USER_CONN_TERM);
}

void BleMidi::onMidiDataReceived(const uint8_t* data, size_t len) {
    parser_.parseBlePacket(data, len);
}

} // namespace pocketpan::hardware

#else

namespace pocketpan::hardware {
BleMidi::BleMidi() = default;
BleMidi::~BleMidi() = default;
bool BleMidi::begin() { return true; }
void BleMidi::poll() {}
void BleMidi::onMidiDataReceived(const uint8_t*, size_t) {}
void BleMidi::onConnected(uint16_t) { connected_.store(true); }
void BleMidi::onDisconnected() { connected_.store(false); disconnectedSinceConnect_.store(true); }
void BleMidi::onConnectionUpdate(uint16_t i, uint16_t l, uint16_t t) { intervalUnits_.store(i); latency_.store(l); supervisionTimeout_.store(t); }
void BleMidi::onRssi(int8_t r) { rssi_.store(r); }
void BleMidi::onDisconnectReason(uint8_t r) { lastDisconnectReason_.store(r); }
void BleMidi::failAndRecover(const char*) { connected_.store(false); setState(BleMidiState::Error); }
} // namespace pocketpan::hardware

#endif
