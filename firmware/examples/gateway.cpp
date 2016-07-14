// This #include statement was automatically added by the Particle IDE.
#include "bluz_gateway/bluz_gateway.h"
SYSTEM_MODE(SEMI_AUTOMATIC);

bluz_gateway *gateway;

void setup() {
    gateway = new bluz_gateway();
    gateway->init();
}

void loop() {
    gateway->loop();

}
