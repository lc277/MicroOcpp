// matth-x/MicroOcppSimulator
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License

#ifndef MO_DASHBOARD_H
#define MO_DASHBOARD_H

#if MO_NETLIB == MO_NETLIB_MONGOOSE

#include "mongoose.h"

namespace MicroOcpp {
class MOcppMongooseClient;
}

void dashboard_initialize(MicroOcpp::MOcppMongooseClient *osock,
                          const char *cert = "", const char *key = "",
                          const char *user = "", const char *pass = "");

void dashboard_listen(struct mg_mgr *mgr, const char *url);

void mg_dash_poll(struct mg_mgr *mgr);

#endif // MO_NETLIB == MO_NETLIB_MONGOOSE

#endif
