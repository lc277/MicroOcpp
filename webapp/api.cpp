// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#include "api.h"
#include "mongoose.h"
#include "connector_backend.h"

#include <MicroOcpp/Debug.h>
#include <MicroOcpp/Core/Memory.h>
#include <MicroOcpp/Core/Configuration.h>
#include <MicroOcpp/Model/ConnectorBase/EvseId.h>
#include <MicroOcpp/Model/Authorization/IdToken.h>

#include <cctype>
#include <cstdio>
#include <cstring>

//simple matching function; takes * as a wildcard
bool str_match(const char *query, const char *pattern) {
    size_t qi = 0, pi = 0;
    
    while (pattern[pi]) {
        if (query[qi] && query[qi] == pattern[pi]) {
            qi++;
            pi++;
        } else if (pattern[pi] == '*') {
            while (pattern[pi] == '*') pi++;
            while (query[qi] != pattern[pi]) qi++;
        } else {
            break;
        }
    }

    return !query[qi] && !pattern[pi];
}

int mocpp_api_call(const char *endpoint, MicroOcpp::Method method, const char *body, char *resp_body, size_t resp_body_size) {
    
    MO_DBG_VERBOSE("process %s, %s: %s",
            endpoint,
            method == MicroOcpp::Method::GET ? "GET" :
            method == MicroOcpp::Method::POST ? "POST" : "error",
            body);
    
    int status = 500;
    StaticJsonDocument<512> response;
    if (resp_body_size >= sizeof("{}")) {
        sprintf(resp_body, "%s", "{}");
    }

    StaticJsonDocument<512> request;
    if (body && *body) {
        auto err = deserializeJson(request, body);
        if (err) {
            MO_DBG_WARN("malformatted body: %s", err.c_str());
            return 400;
        }
    }
    
    unsigned int connectorId = 0;

    if (strlen(endpoint) >= 11) {
        if (endpoint[11] == '1') {
            connectorId = 1;
        } else if (endpoint[11] == '2') {
            connectorId = 2;
        }
    }

    MO_DBG_VERBOSE("connectorId = %u", connectorId);

    const MoConnectorBackend *b = mo_webapp_get_connector_backend();
    bool have_connector = mo_webapp_connector_valid(connectorId);

    //start different api endpoints
    if(str_match(endpoint, "/ocpp/config")) {
        using namespace MicroOcpp;
        if (method == Method::GET) {
            auto containers = getConfigurationContainersPublic();
            size_t nkeys = 0;
            size_t strbytes = 0;
            for (auto container : containers) {
                for (size_t i = 0; i < container->size(); i++) {
                    auto *cfg = container->getConfiguration(i);
                    if (!cfg || !cfg->getKey() || !cfg->isReadable()) continue;
                    nkeys++;
                    strbytes += strlen(cfg->getKey()) + 1;
                    if (cfg->getType() == TConfig::String) {
                        strbytes += strlen(cfg->getString()) + 1;
                    } else {
                        strbytes += 16;
                    }
                }
            }
            size_t cap = JSON_OBJECT_SIZE(1) + JSON_ARRAY_SIZE(nkeys) +
                         nkeys * JSON_OBJECT_SIZE(3) + strbytes + 64;
            auto doc = makeJsonDoc("Simulator", cap);
            if (!doc) {
                snprintf(resp_body, resp_body_size, "{\"error\":\"oom\"}");
                return 500;
            }
            JsonArray arr = doc->to<JsonObject>().createNestedArray("configurationKey");
            for (auto container : containers) {
                for (size_t i = 0; i < container->size(); i++) {
                    auto *cfg = container->getConfiguration(i);
                    if (!cfg || !cfg->getKey() || !cfg->isReadable()) continue;
                    JsonObject item = arr.createNestedObject();
                    item["key"] = cfg->getKey();
                    item["readonly"] = cfg->isReadOnly();
                    switch (cfg->getType()) {
                        case TConfig::Int: {
                            char buf[32];
                            snprintf(buf, sizeof(buf), "%i", cfg->getInt());
                            item["value"] = buf;
                            break;
                        }
                        case TConfig::Bool:
                            item["value"] = cfg->getBool() ? "true" : "false";
                            break;
                        case TConfig::String:
                            item["value"] = cfg->getString();
                            break;
                    }
                }
            }
            size_t written = serializeJson(*doc, resp_body, resp_body_size);
            if (!written || written >= resp_body_size) {
                snprintf(resp_body, resp_body_size, "{\"error\":\"response too large\"}");
                return 500;
            }
            return 200;
        } else if (method == Method::POST) {
            const char *key = request["key"] | "";
            const char *value = request["value"] | (const char *)nullptr;
            if (!*key || !value) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"Rejected\",\"error\":\"missing key or value\"}");
                return 400;
            }
            auto configuration = getConfigurationPublic(key);
            if (!configuration) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"NotSupported\"}");
                return 200;
            }
            if (configuration->isReadOnly()) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"Rejected\"}");
                return 200;
            }

            bool convertibleInt = true;
            int numInt = 0;
            bool convertibleBool = true;
            bool numBool = false;
            int nDigits = 0, nNonDigits = 0, nDots = 0, nSign = 0;
            for (const char *c = value; *c; ++c) {
                if (*c >= '0' && *c <= '9') {
                    if (nDots == 0) {
                        nDigits++;
                        numInt *= 10;
                        numInt += *c - '0';
                    }
                } else if (*c == '.') {
                    nDots++;
                } else if (c == value && *c == '-') {
                    nSign++;
                } else {
                    nNonDigits++;
                }
            }
            if (nSign == 1) numInt = -numInt;
            if (nNonDigits > 0 || nDigits == 0 || nSign > 1 || nDots > 1) {
                convertibleInt = false;
            }
            if (nDigits > 9) convertibleInt = false;

            if (tolower((unsigned char)value[0]) == 't' && tolower((unsigned char)value[1]) == 'r' &&
                tolower((unsigned char)value[2]) == 'u' && tolower((unsigned char)value[3]) == 'e' &&
                !value[4]) {
                numBool = true;
            } else if (tolower((unsigned char)value[0]) == 'f' && tolower((unsigned char)value[1]) == 'a' &&
                       tolower((unsigned char)value[2]) == 'l' && tolower((unsigned char)value[3]) == 's' &&
                       tolower((unsigned char)value[4]) == 'e' && !value[5]) {
                numBool = false;
            } else {
                convertibleBool = false;
            }

            auto validator = getConfigurationValidator(key);
            if (validator && !(*validator)(value)) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"Rejected\"}");
                return 200;
            }

            bool reject = false;
            if (configuration->getType() == TConfig::Int && convertibleInt) {
                configuration->setInt(numInt);
            } else if (configuration->getType() == TConfig::Bool && convertibleBool) {
                configuration->setBool(numBool);
            } else if (configuration->getType() == TConfig::String) {
                configuration->setString(value);
            } else {
                reject = true;
            }

            if (reject) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"Rejected\"}");
                return 200;
            }
            if (!configuration_save()) {
                snprintf(resp_body, resp_body_size, "{\"status\":\"Rejected\",\"error\":\"save failed\"}");
                return 500;
            }
            snprintf(resp_body, resp_body_size, "{\"status\":\"%s\"}",
                     configuration->isRebootRequired() ? "RebootRequired" : "Accepted");
            return 200;
        }
        return 405;
    } else if(str_match(endpoint, "/connectors")) {
        MO_DBG_VERBOSE("query connectors");
        size_t n = mo_webapp_connector_count();
        for (size_t i = 0; i < n; i++) {
            char id[8];
            snprintf(id, sizeof(id), "%u", (unsigned)(i + 1));
            response.add(id);
        }
        status = 200;
    } else if(str_match(endpoint, "/connector/*/evse")){
        MO_DBG_VERBOSE("query evse");
        if (!have_connector || !b) {
            return 404;
        }

        if (method == MicroOcpp::Method::POST) {
            if (request.containsKey("evPlugged") && b->set_ev_plugged) {
                b->set_ev_plugged(connectorId, request["evPlugged"]);
            }
            if (request.containsKey("evsePlugged") && b->set_evse_plugged) {
                b->set_evse_plugged(connectorId, request["evsePlugged"]);
            }
            if (request.containsKey("evReady") && b->set_ev_ready) {
                b->set_ev_ready(connectorId, request["evReady"]);
            }
            if (request.containsKey("evseReady") && b->set_evse_ready) {
                b->set_evse_ready(connectorId, request["evseReady"]);
            }
        }

        response["evPlugged"] = b->get_ev_plugged ? b->get_ev_plugged(connectorId) : false;
        response["evsePlugged"] = b->get_evse_plugged ? b->get_evse_plugged(connectorId) : false;
        response["evReady"] = b->get_ev_ready ? b->get_ev_ready(connectorId) : false;
        response["evseReady"] = b->get_evse_ready ? b->get_evse_ready(connectorId) : false;
        response["chargePointStatus"] =
            b->get_ocpp_status ? b->get_ocpp_status(connectorId) : "";
        status = 200;
    } else if(str_match(endpoint, "/connector/*/meter")){
        MO_DBG_VERBOSE("query meter");
        if (!have_connector || !b) {
            return 404;
        }

        response["energy"] = b->get_energy ? b->get_energy(connectorId) : 0;
        response["power"] = b->get_power ? b->get_power(connectorId) : 0;
        response["current"] = b->get_current ? b->get_current(connectorId) : 0.f;
        response["voltage"] = b->get_voltage ? b->get_voltage(connectorId) : 0.f;
        status = 200;
    } else if(str_match(endpoint, "/connector/*/transaction")){
        MO_DBG_VERBOSE("query transaction");
        if (!have_connector || !b) {
            return 404;
        }

        if (method == MicroOcpp::Method::POST) {
            if (request.containsKey("idTag") && b->present_nfc_tag) {
                b->present_nfc_tag(connectorId, request["idTag"] | "");
            }
        }
        response["idTag"] =
            b->get_session_id_tag ? b->get_session_id_tag(connectorId) : "";
        response["transactionId"] =
            b->get_transaction_id ? b->get_transaction_id(connectorId) : -1;
        response["authorizationStatus"] = "";
        status = 200;
    } else if(str_match(endpoint, "/connector/*/smartcharging")){
        MO_DBG_VERBOSE("query smartcharging");
        if (!have_connector || !b) {
            return 404;
        }

        response["maxPower"] = b->get_smart_charging_max_power
                                   ? b->get_smart_charging_max_power(connectorId)
                                   : 0;
        response["maxCurrent"] =
            b->get_smart_charging_max_current
                ? b->get_smart_charging_max_current(connectorId)
                : 0.f;
        status = 200;
    } else {
        return 404;
    }

    if (response.overflowed()) {
        return 500;
    }

    std::string out;
    serializeJson(response, out);
    if (out.length() >= resp_body_size) {
        return 500;
    }

    if (!out.empty()) {
        sprintf(resp_body, "%s", out.c_str());
    }

    return status;
}

