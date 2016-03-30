#define SLAVE_PTS_PIN TX
#define MASTER_READY_PIN RX

//Photon
#if PLATFORM_ID==6
#define SLAVE_ALERT_PIN 16
#endif

//P1
#if PLATFORM_ID==8
#define SLAVE_ALERT_PIN 16
#endif

#define CLOUD_DOMAIN "device.spark.io"
// #define CLOUD_DOMAIN "10.1.10.175"
#define RX_BUFFER 1024
#define TX_BUFFER RX_BUFFER
#define TCPCLIENT_BUF_MAX_SIZE TX_BUFFER
#define MAX_CLIENTS 9
#define SPI_HEADER_SIZE 4

#define NRF51_SPI_BUFFER_SIZE 255

/**@brief Gateway Protocol states. */
typedef enum
{
    CONNECT,
    DISCONNECT,
    DATA
} gateway_function_t;

typedef struct
{
    TCPClient socket;
    bool connected = false;
} client_t;

client_t m_clients[MAX_CLIENTS];

void debugPrint(String msg) {
    Serial.println(String(millis()) + ":DEBUG: " + msg);
}

void setup() {
    pinMode(A2, OUTPUT);

    SPI.begin();
    SPI.setBitOrder(LSBFIRST);
    SPI.setClockDivider(SPI_CLOCK_DIV128);
    SPI.setDataMode(SPI_MODE0);

    pinMode(SLAVE_ALERT_PIN, INPUT);
    pinMode(SLAVE_PTS_PIN, INPUT);
    
    pinMode(MASTER_READY_PIN, OUTPUT);
    digitalWrite(MASTER_READY_PIN, LOW);
    
    Serial.begin(38400);
    debugPrint("STARTING!");
}

void spi_data_process(uint8_t *buffer, uint16_t length, uint8_t clientId, uint8_t type) {
    debugPrint("Processing message of size " + String(length) + " with clientID " + String(clientId) + " and type " + String(type));

    switch (type) {
        case CONNECT:
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
            break;
        case DISCONNECT:
            // Particle.publish("Disconnecting Client", String(clientId));
            debugPrint("Disconnecting Client" + String(clientId));
            if (m_clients[clientId].connected) {
                m_clients[clientId].socket.stop();
                m_clients[clientId].connected = false;
            }
            break;
        case DATA:
            m_clients[clientId].socket.write(buffer, length);
            // Particle.publish("Wrote bytes to cloud", String(clientId) + "->Cloud  - " + String(i));
            debugPrint(String(clientId) + "->Cloud  - " + String(length));
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
    digitalWrite(A2, LOW);
    uint8_t byte1 = SPI.transfer(0xFF);
    uint8_t byte2 = SPI.transfer(0xFF);
    digitalWrite(A2, HIGH);
    
    //if the nrf51 isn't ready yet, we receve 0xAA, so we wait
    while (byte1 == 0xAA && byte2 == 0xAA) { 
        digitalWrite(A2, LOW);
        byte1 = SPI.transfer(0xFF);
        byte2 = SPI.transfer(0xFF);
        digitalWrite(A2, HIGH);   
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
        digitalWrite(A2, LOW);
        uint16_t chunkSize = (serialBytesAvailable-chunkIndex > NRF51_SPI_BUFFER_SIZE ? NRF51_SPI_BUFFER_SIZE : serialBytesAvailable-chunkIndex);
        debugPrint("Reading chunk of size " + String(chunkSize));
        for (int innerIndex = 0; innerIndex < chunkSize; innerIndex++) {
            tx_buffer[chunkIndex+innerIndex] = SPI.transfer(0xFF);
        }
        digitalWrite(A2, HIGH);
        
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
        uint8_t type = tx_buffer[msgPointer+3];
        //move the pointer past the header
        msgPointer += 4;
        
        spi_data_process(tx_buffer+msgPointer, msgLength, clientId, type);
        
        msgPointer += msgLength;
    }
}

void spi_send(uint8_t *buf, int len) {
    uint8_t rxBuffer[len];
    debugPrint("Starting to send data");
    //nrf51822 can't handle SPI data in chunks bigger than 256 bytes. split it up
    for (int i = 0; i < len; i+=254) {
        uint16_t size = (len-i > 254 ? 254 : len-i);
    	
    	digitalWrite(A2, LOW);
    	for (int j = 0; j < size; j++) {
    	    rxBuffer[j+1] = SPI.transfer(buf[j+i]);
    	}
    	if (size >= 254) {
    	    SPI.transfer(0x01);
    	} 
        digitalWrite(A2, HIGH);	
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
    for (int clientId = 0; clientId < MAX_CLIENTS; clientId++) {
        if (!m_clients[clientId].connected) {continue;}
        if (!m_clients[clientId].socket.connected()) {
            debugPrint("We think we are connected, but this socket is closed: " + String(clientId));
        }
        int bytesAvailable = m_clients[clientId].socket.available();
        if (bytesAvailable > 0) {
            //Spark devices only support 128 byte buffer, but we want one SPI transaction, so buffer the data
            
            uint8_t rx_buffer[RX_BUFFER+SPI_HEADER_SIZE];
            int rx_buffer_filled = SPI_HEADER_SIZE;
            while (bytesAvailable > 0) {
                for (int i = 0; i < bytesAvailable; i++) {
                    rx_buffer[i+rx_buffer_filled] = m_clients[clientId].socket.read();
                }
                rx_buffer_filled += bytesAvailable; 
                bytesAvailable = m_clients[clientId].socket.available();
            }

            rx_buffer[0] = (( (rx_buffer_filled-SPI_HEADER_SIZE) & 0xFF00) >> 8);
            rx_buffer[1] = ( (rx_buffer_filled-SPI_HEADER_SIZE) & 0xFF);
            rx_buffer[2] = (uint8_t)clientId;
            rx_buffer[3] = DATA;
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
}
