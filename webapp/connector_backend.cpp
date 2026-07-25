// matth-x/MicroOcpp
// MIT License

#include "connector_backend.h"

static const MoConnectorBackend *s_backend = nullptr;

void mo_webapp_set_connector_backend(const MoConnectorBackend *backend) {
    s_backend = backend;
}

const MoConnectorBackend *mo_webapp_get_connector_backend(void) {
    return s_backend;
}

size_t mo_webapp_connector_count(void) {
    if (!s_backend || !s_backend->connector_count) {
        return 0;
    }
    size_t n = s_backend->connector_count();
    if (n > MO_WEBAPP_MAX_CONNECTORS) {
        n = MO_WEBAPP_MAX_CONNECTORS;
    }
    return n;
}

bool mo_webapp_connector_valid(unsigned int connectorId) {
    return connectorId >= 1 && connectorId <= mo_webapp_connector_count();
}
