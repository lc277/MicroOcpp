// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#ifndef MO_SIM_LOG_H
#define MO_SIM_LOG_H

#include "mongoose.h"

#ifdef __cplusplus
extern "C" {
#endif

void sim_log_init(struct mg_mgr *mgr);
void sim_log_set_enabled(bool enabled);
void sim_log_set_level(int mg_level);  // MG_LL_*
bool sim_log_is_enabled(void);
int sim_log_get_level(void);

void sim_log_http_handler(struct mg_connection *c, int ev, void *ev_data);

#ifdef __cplusplus
}
#endif

#endif
