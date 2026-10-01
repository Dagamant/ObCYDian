// Diagnostic firmware (env:btscan): scans for Classic and BLE devices at the same time and
// reports anything that looks like a keyboard, to find out how a keyboard connects.

#include <Arduino.h>
#include <esp_bt.h>
#include <esp_bt_main.h>
#include <esp_gap_ble_api.h>
#include <esp_gap_bt_api.h>

#include <set>
#include <string>

static std::set<std::string> seen;

static std::string bdaStr(const uint8_t* b) {
  char s[18];
  snprintf(s, sizeof(s), "%02x:%02x:%02x:%02x:%02x:%02x", b[0], b[1], b[2], b[3], b[4], b[5]);
  return s;
}

static void classicCb(esp_bt_gap_cb_event_t event, esp_bt_gap_cb_param_t* p) {
  if (event == ESP_BT_GAP_DISC_STATE_CHANGED_EVT) {
    Serial.printf("[classic] discovery %s\n",
                  p->disc_st_chg.state == ESP_BT_GAP_DISCOVERY_STARTED ? "started" : "stopped");
    return;
  }
  if (event != ESP_BT_GAP_DISC_RES_EVT) return;
  uint32_t cod = 0;
  int rssi = 0;
  std::string name;
  for (int i = 0; i < p->disc_res.num_prop; i++) {
    auto& pr = p->disc_res.prop[i];
    if (pr.type == ESP_BT_GAP_DEV_PROP_COD) cod = *(uint32_t*)pr.val;
    if (pr.type == ESP_BT_GAP_DEV_PROP_RSSI) rssi = *(int8_t*)pr.val;
    if (pr.type == ESP_BT_GAP_DEV_PROP_BDNAME) name.assign((char*)pr.val, strnlen((char*)pr.val, pr.len));
    if (pr.type == ESP_BT_GAP_DEV_PROP_EIR && name.empty()) {
      uint8_t len = 0;
      uint8_t* n = esp_bt_gap_resolve_eir_data((uint8_t*)pr.val, ESP_BT_EIR_TYPE_CMPL_LOCAL_NAME, &len);
      if (!n) n = esp_bt_gap_resolve_eir_data((uint8_t*)pr.val, ESP_BT_EIR_TYPE_SHORT_LOCAL_NAME, &len);
      if (n) name.assign((char*)n, len);
    }
  }
  std::string key = "C" + bdaStr(p->disc_res.bda);
  if (!seen.insert(key).second) return;
  int major = (cod >> 8) & 0x1F, minor = (cod >> 2) & 0x3F;
  bool kbd = major == 5 && (minor >> 4) & 1;
  Serial.printf("[classic] %s rssi %d cod 0x%06x major %d minor 0x%02x %s name '%s'\n",
                bdaStr(p->disc_res.bda).c_str(), rssi, (unsigned)cod, major, minor,
                kbd ? "KEYBOARD" : major == 5 ? "peripheral" : "", name.c_str());
}

static void bleCb(esp_gap_ble_cb_event_t event, esp_ble_gap_cb_param_t* p) {
  if (event == ESP_GAP_BLE_SCAN_PARAM_SET_COMPLETE_EVT) {
    esp_ble_gap_start_scanning(0);  // until stopped
    return;
  }
  if (event != ESP_GAP_BLE_SCAN_RESULT_EVT || p->scan_rst.search_evt != ESP_GAP_SEARCH_INQ_RES_EVT) return;
  auto& r = p->scan_rst;
  uint8_t* adv = r.ble_adv;
  uint8_t len = 0;
  std::string name;
  uint8_t* n = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_NAME_CMPL, &len);
  if (!n) n = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_NAME_SHORT, &len);
  if (n) name.assign((char*)n, len);
  uint16_t appearance = 0;
  uint8_t* a = esp_ble_resolve_adv_data(adv, ESP_BLE_AD_TYPE_APPEARANCE, &len);
  if (a && len == 2) appearance = a[0] | (a[1] << 8);
  bool hidService = false;
  for (auto type : {ESP_BLE_AD_TYPE_16SRV_CMPL, ESP_BLE_AD_TYPE_16SRV_PART}) {
    uint8_t* u = esp_ble_resolve_adv_data(adv, type, &len);
    for (int i = 0; u && i + 1 < len; i += 2)
      if ((u[i] | (u[i + 1] << 8)) == 0x1812) hidService = true;
  }
  bool kbd = appearance == 0x03C1 || hidService;
  std::string key = "B" + bdaStr(r.bda);
  if (name.empty() && !kbd) return;  // skip anonymous beacons
  if (!seen.insert(key).second) return;
  Serial.printf("[ble] %s (%s) rssi %d appearance 0x%04x %s name '%s'\n", bdaStr(r.bda).c_str(),
                r.ble_addr_type == BLE_ADDR_TYPE_PUBLIC ? "public" : "random", r.rssi, appearance,
                kbd ? (appearance == 0x03C1 ? "KEYBOARD" : "HID") : "", name.c_str());
}

void setup() {
  Serial.begin(921600);
  delay(200);
  Serial.println("\n[btscan] starting Classic + BLE scan");
  if (!btStart()) Serial.println("btStart failed");
  esp_bluedroid_init();
  esp_bluedroid_enable();
  esp_bt_gap_register_callback(classicCb);
  esp_ble_gap_register_callback(bleCb);
  esp_ble_scan_params_t params = {};
  params.scan_type = BLE_SCAN_TYPE_ACTIVE;
  params.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
  params.scan_filter_policy = BLE_SCAN_FILTER_ALLOW_ALL;
  params.scan_interval = 0x50;
  params.scan_window = 0x30;
  params.scan_duplicate = BLE_SCAN_DUPLICATE_DISABLE;
  esp_ble_gap_set_scan_params(&params);
  Serial.printf("[btscan] heap free after BT start: %u\n", ESP.getFreeHeap());
}

void loop() {
  static uint32_t last = 0;
  if (millis() - last > 15000 || last == 0) {
    last = millis();
    esp_bt_gap_cancel_discovery();
    esp_bt_gap_start_discovery(ESP_BT_INQ_MODE_GENERAL_INQUIRY, 10, 0);  // ~12.8 s
    Serial.println("[btscan] --- new round (put the keyboard in pairing mode) ---");
  }
  delay(50);
}
