// matth-x/MicroOcpp — Evse adapter for webapp ConnectorBackend
// MIT License

#include "evse.h"
#include "connector_backend.h"

#include <MicroOcpp/Core/Configuration.h>
#include <cstdio>
#include <cstring>

#if MO_NUMCONNECTORS == 3
std::array<Evse, MO_NUMCONNECTORS - 1> connectors{{1, 2}};
#else
std::array<Evse, MO_NUMCONNECTORS - 1> connectors{{1}};
#endif

static Evse *evse_at(unsigned int connectorId) {
    if (connectorId < 1 || connectorId > connectors.size()) {
        return nullptr;
    }
    return &connectors[connectorId - 1];
}

static size_t be_connector_count(void) {
    return connectors.size();
}

static bool be_get_ev_plugged(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getEvPlugged() : false;
}
static void be_set_ev_plugged(unsigned int id, bool v) {
    if (Evse *e = evse_at(id)) e->setEvPlugged(v);
}
static bool be_get_evse_plugged(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getEvsePlugged() : false;
}
static void be_set_evse_plugged(unsigned int id, bool v) {
    if (Evse *e = evse_at(id)) e->setEvsePlugged(v);
}
static bool be_get_ev_ready(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getEvReady() : false;
}
static void be_set_ev_ready(unsigned int id, bool v) {
    if (Evse *e = evse_at(id)) e->setEvReady(v);
}
static bool be_get_evse_ready(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getEvseReady() : false;
}
static void be_set_evse_ready(unsigned int id, bool v) {
    if (Evse *e = evse_at(id)) e->setEvseReady(v);
}

static const char *be_get_ocpp_status(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getOcppStatus() : "";
}
static int be_get_energy(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getEnergy() : 0;
}
static int be_get_power(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getPower() : 0;
}
static float be_get_current(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getCurrent() : 0.f;
}
static float be_get_voltage(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getVoltage() : 0.f;
}
static const char *be_get_session_id_tag(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getSessionIdTag() : "";
}
static int be_get_transaction_id(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getTransactionId() : -1;
}
static int be_get_sc_max_power(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getSmartChargingMaxPower() : 0;
}
static float be_get_sc_max_current(unsigned int id) {
    Evse *e = evse_at(id);
    return e ? e->getSmartChargingMaxCurrent() : 0.f;
}

static void be_present_nfc(unsigned int id, const char *uid) {
    if (Evse *e = evse_at(id)) e->presentNfcTag(uid);
}

static bool be_present_nfc_typed(unsigned int id, const char *uid, const char *type) {
#if MO_ENABLE_V201
    Evse *e = evse_at(id);
    return e ? e->presentNfcTag(uid, type) : false;
#else
    (void)type;
    if (Evse *e = evse_at(id)) {
        e->presentNfcTag(uid);
        return true;
    }
    return false;
#endif
}

static void be_station_load(MoStationInfo *out) {
    if (!out) return;
    auto model = MicroOcpp::declareConfiguration<const char *>(
            "StationModel", "MicroOcpp Simulator", SIMULATOR_FN, false, false, false);
    auto serial = MicroOcpp::declareConfiguration<const char *>(
            "StationSerial", "", SIMULATOR_FN, false, false, false);
    auto vendor = MicroOcpp::declareConfiguration<const char *>(
            "StationVendor", "MicroOcpp", SIMULATOR_FN, false, false, false);
    auto fw = MicroOcpp::declareConfiguration<const char *>(
            "StationFirmware", "0.0.1", SIMULATOR_FN, false, false, false);
    MicroOcpp::configuration_load(SIMULATOR_FN);
    snprintf(out->model, sizeof(out->model), "%s", model->getString());
    snprintf(out->serial, sizeof(out->serial), "%s", serial->getString());
    snprintf(out->vendor, sizeof(out->vendor), "%s", vendor->getString());
    snprintf(out->firmware, sizeof(out->firmware), "%s", fw->getString());
}

static void be_station_save(const MoStationInfo *in) {
    if (!in) return;
    auto model = MicroOcpp::declareConfiguration<const char *>(
            "StationModel", "MicroOcpp Simulator", SIMULATOR_FN, false, false, false);
    auto serial = MicroOcpp::declareConfiguration<const char *>(
            "StationSerial", "", SIMULATOR_FN, false, false, false);
    auto vendor = MicroOcpp::declareConfiguration<const char *>(
            "StationVendor", "MicroOcpp", SIMULATOR_FN, false, false, false);
    auto fw = MicroOcpp::declareConfiguration<const char *>(
            "StationFirmware", "0.0.1", SIMULATOR_FN, false, false, false);
    model->setString(in->model);
    serial->setString(in->serial);
    vendor->setString(in->vendor);
    fw->setString(in->firmware);
    MicroOcpp::configuration_save();
}

static const MoConnectorBackend s_evse_backend = {
    be_connector_count,
    be_get_ev_plugged,
    be_set_ev_plugged,
    be_get_evse_plugged,
    be_set_evse_plugged,
    be_get_ev_ready,
    be_set_ev_ready,
    be_get_evse_ready,
    be_set_evse_ready,
    be_get_ocpp_status,
    be_get_energy,
    be_get_power,
    be_get_current,
    be_get_voltage,
    be_get_session_id_tag,
    be_get_transaction_id,
    be_get_sc_max_power,
    be_get_sc_max_current,
    be_present_nfc,
    be_present_nfc_typed,
    be_station_load,
    be_station_save,
};

void pc_web_register_evse_backend(void) {
    mo_webapp_set_connector_backend(&s_evse_backend);
}
