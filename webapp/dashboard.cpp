// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "dashboard.h"
#include "connector_backend.h"
#include "api.h"
#include "sim_log.h"

#include <MicroOcppMongooseClient.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Debug.h>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <sys/stat.h>

static struct mg_dash s_dash;
static MicroOcpp::MOcppMongooseClient *s_osock = nullptr;
static const char *s_listen_url = nullptr;
static const char *s_api_cert = "";
static const char *s_api_key = "";
static char s_auth_user[64] = "";
static char s_auth_pass[64] = "";

// ---- Per-connector mirrored state (fieldset c1 / c2) ----

struct ConnectorDash {
  bool evPlugged;
  bool evsePlugged;
  bool evReady;
  bool evseReady;
  char chargePointStatus[32];
  int energy;
  int power;
  double current;
  double voltage;
  char idTag[21];
  int transactionId;
  char nfcTag[21];
  int maxPower;
  double maxCurrent;
};

static ConnectorDash s_c[MO_WEBAPP_MAX_CONNECTORS];

static const MoConnectorBackend *hw(void) {
  return mo_webapp_get_connector_backend();
}

static void connector_read(unsigned int idx) {
  const MoConnectorBackend *b = hw();
  unsigned int id = idx + 1;
  if (!b || !mo_webapp_connector_valid(id)) return;
  ConnectorDash &d = s_c[idx];
  d.evPlugged = b->get_ev_plugged ? b->get_ev_plugged(id) : false;
  d.evsePlugged = b->get_evse_plugged ? b->get_evse_plugged(id) : false;
  d.evReady = b->get_ev_ready ? b->get_ev_ready(id) : false;
  d.evseReady = b->get_evse_ready ? b->get_evse_ready(id) : false;
  const char *st = b->get_ocpp_status ? b->get_ocpp_status(id) : "";
  mg_snprintf(d.chargePointStatus, sizeof(d.chargePointStatus), "%s",
              st ? st : "");
  d.energy = b->get_energy ? b->get_energy(id) : 0;
  d.power = b->get_power ? b->get_power(id) : 0;
  d.current = b->get_current ? (double) b->get_current(id) : 0.0;
  d.voltage = b->get_voltage ? (double) b->get_voltage(id) : 0.0;
  const char *tag = b->get_session_id_tag ? b->get_session_id_tag(id) : "";
  mg_snprintf(d.idTag, sizeof(d.idTag), "%s", tag ? tag : "");
  d.transactionId = b->get_transaction_id ? b->get_transaction_id(id) : -1;
  d.maxPower = b->get_smart_charging_max_power
                   ? b->get_smart_charging_max_power(id)
                   : 0;
  d.maxCurrent = b->get_smart_charging_max_current
                     ? (double) b->get_smart_charging_max_current(id)
                     : 0.0;
}

static void connector_write(unsigned int idx) {
  const MoConnectorBackend *b = hw();
  unsigned int id = idx + 1;
  if (!b || !mo_webapp_connector_valid(id)) return;
  ConnectorDash &d = s_c[idx];
  if (b->set_ev_plugged) b->set_ev_plugged(id, d.evPlugged);
  if (b->set_evse_plugged) b->set_evse_plugged(id, d.evsePlugged);
  if (b->set_ev_ready) b->set_ev_ready(id, d.evReady);
  if (b->set_evse_ready) b->set_evse_ready(id, d.evseReady);
  if (d.nfcTag[0] && b->present_nfc_tag) {
    b->present_nfc_tag(id, d.nfcTag);
    d.nfcTag[0] = '\0';
  }
}

static bool c1_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op == MG_DASH_READ) {
    connector_read(0);
    return true;
  }
  if (op == MG_DASH_WRITE) {
    connector_write(0);
    return true;
  }
  return false;
}

static bool c2_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (mo_webapp_connector_count() < 2) return false;
  if (op == MG_DASH_READ) {
    connector_read(1);
    return true;
  }
  if (op == MG_DASH_WRITE) {
    connector_write(1);
    return true;
  }
  return false;
}