int mocpp_api2_call(const char *uri_raw, size_t uri_raw_len, MicroOcpp::Method method, const char *query_raw, size_t query_raw_len, char *resp_body, size_t resp_body_size) {

    snprintf(resp_body, resp_body_size, "%s", "");
    
    struct mg_str uri = mg_str_n(uri_raw, uri_raw_len);
    struct mg_str query = mg_str_n(query_raw, query_raw_len);

    int evse_id = -1;
    int connector_id = -1;

    unsigned int num;
    struct mg_str evse_id_str = mg_http_var(query, mg_str("evse_id"));
    if (evse_id_str.buf) {
        if (!mg_str_to_num(evse_id_str, 10, &num, sizeof(num)) || num < 1 || num >= MO_NUM_EVSEID) {
            snprintf(resp_body, resp_body_size, "invalid connector_id");
            return 400;
        }
        evse_id = (int)num;
    }

    struct mg_str connector_id_str = mg_http_var(query, mg_str("connector_id"));
    if (connector_id_str.buf) {
        if (!mg_str_to_num(connector_id_str, 10, &num, sizeof(num)) || num != 1) {
            snprintf(resp_body, resp_body_size, "invalid connector_id");
            return 400;
        }
        connector_id = (int)num;
    }

    if (mg_match(uri, mg_str("/plugin"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        const MoConnectorBackend *b = mo_webapp_get_connector_backend();
        if (evse_id < 0 || !mo_webapp_connector_valid((unsigned)evse_id) || !b) {
            snprintf(resp_body, resp_body_size, "no action taken");
            return 200;
        } else {
            unsigned int id = (unsigned int)evse_id;
            bool plugged = b->get_ev_plugged ? b->get_ev_plugged(id) : false;
            snprintf(resp_body, resp_body_size, "%s",
                     plugged ? "EV already plugged" : "plugged in EV");
            if (b->set_ev_plugged) b->set_ev_plugged(id, true);
            if (b->set_ev_ready) b->set_ev_ready(id, true);
            if (b->set_evse_ready) b->set_evse_ready(id, true);
            return 200;
        }
    } else if (mg_match(uri, mg_str("/plugout"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        const MoConnectorBackend *b = mo_webapp_get_connector_backend();
        if (evse_id < 0 || !mo_webapp_connector_valid((unsigned)evse_id) || !b) {
            snprintf(resp_body, resp_body_size, "no action taken");
            return 200;
        } else {
            unsigned int id = (unsigned int)evse_id;
            bool plugged = b->get_ev_plugged ? b->get_ev_plugged(id) : false;
            snprintf(resp_body, resp_body_size, "%s",
                     plugged ? "EV already unplugged" : "unplug EV");
            if (b->set_ev_plugged) b->set_ev_plugged(id, false);
            if (b->set_ev_ready) b->set_ev_ready(id, false);
            if (b->set_evse_ready) b->set_evse_ready(id, false);
            return 200;
        }
    } else if (mg_match(uri, mg_str("/end"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        const MoConnectorBackend *b = mo_webapp_get_connector_backend();
        bool trackEvReady = false;
        size_t n = mo_webapp_connector_count();
        for (size_t i = 0; i < n; i++) {
            unsigned int id = (unsigned int)(i + 1);
            if (b && b->get_ev_ready) {
                trackEvReady |= b->get_ev_ready(id);
            }
            if (b && b->set_ev_ready) {
                b->set_ev_ready(id, false);
            }
        }
        snprintf(resp_body, resp_body_size, "%s",
                 trackEvReady ? "suspended EV" : "EV already suspended");
        return 200;
    } else if (mg_match(uri, mg_str("/state"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        struct mg_str ready_str = mg_http_var(query, mg_str("ready"));
        bool ready = true;
        if (ready_str.buf) {
            if (mg_match(ready_str, mg_str("true"), NULL)) {
                ready = true;
            } else if (mg_match(ready_str, mg_str("false"), NULL)) {
                ready = false;
            } else {
                snprintf(resp_body, resp_body_size, "invalid ready");
                return 400;
            }
        }
        const MoConnectorBackend *b = mo_webapp_get_connector_backend();
        size_t n = mo_webapp_connector_count();
        for (size_t i = 0; i < n; i++) {
            unsigned int id = (unsigned int)(i + 1);
            bool plugged = b && b->get_ev_plugged && b->get_ev_plugged(id);
            if (plugged) {
                bool wasReady = b->get_ev_ready ? b->get_ev_ready(id) : false;
                if (b->set_ev_ready) b->set_ev_ready(id, ready);
                snprintf(resp_body, resp_body_size, "%s, %s",
                         ready ? "EV suspended" : "EV not suspended",
                         wasReady ? "suspended before" : "not suspended before");
                return 200;
            }
        }
        snprintf(resp_body, resp_body_size, "no action taken - EV not plugged");
        return 200;
    } else if (mg_match(uri, mg_str("/authorize"), NULL)) {
        if (method != MicroOcpp::Method::POST) {
            return 405;
        }
        struct mg_str id = mg_http_var(query, mg_str("id"));
        if (!id.buf) {
            snprintf(resp_body, resp_body_size, "missing id");
            return 400;
        }
        struct mg_str type = mg_http_var(query, mg_str("type"));
        if (!type.buf) {
            snprintf(resp_body, resp_body_size, "missing type");
            return 400;
        }

        int ret;
        char id_buf [MO_IDTOKEN_LEN_MAX + 1];
        ret = snprintf(id_buf, sizeof(id_buf), "%.*s", (int)id.len, id.buf);
        if (ret < 0 || ret >= (int)sizeof(id_buf)) {
            snprintf(resp_body, resp_body_size, "invalid id");
            return 400;
        }
        char type_buf [128];
        ret = snprintf(type_buf, sizeof(type_buf), "%.*s", (int)type.len, type.buf);
        if (ret < 0 || ret >= (int)sizeof(type_buf)) {
            snprintf(resp_body, resp_body_size, "invalid type");
            return 400;
        }

        if (evse_id <= 0 || !mo_webapp_connector_valid((unsigned)evse_id)) {
            snprintf(resp_body, resp_body_size, "invalid evse_id");
            return 400;
        }

        const MoConnectorBackend *b = mo_webapp_get_connector_backend();
        if (!b) {
            snprintf(resp_body, resp_body_size, "no backend");
            return 500;
        }

        unsigned int cid = (unsigned int)evse_id;
        const char *tag = b->get_session_id_tag ? b->get_session_id_tag(cid) : "";
        bool trackAuthActive = tag && tag[0];

        bool ok = false;
        if (b->present_nfc_tag_typed) {
            ok = b->present_nfc_tag_typed(cid, id_buf, type_buf);
        } else if (b->present_nfc_tag) {
            b->present_nfc_tag(cid, id_buf);
            ok = true;
        }
        if (!ok) {
            snprintf(resp_body, resp_body_size, "invalid id and / or type");
            return 400;
        }

        tag = b->get_session_id_tag ? b->get_session_id_tag(cid) : "";
        bool authActive = tag && tag[0];

        snprintf(resp_body, resp_body_size, "%s",
                !trackAuthActive && authActive ? "authorize in progress" :
                trackAuthActive && !authActive ? "unauthorize in progress" :
                trackAuthActive && authActive ?  "no action taken (EVSE still authorized)" :
                                                 "no action taken (EVSE not authorized)");

        return 200;
    } else if (mg_match(uri, mg_str("/memory/info"), NULL)) {
        #if MO_OVERRIDE_ALLOCATION && MO_ENABLE_HEAP_PROFILER
        {
            if (method != MicroOcpp::Method::GET) {
                return 405;
            }

            int ret = mo_mem_write_stats_json(resp_body, resp_body_size);
            if (ret < 0 || ret >= resp_body_size) {
                snprintf(resp_body, resp_body_size, "internal error");
                return 500;
            }

            return 200;
        }
        #else
        {
            snprintf(resp_body, resp_body_size, "memory profiler disabled");
            return 404;
        }
        #endif
    } else if (mg_match(uri, mg_str("/memory/reset"), NULL)) {
        #if MO_OVERRIDE_ALLOCATION && MO_ENABLE_HEAP_PROFILER
        {
            if (method != MicroOcpp::Method::POST) {
                return 405;
            }

            MO_MEM_RESET();
            return 200;
        }
        #else
        {
            snprintf(resp_body, resp_body_size, "memory profiler disabled");
            return 404;
        }
        #endif

    }

    return 404;
}
