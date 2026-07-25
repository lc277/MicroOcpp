// Hardware / connector backend for the webapp module.
// The example (e.g. Evse simulation) registers an implementation at startup.

#ifndef MO_CONNECTOR_BACKEND_H
#define MO_CONNECTOR_BACKEND_H

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef MO_WEBAPP_MAX_CONNECTORS
#define MO_WEBAPP_MAX_CONNECTORS 2
#endif

typedef struct MoStationInfo {
    char model[64];
    char serial[64];
    char vendor[64];
    char firmware[32];
} MoStationInfo;

/*
 * connectorId is 1-based (OCPP connectorId).
 * Optional function pointers may be NULL.
 */
typedef struct MoConnectorBackend {
    size_t (*connector_count)(void);

    bool (*get_ev_plugged)(unsigned int connectorId);
    void (*set_ev_plugged)(unsigned int connectorId, bool v);
    bool (*get_evse_plugged)(unsigned int connectorId);
    void (*set_evse_plugged)(unsigned int connectorId, bool v);
    bool (*get_ev_ready)(unsigned int connectorId);
    void (*set_ev_ready)(unsigned int connectorId, bool v);
    bool (*get_evse_ready)(unsigned int connectorId);
    void (*set_evse_ready)(unsigned int connectorId, bool v);

    const char *(*get_ocpp_status)(unsigned int connectorId);
    int (*get_energy)(unsigned int connectorId);
    int (*get_power)(unsigned int connectorId);
    float (*get_current)(unsigned int connectorId);
    float (*get_voltage)(unsigned int connectorId);
    const char *(*get_session_id_tag)(unsigned int connectorId);
    int (*get_transaction_id)(unsigned int connectorId);
    int (*get_smart_charging_max_power)(unsigned int connectorId);
    float (*get_smart_charging_max_current)(unsigned int connectorId);

    void (*present_nfc_tag)(unsigned int connectorId, const char *uid);
    bool (*present_nfc_tag_typed)(unsigned int connectorId, const char *uid,
                                  const char *type);

    void (*station_load)(MoStationInfo *out);
    void (*station_save)(const MoStationInfo *in);
} MoConnectorBackend;

void mo_webapp_set_connector_backend(const MoConnectorBackend *backend);
const MoConnectorBackend *mo_webapp_get_connector_backend(void);

size_t mo_webapp_connector_count(void);
bool mo_webapp_connector_valid(unsigned int connectorId);

#ifdef __cplusplus
}
#endif

#endif