#define CONNECTOR_FIELDS(n)                                                    \
  static struct mg_field fields_c##n[] = {                                     \
      {"evPlugged", MG_VAL_BOOL, &s_c[n - 1].evPlugged,                        \
       sizeof(s_c[n - 1].evPlugged)},                                          \
      {"evsePlugged", MG_VAL_BOOL, &s_c[n - 1].evsePlugged,                    \
       sizeof(s_c[n - 1].evsePlugged)},                                        \
      {"evReady", MG_VAL_BOOL, &s_c[n - 1].evReady,                            \
       sizeof(s_c[n - 1].evReady)},                                            \
      {"evseReady", MG_VAL_BOOL, &s_c[n - 1].evseReady,                        \
       sizeof(s_c[n - 1].evseReady)},                                          \
      {"chargePointStatus", MG_VAL_STR, s_c[n - 1].chargePointStatus, 0},      \
      {"energy", MG_VAL_INT, &s_c[n - 1].energy, 0},                           \
      {"power", MG_VAL_INT, &s_c[n - 1].power, 0},                             \
      {"current", MG_VAL_DBL, &s_c[n - 1].current, 0},                         \
      {"voltage", MG_VAL_DBL, &s_c[n - 1].voltage, 0},                         \
      {"idTag", MG_VAL_STR, s_c[n - 1].idTag, 0},                               \
      {"transactionId", MG_VAL_INT, &s_c[n - 1].transactionId, 0},             \
      {"nfcTag", MG_VAL_STR, s_c[n - 1].nfcTag, sizeof(s_c[n - 1].nfcTag)},    \
      {"maxPower", MG_VAL_INT, &s_c[n - 1].maxPower, 0},                       \
      {"maxCurrent", MG_VAL_DBL, &s_c[n - 1].maxCurrent, 0},                   \
      {NULL, MG_VAL_INT, NULL, 0},                                             \
  }

CONNECTOR_FIELDS(1);
CONNECTOR_FIELDS(2);

static struct mg_field_set set_c1 = {"c1", fields_c1, c1_fn, NULL, NULL, NULL};
static struct mg_field_set set_c2 = {"c2", fields_c2, c2_fn, NULL, NULL, NULL};

// ---- Meter graphs (voltage + current history, like full dashboard graph1) ----

#define METER_POINTS 60
#define METER_POINT_SIZE 28

struct MeterSample {
  double voltage;
  double current;
};

static MeterSample s_meter_hist[2][METER_POINTS];
static size_t s_meter_count[2] = {0, 0};
static size_t s_meter_head[2] = {0, 0};
static char s_g1_data[METER_POINTS * METER_POINT_SIZE + 4] = "[]";
static char s_g2_data[METER_POINTS * METER_POINT_SIZE + 4] = "[]";

static void meter_push(unsigned int idx) {
  if (idx >= 2 || idx >= mo_webapp_connector_count()) return;
  MeterSample &s = s_meter_hist[idx][s_meter_head[idx]];
  s.voltage = s_c[idx].voltage;
  s.current = s_c[idx].current;
  s_meter_head[idx] = (s_meter_head[idx] + 1) % METER_POINTS;
  if (s_meter_count[idx] < METER_POINTS) s_meter_count[idx]++;
}

static void meter_serialize(unsigned int idx, char *buf, size_t buflen) {
  size_t len = 0;
  size_t n = (idx < 2) ? s_meter_count[idx] : 0;
  size_t start =
      (idx < 2 && n == METER_POINTS) ? s_meter_head[idx] : 0;
  len += mg_snprintf(buf + len, buflen - len, "[");
  for (size_t j = 0; j < n; j++) {
    const MeterSample &s =
        s_meter_hist[idx][(start + j) % METER_POINTS];
    len += mg_snprintf(buf + len, buflen - len, "%s[%zu,%.1f,%.2f]",
                       j > 0 ? "," : "", j, s.voltage, s.current);
    if (len + METER_POINT_SIZE >= buflen) break;
  }
  mg_snprintf(buf + len, buflen - len, "]");
}

