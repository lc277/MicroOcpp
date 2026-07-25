/* matth-x/MicroOcpp
 * Copyright Matthias Akstaller 2019 - 2024
 * MIT License
 *
 * Minimal PC (Linux/WSL) example — C API (same style as examples/ESP-IDF).
 */

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include <MicroOcpp/Platform.h>
#include <MicroOcppMongooseClient_c.h>
#include <MicroOcpp_c.h>
#include <mongoose.h>

#define OCPP_BACKEND_URL   "ws://127.0.0.1:8080/ocpp/"
#define OCPP_CHARGE_BOX_ID "charger-01"

/*
 *  Settings which worked for my SteVe instance:
 *
 * #define OCPP_BACKEND_URL   "ws://192.168.178.100:8180/steve/websocket/CentralSystemService"
 * #define OCPP_CHARGE_BOX_ID "esp-charger"
 */

static volatile int g_running = 1;

static void on_signal(int sig) {
    (void)sig;
    g_running = 0;
}

static void console_out(const char* msg) {
    fputs(msg, stdout);
    fflush(stdout);
}

/* Inputs / outputs (C callbacks) */

static int energy_meter_input(void) {
    /* return energy register in watt-hours */
    return 0;
}

static void smart_charging_current_output(float limit) {
    printf("[main] Smart Charging allows maximum charge rate: %.0f\n", limit);
}

static bool connector_plugged_input(void) {
    /* return true if an EV is plugged to this EVSE */
    return false;
}

int main(void) {
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    mocpp_set_console_out(console_out);

    struct mg_mgr mgr;
    mg_mgr_init(&mgr);
    mg_log_set(MG_LL_INFO);

    struct OCPP_FilesystemOpt fsopt = {
        .use = true,
        .mount = true,
        .formatFsOnFail = true,
    };

    /*
     * WebSocket connection via MicroOcppMongoose (C API)
     */
    OCPP_Connection* osock = ocpp_makeConnection(&mgr,
                                                 OCPP_BACKEND_URL,
                                                 OCPP_CHARGE_BOX_ID,
                                                 "", /* auth key */
                                                 "", /* CA cert (needed for wss://) */
                                                 fsopt);

    /*
     * Initialize the OCPP library
     */
    ocpp_initialize(osock, "Demo Charger", "My Company Ltd.", fsopt, false, false);

    /*
     * Integrate OCPP functionality. You can leave out the following part if your EVSE doesn't need it.
     */
    ocpp_setEnergyMeterInput(energy_meter_input);
    ocpp_setSmartChargingCurrentOutput(smart_charging_current_output);
    ocpp_setConnectorPluggedInput(connector_plugged_input);

    /* ... see MicroOcpp_c.h for more settings */

    printf("[main] MicroOcpp PC example running (Ctrl+C to stop)\n");
    printf("[main] Backend: %s/%s\n", OCPP_BACKEND_URL, OCPP_CHARGE_BOX_ID);

    while (g_running) {
        /*
         * Drive Mongoose (sockets / WebSocket) and MicroOcpp
         */
        mg_mgr_poll(&mgr, 10);
        ocpp_loop();

        /*
         * Energize EV plug if OCPP transaction is up and running
         */
        if (ocpp_ocppPermitsCharge()) {
            /* OCPP set up and transaction running. Energize the EV plug here */
        } else {
            /* No transaction running at the moment. De-energize EV plug */
        }

        /*
         * Use NFC reader to start and stop transactions
         */
        if (/* RFID chip detected? */ 0) {
            const char* idTag = "0123456789ABCD"; /* e.g. RFID.readIdTag() */

            if (!ocpp_isTransactionActive()) {
                printf("[main] Begin Transaction with idTag %s\n", idTag);
                if (ocpp_beginTransaction(idTag)) {
                    printf("[main] Transaction initiated. OCPP lib will send a StartTransaction when "
                           "ConnectorPlugged Input becomes true and if the Authorization succeeds\n");
                } else {
                    printf("[main] No transaction initiated\n");
                }
            } else {
                const char* txTag = ocpp_getTransactionIdTag();
                if (txTag && strcmp(txTag, idTag) == 0) {
                    printf("[main] End transaction by RFID card\n");
                    ocpp_endTransaction(idTag, NULL);
                } else {
                    printf("[main] Cannot end transaction by RFID card (different card?)\n");
                }
            }
        }

        /* ... see MicroOcpp_c.h for more possibilities */
    }

    printf("[main] Shutting down\n");
    ocpp_deinitialize();
    ocpp_deinitConnection(osock);
    mg_mgr_free(&mgr);
    return 0;
}
