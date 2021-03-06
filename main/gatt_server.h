#pragma once

#include "common.h"
#include "config_manager.h"

#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace sd {

const static size_t kMaxGATTMTU = 512;
const static size_t kMinGATTMTU = 22;

using char_read_cb = std::function<void(BufferPtr*, size_t*)>;
using char_write_cb = std::function<void(uint16_t, uint8_t*, size_t)>;
using conn_cb = std::function<void(bool)>;

static constexpr uint8_t kServiceUUID128[16] = {
    /* LSB <--------------------------------------------------------------------------------> MSB */
    0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0xFF, 0x00, 0x00, 0x00,
};

class GATTServer {
  public:
    GATTServer(ConfigManager* cfg_mgt);
    ~GATTServer();

    static GATTServer* RegisterServer(ConfigManager* cfg_mgt);

    esp_err_t Init();
    esp_err_t CreateService(uint16_t uuid, uint8_t* inst_id);
    esp_err_t AddCharateristic(uint8_t service_inst_id, uint16_t uuid,
                               char_read_cb read_cb, char_write_cb write_cb, uint16_t* char_handle = nullptr);
    esp_err_t StartAdvertising();
    esp_err_t StopAdvertising();
    esp_err_t Notify(uint16_t char_handle, uint8_t* buf, size_t buf_len);
    esp_err_t RegisterConnectionStateChangeCb(uint32_t* reg_no, const conn_cb& cb);
    esp_err_t UnregisterConnectionStateChangeCb(uint32_t reg_no);

    void GATTEventHandler(esp_gatts_cb_event_t event, esp_gatt_if_t gatts_if, esp_ble_gatts_cb_param_t *param);
    void GAPEventHandler(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t *param);

  private:
    typedef struct {
        uint8_t* prepare_buf;
        int      prepare_len;
        uint32_t next_trans_id;
    } prepare_type_env_t;

    struct Service {
        uint16_t service_handle;
        esp_gatt_srvc_id_t service_id;
    };

    struct LongMsg {
        BufferPtr read_buf;
        size_t read_buf_size;
        uint32_t next_offset;
        uint32_t next_trans_id;
    };

    struct Charateristic {
        uint8_t service_inst_id;
        uint16_t char_handle;
        esp_bt_uuid_t char_uuid;
        uint16_t cccd_handle;
        bool notification_enabled;
        bool indication_enabled;
        // esp_gatt_perm_t perm;
        // esp_gatt_char_prop_t property;
        // uint16_t descr_handle;
        // esp_bt_uuid_t descr_uuid;
        char_read_cb read_cb;
        char_write_cb write_cb;
        LongMsg read_long_msg;
        prepare_type_env_t prepare_write_env;
    };

    esp_err_t InitBTStack();
    esp_err_t InitGap();
    void HandleCCCDWrite(uint16_t cccd_handle, uint16_t char_handle);

    static void PrepareWrite(esp_gatt_if_t gatts_if, Charateristic* charateristic, esp_ble_gatts_cb_param_t *param);
    static void ExecWrite(Charateristic* charateristic, esp_ble_gatts_cb_param_t *param);

    uint32_t app_id_;
    std::string device_name_;

    // gap
    esp_ble_adv_data_t adv_data_;
    esp_ble_adv_data_t scan_rsp_data_;
    esp_ble_adv_params_t adv_params_;

    // gatt
    esp_gatts_cb_t gatts_cb_;
    uint16_t gatts_if_ = ESP_GATT_IF_NONE;
    uint16_t conn_id_;
    size_t mtu_;
    std::unordered_map<uint32_t, conn_cb> conn_cbs_;
    std::unique_ptr<Mutex> conn_cbs_mutex_;
    uint32_t conn_cbs_index_ = 0;

    // services
    std::vector<std::unique_ptr<struct Service>> services_;

    // charateristics
    std::unordered_map<uint16_t, std::unique_ptr<struct Charateristic>> chars_;
    std::unordered_map<uint16_t, uint16_t> cccds_;
    uint16_t new_char_handle_;
    uint16_t new_cccd_handle_;

    ConfigManager* cfg_mgt_;
    bool is_login_ = false;

    // event group
    static const TickType_t kEGTimeout = 3000 / portTICK_PERIOD_MS;
    static const EventBits_t kEGAdvConfigDone = (1 << 0);
    static const EventBits_t kEGAdvRspConfigDone = (1 << 1);
    static const EventBits_t kEGAdvStartComplete = (1 << 2);
    static const EventBits_t kEGAppRegisterComplete = (1 << 3);
    static const EventBits_t kEGServiceCreateComplete = (1 << 4);
    static const EventBits_t kEGAddCharComplete = (1 << 5);

    EventGroupHandle_t event_group_;

    esp_bt_status_t start_adv_status_;
    esp_gatt_status_t reg_app_status_;
    esp_gatt_status_t add_char_status_;
};

}  // namespace sd