static void meter_sample_all(void) {
  size_t n = mo_webapp_connector_count();
  for (unsigned int i = 0; i < n && i < 2; i++) {
    connector_read(i);
    meter_push(i);
  }
  meter_serialize(0, s_g1_data, sizeof(s_g1_data));
  if (n >= 2) {
    meter_serialize(1, s_g2_data, sizeof(s_g2_data));
  } else {
    mg_snprintf(s_g2_data, sizeof(s_g2_data), "[]");
  }
}

static bool g1_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op != MG_DASH_READ) return false;
  meter_serialize(0, s_g1_data, sizeof(s_g1_data));
  return true;
}

static bool g2_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op != MG_DASH_READ) return false;
  if (mo_webapp_connector_count() < 2) {
    mg_snprintf(s_g2_data, sizeof(s_g2_data), "[]");
    return true;
  }
  meter_serialize(1, s_g2_data, sizeof(s_g2_data));
  return true;
}

static struct mg_field fields_g1[] = {
    {"data", MG_VAL_RAW, s_g1_data, sizeof(s_g1_data)},
    {NULL, MG_VAL_INT, NULL, 0},
};
static struct mg_field fields_g2[] = {
    {"data", MG_VAL_RAW, s_g2_data, sizeof(s_g2_data)},
    {NULL, MG_VAL_INT, NULL, 0},
};
static struct mg_field_set set_g1 = {"g1", fields_g1, g1_fn, NULL, NULL, NULL};
static struct mg_field_set set_g2 = {"g2", fields_g2, g2_fn, NULL, NULL, NULL};

// ---- OCPP WebSocket connection settings (replaces /api/websocket REST) ----

static char s_backendUrl[256];
static char s_chargeBoxId[64];
static char s_authorizationKey[64];
static char s_caCert[2048];
static char s_dnsUrl[128];
static int s_pingInterval;
static int s_reconnectInterval;
static bool s_ocppConnected;

#define CA_CERT_FN MO_FILENAME_PREFIX "ca_cert.pem"

static void load_ca_cert(void) {
  struct mg_str data = mg_file_read(&mg_fs_posix, CA_CERT_FN);
  if (data.buf) {
    mg_snprintf(s_caCert, sizeof(s_caCert), "%.*s", (int) data.len, data.buf);
    free(data.buf);
  } else {
    s_caCert[0] = '\0';
  }
}

static void save_ca_cert(void) {
  mg_file_printf(&mg_fs_posix, CA_CERT_FN, "%s", s_caCert);
}

static bool ocpp_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (!s_osock) return false;

  auto webSocketPingIntervalInt =
      MicroOcpp::declareConfiguration<int>("WebSocketPingInterval", 10, MO_WSCONN_FN);
  auto reconnectIntervalInt = MicroOcpp::declareConfiguration<int>(
      MO_CONFIG_EXT_PREFIX "ReconnectInterval", 30, MO_WSCONN_FN);

  if (op == MG_DASH_READ) {
    mg_snprintf(s_backendUrl, sizeof(s_backendUrl), "%s",
                s_osock->getBackendUrl());
    mg_snprintf(s_chargeBoxId, sizeof(s_chargeBoxId), "%s",
                s_osock->getChargeBoxId());
    mg_snprintf(s_authorizationKey, sizeof(s_authorizationKey), "%s",
                s_osock->getAuthKey());
    load_ca_cert();
    s_pingInterval = webSocketPingIntervalInt->getInt();
    s_reconnectInterval = reconnectIntervalInt->getInt();
    s_ocppConnected = s_osock->isConnected();
    return true;
  }
  if (op == MG_DASH_WRITE) {
    s_osock->setBackendUrl(s_backendUrl);
    s_osock->setChargeBoxId(s_chargeBoxId);
    s_osock->setAuthKey(s_authorizationKey);
    s_osock->reloadConfigs();
    if (s_pingInterval > 0) {
      webSocketPingIntervalInt->setInt(s_pingInterval);
    }
    if (s_reconnectInterval > 0) {
      reconnectIntervalInt->setInt(s_reconnectInterval);
    }
    save_ca_cert();
    if (s_dnsUrl[0]) {
      MO_DBG_WARN("dnsUrl not implemented");
    }
    MicroOcpp::configuration_save();
    return true;
  }
  return false;
}

