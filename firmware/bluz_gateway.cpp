#include "bluz_gateway.h"

bluz_gateway::bluz_gateway() { }

void bluz_gateway::debugPrint(String msg) {
    Serial.println(String(millis()) + ":DEBUG: " + msg);
}

void bluz_gateway::init() {
    timeGatewayConnected = -1;
    gatewayIDDiscovered = false;

    Particle.variable("gatewayID", gatewayID);

    pinMode(SLAVE_SELECT, OUTPUT);
    digitalWrite(SLAVE_SELECT, HIGH);

    SPI.begin();
    SPI.setBitOrder(LSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128);
    SPI.setDataMode(SPI_MODE0);

    pinMode(SLAVE_ALERT_PIN, INPUT_PULLDOWN);
    pinMode(SLAVE_PTS_PIN, INPUT_PULLDOWN);

    pinMode(MASTER_READY_PIN, OUTPUT);
    digitalWrite(MASTER_READY_PIN, LOW);

    Serial.begin(38400);
    debugPrint("STARTING!");
    ble_local = false;
    connectionParameters = false;
}

void bluz_gateway::send_data(gateway_service_ids_t service, uint8_t id, uint8_t *data, uint16_t length)
{
    int packet_length = 4+length;
    uint8_t dummy[4+length];
    uint8_t header[4] = {(( (packet_length-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF00) >> 8), ( (packet_length-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF), id, service};
    memcpy(dummy, header, 4);
    memcpy(dummy+4, data, length);
    spi_send(dummy, packet_length);
}

void bluz_gateway::register_data_callback(void (*dc)(uint8_t *data, uint16_t length))
{
    data_callback = dc;
}

void bluz_gateway::register_gateway_event(void (*ec)(uint8_t event, uint8_t *data, uint16_t length))
{
    event_callback = ec;
}

void bluz_gateway::set_ble_local(bool local) {
    ble_local = local;
}

void bluz_gateway::set_connection_parameters(uint16_t min, uint16_t max) {
    connectionParameters = true;
    minConnInterval = min;
    maxConnInterval = max;
}

void bluz_gateway::poll_connections()
{
    uint8_t dummy[2] = {POLL_CONNECTIONS, 0};
    send_data(INFO_DATA_SERVICE, MAX_CLIENTS-1, dummy, 2);
}
void bluz_gateway::send_peripheral_data(uint8_t id, uint8_t *data, uint16_t length)
{
    send_data(CUSTOM_DATA_SERVICE, id, data, length);
}

void bluz_gateway::requestID() {
    uint8_t dummy[2] = {GET_ID, 0};
    send_data(INFO_DATA_SERVICE, MAX_CLIENTS-1, dummy, 2);
}

void bluz_gateway::setLocalMode(bool local) {
    uint8_t dummy[2] = {SET_MODE, (uint8_t)local};
    send_data(INFO_DATA_SERVICE, MAX_CLIENTS-1, dummy, 2);
}

void bluz_gateway::sendConnectionParameters(uint16_t min, uint16_t max) {
    //TO DO: This needs more refinement on when they can be sent...
    // uint8_t dummy[9] = {(( (9-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF00) >> 8), ( (9-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF), MAX_CLIENTS-1, INFO_DATA_SERVICE, 2,
    // (uint8_t)((min & 0xFF00) >> 8), (uint8_t)(min & 0xFF), (uint8_t)((max & 0xFF00) >> 8), (uint8_t)(max & 0xFF),};
    // spi_send(dummy, 9);
}

// Timer timer(20000, requestRequestID, true);

char bluz_gateway::hexToAscii(uint8_t byte)
{
    static const char asciimap[] = "0123456789abcdef";
    return asciimap[byte & 0x0f];
}

void bluz_gateway::parseID(char *destination, uint8_t *buffer)
{
    int gatewayIndex = 0;
    for (int i = 0; i < 12; i++) {
        destination[gatewayIndex++] = hexToAscii( ((buffer[i] >> 4) & 0xF) );
        destination[gatewayIndex++] = hexToAscii( (buffer[i] & 0xF) );
    }
    destination[gatewayIndex] = 0x00;
}

void bluz_gateway::spi_data_process(uint8_t *buffer, uint16_t length, uint8_t clientId)
{
    uint8_t serviceID = buffer[0];
    debugPrint("Processing message of size " + String(length) + " with clientID " + String(clientId) + " and service ID " + String(serviceID));

    switch (serviceID) {
        case SOCKET_DATA_SERVICE:
        {
            if (ble_local && clientId == MAX_CLIENTS-1) {
                if (connectionParameters) {
                    sendConnectionParameters(minConnInterval, maxConnInterval);
                }
                debugPrint("Trying to stop the nrf51 from connecting");
                setLocalMode(ble_local);
                return;
            }
            uint8_t type = (buffer[1] >> 4 & 0x0F);
            uint8_t socketId = (buffer[1] & 0xF);
            switch (type) {
                case SOCKET_CONNECT:
                    // Particle.publish("Connecting Client", String(clientId));
                    debugPrint("Connecting Client" + String(clientId));
                    if (m_clients[clientId].connected) {
                        m_clients[clientId].socket.stop();
                        m_clients[clientId].connected = false;
                    }
                    while (!m_clients[clientId].socket.connected()) {
                        m_clients[clientId].socket.connect(CLOUD_DOMAIN, 5683);
                        delay(250);
                    }
                    m_clients[clientId].connected = true;
                    if (clientId == MAX_CLIENTS-1) {
                        //this is the gateway nrf, get the ID
                        // timer.start();
                        timeGatewayConnected = millis();
                    }
                    break;
                case SOCKET_DISCONNECT:
                    // Particle.publish("Disconnecting Client", String(clientId));
                    if (connectionParameters) {
                        sendConnectionParameters(minConnInterval, maxConnInterval);
                    }
                    debugPrint("Disconnecting Client" + String(clientId));
                    if (m_clients[clientId].connected) {
                        m_clients[clientId].socket.stop();
                        m_clients[clientId].connected = false;
                    }
                    break;
                case SOCKET_DATA:
                    m_clients[clientId].socket.write(buffer+BLE_HEADER_SIZE, length-BLE_HEADER_SIZE);
                    // Particle.publish("Wrote bytes to cloud", String(clientId) + "->Cloud  - " + String(i));
                    debugPrint(String(clientId) + "->Cloud  - " + String(length-BLE_HEADER_SIZE));
                    break;
            }
            break;
        }
        case INFO_DATA_SERVICE:
            debugPrint("Info data service with command " + String(buffer[1]));
            switch (buffer[1]) {
                case CONNECTION_RESULTS:
                    if (event_callback != NULL) {
                        event_callback(buffer[1], buffer+2, length-BLE_HEADER_SIZE);
                    }
                    break;
                case 0xb1:
                    //all bluz ID's start with b1, so this must be
                    char id[25];
                    parseID(id, buffer+1);
                    gatewayID = String(id);
                    debugPrint("You're gateway ID is " + String(id));
                    Particle.publish("bluz gateway device id", String(id));
                    break;
            }
            break;
        case CUSTOM_DATA_SERVICE:
            if (data_callback != NULL) {
                data_callback(buffer+1, length-1);
            }
            break;
    }
}

void bluz_gateway::spi_retreive() {
    debugPrint("In SPI Receive");
    digitalWrite(MASTER_READY_PIN, HIGH);
    while (digitalRead(SLAVE_ALERT_PIN) == LOW) { }
    digitalWrite(MASTER_READY_PIN, LOW);
    debugPrint("Handshake complete");

    //get the length of data available to read
    digitalWrite(SLAVE_SELECT, LOW);
    uint8_t byte1 = SPI.transfer(0xFF);
    uint8_t byte2 = SPI.transfer(0xFF);
    digitalWrite(SLAVE_SELECT, HIGH);

    //if the nrf51 isn't ready yet, we receve 0xAA, so we wait
    while (byte1 == 0xAA && byte2 == 0xAA) {
        digitalWrite(SLAVE_SELECT, LOW);
        byte1 = SPI.transfer(0xFF);
        byte2 = SPI.transfer(0xFF);
        digitalWrite(SLAVE_SELECT, HIGH);
    }
    delay(2);

    int serialBytesAvailable = (byte1 << 8) | byte2;
    debugPrint("Receiving SPI data of size " + String(serialBytesAvailable));

    //if there is no data, we're done
    if (serialBytesAvailable == 0) { return; }

    uint8_t tx_buffer[TX_BUFFER];
    //read the data one chunk at a time
    for (int chunkIndex = 0; chunkIndex < serialBytesAvailable; chunkIndex+=NRF51_SPI_BUFFER_SIZE)
    {
        while (digitalRead(SLAVE_ALERT_PIN) == LOW) { }
        digitalWrite(SLAVE_SELECT, LOW);
        uint16_t chunkSize = (serialBytesAvailable-chunkIndex > NRF51_SPI_BUFFER_SIZE ? NRF51_SPI_BUFFER_SIZE : serialBytesAvailable-chunkIndex);
        debugPrint("Reading chunk of size " + String(chunkSize));
        for (int innerIndex = 0; innerIndex < chunkSize; innerIndex++) {
            tx_buffer[chunkIndex+innerIndex] = SPI.transfer(0xFF);
        }
        digitalWrite(SLAVE_SELECT, HIGH);

        //give the nrf51 time to resognize end of transmission and set SA back to LOW
        delay(2);

        //if the nrf51 wasn't ready yet, we will receive this
        if (tx_buffer[chunkIndex] == 0xAA && tx_buffer[chunkIndex+chunkSize-1] == 0xAA) {
            chunkIndex-=NRF51_SPI_BUFFER_SIZE;
        }
    }

    //now process the data one message at a time
    int msgPointer = 0;
    while (msgPointer < serialBytesAvailable) {

        int msgLength = (tx_buffer[msgPointer] << 8) | tx_buffer[msgPointer+1];
        uint8_t clientId = tx_buffer[msgPointer+2];
        //move the pointer past the header
        msgPointer += SPI_HEADER_SIZE;

        spi_data_process(tx_buffer+msgPointer, msgLength+BLE_HEADER_SIZE, clientId);
        debugPrint("Read length = " + String(msgLength));
        msgPointer += msgLength+BLE_HEADER_SIZE;
    }
    debugPrint("All done in SPI Retreive");
}

void bluz_gateway::spi_send(uint8_t *buf, int len) {
    uint8_t rxBuffer[len];
    debugPrint("Starting to send data");
    //nrf51822 can't handle SPI data in chunks bigger than 256 bytes. split it up
    for (int i = 0; i < len; i+=254) {
        uint16_t size = (len-i > 254 ? 254 : len-i);

        digitalWrite(SLAVE_SELECT, LOW);
        for (int j = 0; j < size; j++) {
            rxBuffer[j+1] = SPI.transfer(buf[j+i]);
        }
        if (size >= 254) {
            SPI.transfer(0x01);
        }
        digitalWrite(SLAVE_SELECT, HIGH);
        delay(50);

        //if the nrf51 wasn't ready yet, we will receive this
        if (rxBuffer[i] == 0xAA && rxBuffer[i+size-1] == 0xAA) {
            i-=254;
            debugPrint("Resetting the counter since i only see AA");
        }
    }
    debugPrint("Completed Sending");
}

void bluz_gateway::loop() {
#if PLATFORM_ID==10
    //Electron, just connect
    Particle.connect();
#else
    //WiFi device, enter listening mode if need be
    if (!Particle.connected()) {
        Particle.connect();
        if (!waitFor(Particle.connected, 60000)) {
            WiFi.listen();
        }
    }
#endif

    for (int clientId = 0; clientId < MAX_CLIENTS; clientId++) {
        if (!m_clients[clientId].connected) {continue;}
        if (!m_clients[clientId].socket.connected()) {
            // debugPrint("We think we are connected, but this socket is closed: " + String(clientId));
        }
        int bytesAvailable = m_clients[clientId].socket.available();
        if (bytesAvailable > 0) {
            //Spark devices only support 128 byte buffer, but we want one SPI transaction, so buffer the data
            debugPrint("Receiving Network data of size " + String(bytesAvailable));

            uint8_t rx_buffer[RX_BUFFER+BLE_HEADER_SIZE+SPI_HEADER_SIZE];
            int rx_buffer_filled = BLE_HEADER_SIZE+SPI_HEADER_SIZE;
            while (bytesAvailable > 0) {
                for (int i = 0; i < bytesAvailable; i++) {
                    rx_buffer[i+rx_buffer_filled] = m_clients[clientId].socket.read();
                }
                rx_buffer_filled += bytesAvailable;
                bytesAvailable = m_clients[clientId].socket.available();
            }

            //add SPI header
            rx_buffer[0] = (( (rx_buffer_filled-BLE_HEADER_SIZE-SPI_HEADER_SIZE) & 0xFF00) >> 8);
            rx_buffer[1] = ( (rx_buffer_filled-BLE_HEADER_SIZE-SPI_HEADER_SIZE) & 0xFF);
            rx_buffer[2] = (uint8_t)clientId;
            //add BLE header, default to socket id of 0 for now since we only support one at the moment
            rx_buffer[3] = SOCKET_DATA_SERVICE;
            rx_buffer[4] = ((SOCKET_DATA << 4) & 0xF0) | (0 & 0x0F);;

            // Spark.publish("Bytes Available", String(rx_buffer_filled));

            spi_send(rx_buffer, rx_buffer_filled);
            // Particle.publish("Sending bytes through SPI", String(clientId) + "->BLE    - " + String(rx_buffer_filled));
            debugPrint(String(clientId) + "->BLE    - " + String(rx_buffer_filled));
            //now send the data
            // Spark.publish("Pushed this many bytes through SPI", String(totalBytes));
        }
    }
    if (digitalRead(SLAVE_PTS_PIN))
    {
        spi_retreive();
    }
    if (timeGatewayConnected > 0 && millis() - timeGatewayConnected > 20000 && !gatewayIDDiscovered) {
        debugPrint("Asking for id");
        requestID();
        gatewayIDDiscovered = true;
        // requestIDFlag = false;
    }
}