#define SLAVE_PTS_PIN TX
#define MASTER_READY_PIN RX

//Core
#if PLATFORM_ID==0
#define SLAVE_ALERT_PIN 16
#define SLAVE_SELECT A2
#endif

//Photon
#if PLATFORM_ID==6
#define SLAVE_ALERT_PIN 16
#define SLAVE_SELECT A2
#endif

//P1
#if PLATFORM_ID==8
#define SLAVE_ALERT_PIN A0
#define SLAVE_SELECT DAC
#endif

//Electron
#if PLATFORM_ID==10
#define SLAVE_ALERT_PIN 16
#define SLAVE_SELECT A2
#endif

#define CLOUD_DOMAIN "device.spark.io"
// #define CLOUD_DOMAIN "10.1.10.175"
#define RX_BUFFER 1024
#define TX_BUFFER RX_BUFFER
#define TCPCLIENT_BUF_MAX_SIZE TX_BUFFER
#define MAX_CLIENTS 4
#define SPI_HEADER_SIZE 3
#define BLE_HEADER_SIZE 2

#define NRF51_SPI_BUFFER_SIZE 255

SYSTEM_MODE(SEMI_AUTOMATIC);

/**@brief Gateway Protocol states. */
typedef enum
{
    SOCKET_DATA_SERVICE=1,
    INFO_DATA_SERVICE,
    RESERVED_DATA_SERVICE,
    CUSTOM_DATA_SERVICE
} gateway_service_ids_t;

typedef enum
{
    SOCKET_DATA,
    SOCKET_CONNECT,
    SOCKET_DISCONNECT
} gateway_socket_function_t;

typedef struct
{
    TCPClient socket;
    bool connected = false;
} client_t;

client_t m_clients[MAX_CLIENTS];

void debugPrint(String msg) {
    Serial.println(String(millis()) + ":DEBUG: " + msg);
}

String gatewayID = "No gateway detected yet.";
int timeGatewayConnected;
bool gatewayIDDiscovered;


void handle_custom_data(uint8_t *data, int length) {
    //if you use BLE.send from any connected DK, the data will end up here
    
}

void setup() {
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
}

// bool requestIDFlag = false;
// void requestRequestID() {
//     requestIDFlag = true;
// }

void requestID() {
    uint8_t dummy[6] = {(( (6-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF00) >> 8), ( (6-SPI_HEADER_SIZE-BLE_HEADER_SIZE) & 0xFF), MAX_CLIENTS-1, INFO_DATA_SERVICE, 0, 0};
    spi_send(dummy, 6);
}

// Timer timer(20000, requestRequestID, true);

char hexToAscii(uint8_t byte)
{
    static const char asciimap[] = "0123456789abcdef";
    return asciimap[byte & 0x0f];
}

void parseID(char *destination, uint8_t *buffer)
{
    int gatewayIndex = 0;
    for (int i = 0; i < 12; i++) {
        destination[gatewayIndex++] = hexToAscii( ((buffer[i] >> 4) & 0xF) );
        destination[gatewayIndex++] = hexToAscii( (buffer[i] & 0xF) );
    }
    destination[gatewayIndex] = 0x00;
}

void spi_data_process(uint8_t *buffer, uint16_t length, uint8_t clientId)
{
    uint8_t serviceID = buffer[0];
    debugPrint("Processing message of size " + String(length) + " with clientID " + String(clientId) + " and service ID " + String(serviceID));
    
    switch (serviceID) {
        case SOCKET_DATA_SERVICE:
        {
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
            char id[25];
            parseID(id, buffer+1);
            gatewayID = String(id);
            debugPrint("You're gateway ID is " + String(id));
            Particle.publish("bluz gateway device id", String(id));
            break;
        case CUSTOM_DATA_SERVICE:
            handle_custom_data(buffer+1, length-1);
            break;
    }
}

void spi_retreive() {
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
}

void spi_send(uint8_t *buf, int len) {
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

void loop() {
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