static struct mg_field fields_ocpp[] = {
    {"backendUrl", MG_VAL_STR, s_backendUrl, sizeof(s_backendUrl)},
    {"chargeBoxId", MG_VAL_STR, s_chargeBoxId, sizeof(s_chargeBoxId)},
    {"authorizationKey", MG_VAL_STR, s_authorizationKey,
     sizeof(s_authorizationKey)},
    {"caCert", MG_VAL_STR, s_caCert, sizeof(s_caCert)},
    {"dnsUrl", MG_VAL_STR, s_dnsUrl, sizeof(s_dnsUrl)},
    {"pingInterval", MG_VAL_INT, &s_pingInterval, sizeof(s_pingInterval)},
    {"reconnectInterval", MG_VAL_INT, &s_reconnectInterval,
     sizeof(s_reconnectInterval)},
    {"connected", MG_VAL_BOOL, &s_ocppConnected, 0},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_ocpp = {"ocpp", fields_ocpp, ocpp_fn,
                                      NULL,    NULL,        NULL};

// ---- Station metadata (persisted in simulator.jsn) ----

static char s_cpModel[64] = "MicroOcpp Simulator";
static char s_cpSerial[64] = "";
static char s_cpVendor[64] = "MicroOcpp";
static char s_fwVersion[32] = "0.0.1";

static bool station_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  const MoConnectorBackend *b = hw();
  MoStationInfo info;

  if (op == MG_DASH_READ) {
    if (b && b->station_load) {
      memset(&info, 0, sizeof(info));
      b->station_load(&info);
      mg_snprintf(s_cpModel, sizeof(s_cpModel), "%s", info.model);
      mg_snprintf(s_cpSerial, sizeof(s_cpSerial), "%s", info.serial);
      mg_snprintf(s_cpVendor, sizeof(s_cpVendor), "%s", info.vendor);
      mg_snprintf(s_fwVersion, sizeof(s_fwVersion), "%s", info.firmware);
    }
    return true;
  }
  if (op == MG_DASH_WRITE) {
    if (b && b->station_save) {
      memset(&info, 0, sizeof(info));
      mg_snprintf(info.model, sizeof(info.model), "%s", s_cpModel);
      mg_snprintf(info.serial, sizeof(info.serial), "%s", s_cpSerial);
      mg_snprintf(info.vendor, sizeof(info.vendor), "%s", s_cpVendor);
      mg_snprintf(info.firmware, sizeof(info.firmware), "%s", s_fwVersion);
      b->station_save(&info);
    }
    return true;
  }
  return false;
}

static struct mg_field fields_station[] = {
    {"chargePointModel", MG_VAL_STR, s_cpModel, sizeof(s_cpModel)},
    {"chargePointSerialNumber", MG_VAL_STR, s_cpSerial, sizeof(s_cpSerial)},
    {"chargePointVendor", MG_VAL_STR, s_cpVendor, sizeof(s_cpVendor)},
    {"firmwareVersion", MG_VAL_STR, s_fwVersion, sizeof(s_fwVersion)},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_station = {"station", fields_station, station_fn,
                                         NULL,       NULL,           NULL};

// ---- Network demo (static UI parity with webapp-src) ----

static char s_wifiSsid[64] = "company_wifi_ef3c98";
static char s_wifiPass[64] = "compfi_ef3c98";
static char s_wifiStatus[32] = "Connected: Local_Network_Point";
static bool s_wifiEnabled = true;

static bool network_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op == MG_DASH_READ) return true;
  if (op == MG_DASH_WRITE) {
    mg_snprintf(s_wifiStatus, sizeof(s_wifiStatus), "Demo: %s",
                s_wifiEnabled ? s_wifiSsid : "Disconnected");
    return true;
  }
  return false;
}

