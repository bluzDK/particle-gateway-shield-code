// This #include statement was automatically added by the Particle IDE.
#include "bluz_gateway/bluz_gateway.h"
SYSTEM_MODE(SEMI_AUTOMATIC);

bluz_gateway gateway;

void handle_custom_data(uint8_t *data, uint16_t length) {
    //if you use BLE.send from any connected DK, the data will end up here
}

void setup() {
    gateway.init();
    gateway.registerDataCallback(handle_custom_data);
}

void loop() {
    gateway.loop();
    
}
