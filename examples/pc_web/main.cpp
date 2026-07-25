// matth-x/MicroOcpp
// Copyright Matthias Akstaller 2022 - 2024
// GPL-3.0 License
//
// PC example with web dashboard (uses webapp/ module).

#include <cstdio>
#include <cstring>
#include <signal.h>

#include <mbedtls/platform.h>

#include <MicroOcpp.h>
#include <MicroOcpp/Core/Context.h>
#include <MicroOcpp/Core/FilesystemUtils.h>
#include <MicroOcpp/Core/Memory.h>
#include <MicroOcpp/Platform.h>
#include <MicroOcppMongooseClient.h>

#include "api.h"
#include "dashboard.h"
#include "evse.h"
#include "mongoose.h"

#ifndef MO_SIM_ENDPOINT_URL
#define MO_SIM_ENDPOINT_URL "http://0.0.0.0:8000"
#endif

bool g_isOcpp201 = false;
bool g_runSimulator = true;

bool g_isUpAndRunning = false;
unsigned int g_bootNotificationTime = 0;

struct mg_mgr mgr;
MicroOcpp::MOcppMongooseClient* osock = nullptr;

#if MBEDTLS_PLATFORM_MEMORY

void* mo_mem_mbedtls_calloc(size_t n, size_t count) {
    size_t size = n * count;
    auto ptr = MO_MALLOC("MbedTLS", size);
    if (ptr) {
        memset(ptr, 0, size);
    }
    return ptr;
}

void mo_mem_mbedtls_free(void* ptr) {
    MO_FREE(ptr);
}

#endif // MBEDTLS_PLATFORM_MEMORY

static void console_out(const char* msg) {
    fputs(msg, stdout);
    fflush(stdout);
}

void mo_sim_sig_handler(int s) {
    (void)s;
    if (!g_runSimulator) {
        exit(EXIT_FAILURE);
    }
    g_runSimulator = false;
}

void load_ocpp_version(std::shared_ptr<MicroOcpp::FilesystemAdapter> filesystem) {
    MicroOcpp::configuration_init(filesystem);

#if MO_ENABLE_V201
    {
        auto protocolVersion_stored =
            MicroOcpp::declareConfiguration<const char*>("OcppVersion", "1.6", SIMULATOR_FN, false, false, false);
        MicroOcpp::configuration_load(SIMULATOR_FN);
        if (!strcmp(protocolVersion_stored->getString(), "2.0.1")) {
            g_isOcpp201 = true;
            return;
        }
    }
#endif // MO_ENABLE_V201

    g_isOcpp201 = false;
}

void app_setup(MicroOcpp::Connection& connection, std::shared_ptr<MicroOcpp::FilesystemAdapter> filesystem) {
    mocpp_initialize(connection,
                     g_isOcpp201 ? ChargerCredentials::v201("MicroOcpp Simulator", "MicroOcpp")
                                 : ChargerCredentials("MicroOcpp Simulator", "MicroOcpp"),
                     filesystem,
                     false,
                     g_isOcpp201 ? MicroOcpp::ProtocolVersion{2, 0, 1} : MicroOcpp::ProtocolVersion{1, 6});

    for (unsigned int i = 0; i < connectors.size(); i++) {
        connectors[i].setup();
    }
}

void app_loop() {
    mocpp_loop();
    for (unsigned int i = 0; i < connectors.size(); i++) {
        connectors[i].loop();
    }
}

int main() {
#if MBEDTLS_PLATFORM_MEMORY
    mbedtls_platform_set_calloc_free(mo_mem_mbedtls_calloc, mo_mem_mbedtls_free);
#endif

    mocpp_set_console_out(console_out);

    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = mo_sim_sig_handler;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(SIGINT, &sigIntHandler, NULL);

    mg_log_set(MG_LL_INFO);
    mg_mgr_init(&mgr);

    auto filesystem = MicroOcpp::makeDefaultFilesystemAdapter(MicroOcpp::FilesystemOpt::Use_Mount_FormatOnFail);

    load_ocpp_version(filesystem);

    struct mg_str api_cert = mg_file_read(&mg_fs_posix, MO_FILENAME_PREFIX "api_cert.pem");
    struct mg_str api_key = mg_file_read(&mg_fs_posix, MO_FILENAME_PREFIX "api_key.pem");

    auto api_settings_doc = MicroOcpp::FilesystemUtils::loadJson(filesystem, MO_FILENAME_PREFIX "api.jsn", "Simulator");
    if (!api_settings_doc) {
        api_settings_doc = MicroOcpp::makeJsonDoc("Simulator", 0);
    }
    JsonObject api_settings = api_settings_doc->as<JsonObject>();

    const char* api_url = api_settings["url"] | MO_SIM_ENDPOINT_URL;

    osock = new MicroOcpp::MOcppMongooseClient(&mgr,
                                               "ws://echo.websocket.events",
                                               "charger-01",
                                               "",
                                               "",
                                               filesystem,
                                               g_isOcpp201 ? MicroOcpp::ProtocolVersion{2, 0, 1}
                                                           : MicroOcpp::ProtocolVersion{1, 6});

    pc_web_register_evse_backend();

    dashboard_initialize(osock,
                         api_cert.buf ? api_cert.buf : "",
                         api_key.buf ? api_key.buf : "",
                         api_settings["user"] | "",
                         api_settings["pass"] | "");
    dashboard_listen(&mgr, api_url);
    app_setup(*osock, filesystem);

    setOnResetExecute([](bool isHard) {
        (void)isHard;
        g_runSimulator = false;
    });

    printf("[main] Dashboard: %s\n", api_url);

    while (g_runSimulator) {
        mg_mgr_poll(&mgr, 100);
        mg_dash_poll(&mgr);
        app_loop();

        if (!g_bootNotificationTime && getOcppContext()->getModel().getClock().now() >= MicroOcpp::MIN_TIME) {
            g_bootNotificationTime = mocpp_tick_ms();
        }

        if (!g_isUpAndRunning && g_bootNotificationTime && mocpp_tick_ms() - g_bootNotificationTime >= 1000) {
            printf("[Sim] Resetting maximum heap usage after boot success\n");
            g_isUpAndRunning = true;
            MO_MEM_RESET();
        }
    }

    printf("[Sim] Shutting down\n");
    MO_MEM_PRINT_STATS();

    mocpp_deinitialize();
    delete osock;
    mg_mgr_free(&mgr);
    free(api_cert.buf);
    free(api_key.buf);
    return 0;
}