static struct mg_field fields_network[] = {
    {"ssid", MG_VAL_STR, s_wifiSsid, sizeof(s_wifiSsid)},
    {"password", MG_VAL_STR, s_wifiPass, sizeof(s_wifiPass)},
    {"enabled", MG_VAL_BOOL, &s_wifiEnabled, sizeof(s_wifiEnabled)},
    {"status", MG_VAL_STR, s_wifiStatus, 0},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_network = {"network", fields_network, network_fn,
                                         NULL,       NULL,           NULL};

// ---- Security (CA cert + static cipher info) ----

static char s_cipherName[32] = "TLS";
static char s_cipherVersion[32] = "v2.3.1";
static char s_cipherExpiry[32] = "26.12.2026";

static bool security_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op == MG_DASH_READ) {
    load_ca_cert();
    return true;
  }
  if (op == MG_DASH_WRITE) {
    save_ca_cert();
    return true;
  }
  return false;
}

static struct mg_field fields_security[] = {
    {"caCert", MG_VAL_STR, s_caCert, sizeof(s_caCert)},
    {"cipherName", MG_VAL_STR, s_cipherName, 0},
    {"cipherVersion", MG_VAL_STR, s_cipherVersion, 0},
    {"cipherExpiry", MG_VAL_STR, s_cipherExpiry, 0},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_security = {
    "security", fields_security, security_fn, NULL, NULL, NULL};

// ---- Firmware (OTA demo for desktop simulator) ----

static char s_fw_version[32] = "0.0.1";
static char s_fw_date[32] = __DATE__;
static char s_fw_status[64] = "Idle (desktop simulator)";
static char s_fw_url[128] = "https://example.com/ota.json";
static int s_fw_interval = 3600;

static bool firmware_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  const MoConnectorBackend *b = hw();
  MoStationInfo info;

  if (op == MG_DASH_READ) {
    mg_snprintf(s_fw_date, sizeof(s_fw_date), "%s", __DATE__);
    if (b && b->station_load) {
      memset(&info, 0, sizeof(info));
      b->station_load(&info);
      mg_snprintf(s_fw_version, sizeof(s_fw_version), "%s", info.firmware);
    }
    return true;
  }
  if (op == MG_DASH_WRITE) {
    if (b && b->station_load && b->station_save) {
      memset(&info, 0, sizeof(info));
      b->station_load(&info);
      mg_snprintf(info.firmware, sizeof(info.firmware), "%s", s_fw_version);
      b->station_save(&info);
    }
    mg_snprintf(s_fw_status, sizeof(s_fw_status),
                "Config saved (OTA N/A on desktop)");
    return true;
  }
  return false;
}

