// This #include statement was automatically added by the Particle IDE.
#include "bluz_gateway/bluz_gateway.h"

#define POLL_CONNECTIONS_INTERVAL 30000

SYSTEM_MODE(SEMI_AUTOMATIC);
bluz_gateway gateway;

void handle_custom_data(uint8_t *data, uint16_t length) {
    //if you use BLE.send from any connected DK, the data will end up here
    Particle.publish("Bluz Custom Data", String((char*)data));
}

void handle_gateway_event(uint8_t event, uint8_t *data, uint16_t length) {
    //will return any polling queries from the gateway here
    uint8_t rsp[2];
    rsp[0] = 'H';
    rsp[1] = 'i';

    switch (event) {
        case CONNECTION_RESULTS:
            String online_devices = "";
            for (int i = 0; i < length; i++) {
                if (data[i] == 0) {
                    online_devices +="O ";
                } else {
                    online_devices +="X ";
                    gateway.send_peripheral_data(i, rsp, 2);
                }
            }
            Particle.publish("Bluz Devices Online", online_devices);
            break;
    }
}

void setup() {
    gateway.init();
    //only set this if you want the nrf51 central to not connect, recomended for Electron gateway
    gateway.set_ble_local(true);

    //register the callback functions
    gateway.register_data_callback(handle_custom_data);
    gateway.register_gateway_event(handle_gateway_event);
}

long timeToNextPoll = POLL_CONNECTIONS_INTERVAL;
void loop() {
    gateway.loop();
    if (millis() > timeToNextPoll) {
        timeToNextPoll = POLL_CONNECTIONS_INTERVAL + millis();
        gateway.poll_connections();
    }
}}