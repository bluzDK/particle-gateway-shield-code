// This #include statement was automatically added by the Particle IDE.
#include "bluz_gateway/bluz_gateway.h"
SYSTEM_MODE(SEMI_AUTOMATIC);

bluz_gateway gateway;

void setup() {
    gateway.init();
}

void loop() {
    gateway.loop();
    
}