static struct mg_field fields_firmware[] = {
    {"version", MG_VAL_STR, s_fw_version, sizeof(s_fw_version)},
    {"date", MG_VAL_STR, s_fw_date, 0},
    {"status", MG_VAL_STR, s_fw_status, sizeof(s_fw_status)},
    {"url", MG_VAL_STR, s_fw_url, sizeof(s_fw_url)},
    {"interval", MG_VAL_INT, &s_fw_interval, sizeof(s_fw_interval)},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_firmware = {
    "firmware", fields_firmware, firmware_fn, NULL, NULL, NULL};

// ---- Events (ring-buffer array fieldset) ----

struct EvEvent {
  int type;   // 0=ocpp 1=connector 2=system 3=auth
  int prio;   // 0=high 1=medium 2=low
  char time[32];
  char message[96];
};

#define EV_EVENT_MAX 100
static EvEvent *s_events = nullptr;
static size_t s_ev_count = 0;
static EvEvent s_ev_stage;
static int s_ev_index = 0;
static char s_prev_status[MO_WEBAPP_MAX_CONNECTORS][32];
static bool s_prev_ocpp = false;
static bool s_ev_inited = false;

static bool event_append_msg(int type, int prio, const char *msg) {
  EvEvent *arr =
      (EvEvent *) realloc(s_events, (s_ev_count + 1) * sizeof(EvEvent));
  if (!arr) return false;
  s_events = arr;
  time_t now = time(nullptr);
  struct tm *tm = localtime(&now);
  if (tm) {
    mg_snprintf(s_events[s_ev_count].time, sizeof(s_events[s_ev_count].time),
                "%04d-%02d-%02d %02d:%02d:%02d", tm->tm_year + 1900,
                tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min,
                tm->tm_sec);
  } else {
    mg_snprintf(s_events[s_ev_count].time, sizeof(s_events[s_ev_count].time),
                "1970-01-01 00:00:00");
  }
  s_events[s_ev_count].type = type;
  s_events[s_ev_count].prio = prio;
  mg_snprintf(s_events[s_ev_count].message, sizeof(s_events[s_ev_count].message),
              "%s", msg ? msg : "");
  s_ev_count++;
  if (s_ev_count > EV_EVENT_MAX) {
    memmove(s_events, s_events + 1, (s_ev_count - 1) * sizeof(EvEvent));
    s_ev_count--;
    EvEvent *shrunk =
        (EvEvent *) realloc(s_events, s_ev_count * sizeof(EvEvent));
    if (shrunk) s_events = shrunk;
  }
  return true;
}

static bool event_remove(int index) {
  if (index < 0 || (size_t) index >= s_ev_count) return false;
  memmove(&s_events[index], &s_events[index + 1],
          (s_ev_count - (size_t) index - 1) * sizeof(EvEvent));
  s_ev_count--;
  if (s_ev_count == 0) {
    free(s_events);
    s_events = nullptr;
  } else {
    EvEvent *arr = (EvEvent *) realloc(s_events, s_ev_count * sizeof(EvEvent));
    if (arr) s_events = arr;
  }
  return true;
}

static bool event_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op == MG_DASH_READ) {
    if (s_ev_index < 0) {
      s_ev_index = (int) s_ev_count;
    } else if ((size_t) s_ev_index >= s_ev_count) {
      s_ev_index = -1;
    } else {
      s_ev_stage = s_events[s_ev_index];
    }
    return true;
  }
  if (op == MG_DASH_WRITE && s_ev_index >= 0 &&
      (size_t) s_ev_index < s_ev_count) {
    s_events[s_ev_index] = s_ev_stage;
    return true;
  }
  if (op == MG_DASH_DELETE) return event_remove(s_ev_index);
  if (op == MG_DASH_ADD) {
    return event_append_msg(s_ev_stage.type, s_ev_stage.prio, s_ev_stage.message);
  }
  return false;
}

static struct mg_field fields_event[] = {
    {"index", MG_VAL_INT, &s_ev_index, sizeof(s_ev_index)},
    {"type", MG_VAL_INT, &s_ev_stage.type, sizeof(s_ev_stage.type)},
    {"prio", MG_VAL_INT, &s_ev_stage.prio, sizeof(s_ev_stage.prio)},
    {"time", MG_VAL_STR, s_ev_stage.time, sizeof(s_ev_stage.time)},
    {"message", MG_VAL_STR, s_ev_stage.message, sizeof(s_ev_stage.message)},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_event = {
    "event", fields_event, event_fn, &s_ev_index, NULL, NULL};

// ---- Log capture settings (Mongoose level + UI enable) ----

static bool s_logEnabled = true;
static int s_logLevel = MG_LL_INFO;

static bool log_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  (void) u;
  if (op == MG_DASH_READ) {
    s_logEnabled = sim_log_is_enabled();
    s_logLevel = sim_log_get_level();
    return true;
  }
  if (op == MG_DASH_WRITE) {
    sim_log_set_enabled(s_logEnabled);
    sim_log_set_level(s_logLevel);
    return true;
  }
  return false;
}

static struct mg_field fields_log[] = {
    {"enabled", MG_VAL_BOOL, &s_logEnabled, sizeof(s_logEnabled)},
    {"level", MG_VAL_INT, &s_logLevel, sizeof(s_logLevel)},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_log = {"log", fields_log, log_fn, NULL, NULL,
                                      NULL};

// ---- File manager (mo_store), same pattern as device-dashboard/full ----

struct DashFile {
  int index;
  char name[128];
  uint64_t size;
  uint64_t checksum;
};

static struct DashFile s_file;
static bool files_fn(enum mg_dash_op op, struct mg_dash_user *u);

static bool files_get_dir(const struct mg_dash_user *u, char *buf, size_t len) {
  (void) u;
  mg_snprintf(buf, len, "%s", "./mo_store");
  return true;
}

static struct mg_field fields_files[] = {
    {"name", MG_VAL_STR, s_file.name, sizeof(s_file.name)},
    {"size", MG_VAL_UINT64, &s_file.size, sizeof(s_file.size)},
    {"checksum", MG_VAL_UINT64, &s_file.checksum, sizeof(s_file.checksum)},
    {NULL, MG_VAL_INT, NULL, 0},
};

static struct mg_field_set set_files = {"files",       fields_files, files_fn,
                                        &s_file.index, files_get_dir, NULL};

static bool files_fn(enum mg_dash_op op, struct mg_dash_user *u) {
  if (op == MG_DASH_WRITE || op == MG_DASH_DELETE) {
    return u != NULL && u->level >= 7;
  }
  if (op != MG_DASH_READ) return false;
  if (!mg_dash_dir_read(&set_files, u)) return false;
  s_file.checksum = 0;
  return true;
}

static int status_prio(const char *st) {
  if (!st) return 1;
  if (!strcmp(st, "Faulted")) return 0;
  if (!strcmp(st, "Charging")) return 2;
  return 1;
}

static void events_poll_changes(struct mg_mgr *mgr) {
  char buf[96];
  bool changed = false;
  const MoConnectorBackend *b = hw();
  size_t n = mo_webapp_connector_count();
  for (size_t i = 0; i < n; i++) {
    unsigned int id = (unsigned int) (i + 1);
    const char *st =
        (b && b->get_ocpp_status) ? b->get_ocpp_status(id) : "";
    if (!st) st = "";
    if (!s_ev_inited || strcmp(s_prev_status[i], st) != 0) {
      mg_snprintf(buf, sizeof(buf), "C%u status → %s",
                  (unsigned) (i + 1), st);
      event_append_msg(1, status_prio(st), buf);
      mg_snprintf(s_prev_status[i], sizeof(s_prev_status[i]), "%s", st);
      changed = true;
    }
  }
  bool conn = s_osock && s_osock->isConnected();
  if (!s_ev_inited || conn != s_prev_ocpp) {
    event_append_msg(0, conn ? 2 : 0,
                     conn ? "OCPP connected" : "OCPP disconnected");
    s_prev_ocpp = conn;
    changed = true;
  }
  if (!s_ev_inited) {
    event_append_msg(2, 2, "Simulator dashboard ready");
    s_ev_inited = true;
    changed = true;
  }
  if (changed) {
    s_ev_index = -1;
    mg_dash_send_change(mgr, &set_event);
  }
}

// ---- Auth ----

static int dash_authenticate(char *user, size_t userlen, const char *pass) {
  if (s_auth_user[0] || s_auth_pass[0]) {
    if (strcmp(user, s_auth_user) == 0 && strcmp(pass, s_auth_pass) == 0) {
      return 7;
    }
    if (s_auth_user[0] == '\0' && strcmp(pass, s_auth_pass) == 0) {
      mg_snprintf(user, userlen, "%s", "admin");
      return 7;
    }
  }
  if (strcmp(pass, "admin") == 0) {
    mg_snprintf(user, userlen, "%s", "admin");
    return 7;
  }
  if (strcmp(pass, "user") == 0) {
    mg_snprintf(user, userlen, "%s", "user");
    return 3;
  }
  return 0;
}

// ---- Legacy REST API (except /api/websocket, claimed by dashboard WS) ----

static void legacy_api_handler(struct mg_connection *c, int ev, void *ev_data) {
  if (ev != MG_EV_HTTP_MSG) return;

  struct mg_http_message *hm =
      reinterpret_cast<struct mg_http_message *>(ev_data);
  const char *headers =
      "Content-Type: application/json\r\n"
      "Access-Control-Allow-Origin: *\r\n"
      "Access-Control-Allow-Headers: Access-Control-Allow-Headers, Origin, "
      "Accept, X-Requested-With, Content-Type, Access-Control-Request-Method, "
      "Access-Control-Request-Headers\r\n"
      "Access-Control-Allow-Methods: GET,HEAD,OPTIONS,POST,PUT\r\n";

  MicroOcpp::Method method = MicroOcpp::Method::UNDEFINED;
  if (!mg_strcasecmp(hm->method, mg_str("POST"))) {
    method = MicroOcpp::Method::POST;
  } else if (!mg_strcasecmp(hm->method, mg_str("GET"))) {
    method = MicroOcpp::Method::GET;
  }

  char resp_buf[32768];
  if (char *sp = strchr((char *) hm->uri.buf, ' ')) {
    *sp = '\0';
  }

  int status = 404;
  if (hm->uri.len >= 4 && strncmp(hm->uri.buf, "/api", 4) == 0) {
    status = mocpp_api2_call(hm->uri.buf + 4, hm->uri.len - 4, method,
                             hm->query.buf, hm->query.len, resp_buf,
                             sizeof(resp_buf));
    if (status == 404) {
      status = mocpp_api_call(hm->uri.buf + 4, method, hm->body.buf, resp_buf,
                              sizeof(resp_buf));
    }
  }

  mg_http_reply(c, status, headers, "%s", status == 404 ? "{}" : resp_buf);
}

// ---- HTTP serve wrapper (TLS + dashboard) ----

static void dash_http_serve(struct mg_connection *c, int ev, void *ev_data) {
  if (ev == MG_EV_ACCEPT && s_listen_url && mg_url_is_ssl(s_listen_url)) {
    struct mg_tls_opts opts = {};
    opts.cert = mg_str(s_api_cert);
    opts.key = mg_str(s_api_key);
    mg_tls_init(c, &opts);
  }
  mg_dash_ev_handler(c, ev, ev_data);
}

void dashboard_initialize(MicroOcpp::MOcppMongooseClient *osock,
                          const char *cert, const char *key, const char *user,
                          const char *pass) {
  s_osock = osock;
  s_api_cert = cert ? cert : "";
  s_api_key = key ? key : "";
  mg_snprintf(s_auth_user, sizeof(s_auth_user), "%s", user ? user : "");
  mg_snprintf(s_auth_pass, sizeof(s_auth_pass), "%s", pass ? pass : "");
}

void dashboard_listen(struct mg_mgr *mgr, const char *url) {
  memset(&s_dash, 0, sizeof(s_dash));

  MG_DASH_ADD_FIELD_SET(&s_dash, &set_c1);
  if (mo_webapp_connector_count() >= 2) {
    MG_DASH_ADD_FIELD_SET(&s_dash, &set_c2);
  }
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_g1);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_g2);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_ocpp);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_station);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_network);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_security);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_firmware);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_event);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_log);
  MG_DASH_ADD_FIELD_SET(&s_dash, &set_files);

  mkdir("./mo_store", 0755);

  sim_log_init(mgr);

  // Keep legacy REST for /api/connector/*, /api/plugin, etc.
  // Register /api/log/# after /api/# so it is matched first (LIFO list).
  MG_DASH_REGISTER_CUSTOM_HANDLER(&s_dash, "/api/#", legacy_api_handler, NULL);
  MG_DASH_REGISTER_CUSTOM_HANDLER(&s_dash, "/api/log/#", sim_log_http_handler,
                                  NULL);

  s_dash.authenticate = dash_authenticate;

  s_listen_url = url;
  mg_mem_files = mg_packed_files;
  mg_http_listen(mgr, url, dash_http_serve, &s_dash);
  MO_DBG_INFO("Dashboard listening on %s", url);
}

void mg_dash_poll(struct mg_mgr *mgr) {
  static uint64_t timer = 0;
  if (mg_timer_expired(&timer, 1000, mg_now())) {
    meter_sample_all();
    mg_dash_send_change(mgr, &set_c1);
    if (mo_webapp_connector_count() >= 2) {
      mg_dash_send_change(mgr, &set_c2);
    }
    mg_dash_send_change(mgr, &set_g1);
    if (mo_webapp_connector_count() >= 2) {
      mg_dash_send_change(mgr, &set_g2);
    }
    mg_dash_send_change(mgr, &set_ocpp);
    events_poll_changes(mgr);
  }
}
