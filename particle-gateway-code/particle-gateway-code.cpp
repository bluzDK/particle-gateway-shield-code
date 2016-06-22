#define GW_DEBUG 1
//#define GW_CLOUD_DEBUG 1

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
#define CLOUD_PORT 5683
#define CLIENT_BUF_MAX_SIZE 1024
#define RX_BUFFER CLIENT_BUF_MAX_SIZE
#define TX_BUFFER CLIENT_BUF_MAX_SIZE
#define MAX_CLIENTS 4
#define MAX_CLIENT_SOCKETS 2
#define SPI_HEADER_SIZE 3
#define BLE_HEADER_SIZE 2

#define NRF51_SPI_BUFFER_SIZE 255

int32_t __time_out;
int64_t __start_time;
#define SET_TIMEOUT(a) { __time_out=(a); __start_time=millis(); }
#define TIMED_OUT() (millis() > (__start_time + __time_out))

SYSTEM_MODE(SEMI_AUTOMATIC);

#include <algorithm>

/**@brief Gateway Protocol states. */
typedef enum {

    SOCKET_DATA_SERVICE=1,
    INFO_DATA_SERVICE,
    RESERVED_DATA_SERVICE,
    CUSTOM_DATA_SERVICE,
    
    __GSID__END /* keep this one last and don't remove */
} gateway_service_ids_t;
const int MAX_GW_SERVICE_ID = (__GSID__END - SOCKET_DATA_SERVICE);

typedef enum {
    SOCKET_DATA,
    SOCKET_CONNECT,
    SOCKET_DISCONNECT,
    SOCKET_CONNECTED, // also provides remote IP
    SOCKET_FAILED,
} gateway_socket_function_t;

typedef struct {
    TCPClient sockets[MAX_CLIENT_SOCKETS];
    bool lastKnownState[MAX_CLIENT_SOCKETS];
} client_t;

client_t m_clients[MAX_CLIENTS];

void debugPrint(String eventName, String eventData = "") 
{
#ifdef GW_DEBUG
    Serial.println(String(millis()) + ":DEBUG: " + eventName + ((eventData=="") ? "" : ": "+eventData) );
 #ifdef GW_CLOUD_DEBUG
    if (eventData != "") Particle.publish("GW:"+eventName, eventData);
 #endif
#endif
    return;
}

String gatewayID = "No gateway detected yet.";
int timeGatewayConnected;
bool gatewayIDDiscovered;

//////////////////////////////////////////////////////////////////
//////       C U S T O M   D A T A   H A N D L I N G        //////  
//////////////////////////////////////////////////////////////////
//  if you use BLE.send from any connected DK, the data will    // 
//  end up in ths function ...  at the  ** END OF THIS FILE **  //
void handle_custom_data(uint8_t, int);                          //
//////////////////////////////////////////////////////////////////

void setup() 
{

    for (uint8_t clientId = 0; clientId < MAX_CLIENTS; clientId++) {
        for (uint8_t socketId = 0; socketId < MAX_CLIENT_SOCKETS; socketId++) {
            m_clients[clientId].lastKnownState[socketId] = false;
        }
    }

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
    delay(2000); // so we don't miss the first few lines of debug data
    debugPrint("STARTING!");
}

// bool requestIDFlag = false;
// void requestRequestID() {
//     requestIDFlag = true;
// }

void requestID()
{
    const int packetSize = 6;
    const uint16_t dataLen = packetSize-SPI_HEADER_SIZE-BLE_HEADER_SIZE; 
    uint8_t packet[packetSize] = 
    {
        (uint8_t)((dataLen & 0xFF00) >> 8),  // data length high byte 
        (uint8_t)(dataLen & 0xFF),           // data length low byte
        MAX_CLIENTS-1,                       // gateway's NRF51 client ID
        INFO_DATA_SERVICE,
        0,
        0
    };
    spi_send(packet, packetSize);
}

char *gatewayIDtoCString(uint8_t *buffer)
{
    const int id_length = 12;
    static char output[id_length*2]; // two hex digits per byte
    int i;
    for (i = 0; i < id_length; i++) {
        itoa(buffer[i], output+i*2, 16);
    }
    output[i*2] = '\0'; // string termination
    return output;
}

// process messages coming FROM the BLE network going TO the cloud
void spi_data_process(uint8_t *buffer, uint16_t length, uint8_t clientId)
{
    uint8_t serviceID = buffer[0];
    if (serviceID > MAX_GW_SERVICE_ID) return; // sanitize somewhat

    debugPrint("Processing message of size " + String(length) + " with clientID " + String(clientId) + " and service ID " + String(serviceID));

#ifdef GW_DEBUG
    char s[5];
    String bytes;
    for (int i = 0; i < length; i++) {
        sprintf(s, ":%02X:", buffer[i]);
        bytes += s;
    }
    debugPrint("BYTES:", bytes);
#endif

    switch (serviceID) {
        case SOCKET_DATA_SERVICE:
            {
                bool isDN;
                char *destDomain = NULL;
                IPAddress destIP(0UL);

                debugPrint("SOCKET_DATA_SERVICE :: Client ID " + String(clientId));

                uint8_t type = buffer[1] >> 4;
                uint8_t socketId = buffer[1] & 0xF;

                if (clientId >= MAX_CLIENTS || socketId >= MAX_CLIENT_SOCKETS) break; // sanitize somewhat

                uint16_t tcp_port = 0;
                uint8_t *destAddr = (&buffer[13]); // the start of the destination address data, IPv4 or Domain String                IPAddress destIP;
                
                // assume buffer[10] (socket type) == AF_INET and ignore it
                
                if (length > 13) {
                    tcp_port = buffer[11] << 8 | buffer[12];
                    // IP4 or domain name string?
                    // Domain Name strings are sent here *including* their null terminator.
                    // We assume that no domain name will be less than 4 chracters plus null terminator
                    if (length > 17 && buffer[length-1] == '\0' && buffer[length-2] != '\0') { // it's a domain name string
                        isDN = true;
                        destDomain = (char *)destAddr;
                    } else { // it must be an IP address (v4 or v6 ... but TODO: IPv6)
                        // destIP.version = 4; // We cannot set this. Currently, particle sets it to 4, always
                        isDN = false;
                        destIP.set_ipv4(destAddr[0], destAddr[1], destAddr[2], destAddr[3]);
                    }
                } else break; // invalid data length
                

                TCPClient *skt = &m_clients[clientId].sockets[socketId];
                bool *lks = &m_clients[clientId].lastKnownState[socketId];

                switch (type) {
                    
                    case SOCKET_CONNECT:
                        debugPrint(
                            "SOCKET_CONNECT",
                            "Connecting Client "+String(clientId)+"["+String(socketId)+"] " +
                            "ADDR="+(isDN ? String(destDomain) : String(destIP)) + ":" + String(tcp_port)
                        );

                        SET_TIMEOUT(30000); // probably excessive
                        while (!skt->connected()) {
                            if (TIMED_OUT()) { 
                                debugPrint("SOCKET_CONNNECT", "TIMED OUT :-/ (30s) Client "+String(clientId)+"["+String(socketId)+"]");
                                break; 
                            }
                            // skt->connect() will first close then overwrite any existing (gateway local) connection on this socketId
                            if (!isDN && *destAddr==0UL) { // no IP supplied, so use Particle's cloud [address:port]
                                skt->connect(CLOUD_DOMAIN, CLOUD_PORT);
                            } else {
                                if (isDN) {
                                    skt->connect(destDomain, tcp_port);
                                } else {
                                    skt->connect(destIP, tcp_port);
                                }
                            }
                            delay(250);
                        }

                        *lks = skt->connected();

                        debugPrint("SOCKET_CONNECT", "Client "+String(clientId)+"["+String(socketId)+"] "+((skt->connected()) ? "SUCCEEDED!" : "FAILED :-("));

                        // send result of the TCP connection attempt back to the DK
                        if (clientId != MAX_CLIENTS-1) {  // TODO: Gateway NRF doesn't know how to process this, yet
                            IPAddress remoteIP = skt->remoteIP();
                            uint16_t data_len = (remoteIP.version()==4) ? 4 : (remoteIP.version()==6) ? 16 : 0;
                            uint8_t tx_buffer[SPI_HEADER_SIZE+BLE_HEADER_SIZE+16] = {
                                // SPI header
                                (uint8_t)(data_len >> 8),
                                (uint8_t)(data_len & 0xFF), 
                                clientId,
                                //BLE header
                                SOCKET_DATA_SERVICE,
                                (uint8_t)((((skt->connected()) ? SOCKET_CONNECTED : SOCKET_FAILED) << 4) | (socketId & 0x0F)),
                            };
                            // IP Address -- IPv6 aware, though not yet tested and not currently supported by Particle firmware
                            // Note: IPAddress:: stores addresses in network byte order (little endian). But some 
                            //       overload trickery reverses the faked array[style] access for human IP address readers. 
                            //       Because the &real_address[0] is inaccesible to us (it's private), we roll our own reversed memcpy ...
                            uint8_t *first = &remoteIP[((remoteIP.version()==4) ? 3 : 15)]; // Because of the clever overloaded array style reversal thing.
                            uint8_t *last = &remoteIP[0];                                   // Ditto. In fact, last is first and first is last! (see note above)
                            uint8_t *dest = &tx_buffer[SPI_HEADER_SIZE+BLE_HEADER_SIZE];
                            while (first <= last) *(dest++) = *(last--);
                            spi_send(tx_buffer, SPI_HEADER_SIZE+BLE_HEADER_SIZE+data_len);
                            // NOTE: bluz firmware should assume that (data_len > 4) means an IPv6 address was sent 
                        }

                        if (clientId == MAX_CLIENTS-1 && skt->connected()) {
                            //this is the gateway nrf, get the ID
                            timeGatewayConnected = millis();
                        }
                        break;
                        
                    case SOCKET_DISCONNECT:
                        debugPrint("SOCKET_DISCONNECT", "Client "+String(clientId)+"["+String(socketId)+"]");
                        if (skt->connected()) {
                            skt->stop(); 
                            *lks = false; 
                        }
                        break;
                        
                    case SOCKET_DATA:
                        skt->write(buffer+BLE_HEADER_SIZE, length-BLE_HEADER_SIZE);
                        debugPrint("SOCKET_DATA", "Client " + String(clientId) + "["+String(socketId)+"]->Cloud  - " + String(length-BLE_HEADER_SIZE));
                        break;
                }
                break;
            }
        case INFO_DATA_SERVICE:
            gatewayID = gatewayIDtoCString(buffer+1);
            gatewayIDDiscovered = true;
            debugPrint("GATEWAY_ID", gatewayID);
            break;
        case CUSTOM_DATA_SERVICE:
            handle_custom_data(buffer+1, length-1);
            break;
    }
}

void spi_retreive() 
{
    debugPrint("INCOMING SPI :");
    digitalWrite(MASTER_READY_PIN, HIGH);
    SET_TIMEOUT(200); while (digitalRead(SLAVE_ALERT_PIN) == LOW) { if (TIMED_OUT()) return; }
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
    for (int chunkIndex = 0; chunkIndex < serialBytesAvailable; chunkIndex+=NRF51_SPI_BUFFER_SIZE) {
        SET_TIMEOUT(200); while (digitalRead(SLAVE_ALERT_PIN) == LOW) { if (TIMED_OUT()) return; }
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
        msgPointer += SPI_HEADER_SIZE; //move the pointer past the header
        
        spi_data_process(tx_buffer+msgPointer, msgLength+BLE_HEADER_SIZE, clientId);
        
        msgPointer += msgLength+BLE_HEADER_SIZE;
    }
}

void spi_send(uint8_t *buf, int len) 
{
    uint8_t rxBuffer[len];
    debugPrint("SPI:: Sending " + String(len) + " bytes for Client " + String(buf[2]) + "["+String(buf[4]&0xF) +"] ...");
    //nrf51822 can't handle SPI data in chunks bigger than 256 bytes. split it up

#ifdef DEBUG
    char s[5];
    String bytes;
#endif
    for (int i = 0; i < len; i+=254) {
        uint16_t size = (len-i > 254 ? 254 : len-i);

        digitalWrite(SLAVE_SELECT, LOW);
        for (int j = 0; j < size; j++) {
            rxBuffer[j+1] = SPI.transfer(buf[j+i]);
#ifdef DEBUG
            sprintf(s, ":%02X:", buf[j+i]);
            bytes += s;
#endif
        }
        if (size >= 254) {
            SPI.transfer(0x01);
        }
        digitalWrite(SLAVE_SELECT, HIGH);
        delay(50);

        //if the nrf51 wasn't ready yet, we will receive this
        if (rxBuffer[i] == 0xAA && rxBuffer[i+size-1] == 0xAA) {
            i-=254;
            debugPrint("Resetting the counter since I only see AA");
        }
    }
    debugPrint("BYTES:", bytes);
}

void loop() 
{
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

    if (!Particle.connected()) return;

    // process messages coming FROM the cloud going TO the BLE network
    for (uint8_t clientId = 0; clientId < MAX_CLIENTS; clientId++) {
        for (uint8_t socketId = 0; socketId < MAX_CLIENT_SOCKETS; socketId++) {

            TCPClient *skt = &m_clients[clientId].sockets[socketId];
            bool *lks = &m_clients[clientId].lastKnownState[socketId];

            if (!skt->connected()) {
                if (*lks) { // if we thought we were connected but actually aint ... 
                    *lks = false; 
                    
                    // Notify the DK ...
                    if (clientId != MAX_CLIENTS-1) { // TODO: Gateway NRF doesn't know how to process this, yet
                        // notify the DK
                        uint8_t tx_buffer[5];
                        // SPI header
                        tx_buffer[0] = 0;
                        tx_buffer[1] = 0;
                        tx_buffer[2] = clientId;
                        //add BLE header
                        tx_buffer[3] = SOCKET_DATA_SERVICE;
                        tx_buffer[4] = (SOCKET_DISCONNECT << 4) | (socketId & 0x0F);
                        
                        spi_send(tx_buffer, 5);
                    }
                }    
                continue; // to next in for loop
                
            } else { // this socket is connected. Check for inbound ...
            
                int bytesAvailable = skt->available();
                if (bytesAvailable > 0) {
                    //Spark devices only support a 128 byte buffer, but we want one SPI transaction, so buffer the data
                    uint8_t rx_buffer[RX_BUFFER+BLE_HEADER_SIZE+SPI_HEADER_SIZE];
                    int rx_buffer_filled = BLE_HEADER_SIZE+SPI_HEADER_SIZE;
                    while (bytesAvailable > 0) {
                        for (int i = 0; i < bytesAvailable; i++) {
                            rx_buffer[i+rx_buffer_filled] = skt->read();
                        }
                        rx_buffer_filled += bytesAvailable;
                        bytesAvailable = skt->available();
                    }
                    //set SPI header
                    uint16_t dataLength = rx_buffer_filled-BLE_HEADER_SIZE-SPI_HEADER_SIZE;
                    rx_buffer[0] = dataLength >> 8;
                    rx_buffer[1] = dataLength & 0xFF;
                    rx_buffer[2] = clientId;
                    //set BLE header
                    rx_buffer[3] = SOCKET_DATA_SERVICE;
                    rx_buffer[4] = (SOCKET_DATA << 4) | (socketId & 0x0F);
                    
                    spi_send(rx_buffer, rx_buffer_filled);
                }
            }
        }
    }
    if (digitalRead(SLAVE_PTS_PIN)) {
        spi_retreive();
    }
    if ( !gatewayIDDiscovered && timeGatewayConnected > 0 && (millis() - timeGatewayConnected > 10000) ) {
        static int count = 0;
        debugPrint("Asking for ID :: Attempt " + String(++count));
        requestID(); // gatewayIDDiscovered will be set when the data actually arrives
        timeGatewayConnected = millis(); // reset time, in case we have to try again
    }
}

///////////////////////////////////////////////////////////////
//////       C U S T O M   D A T A   H A N D L E R       //////  
///////////////////////////////////////////////////////////////
void handle_custom_data(uint8_t *data, int length) 
{
    //if you use BLE.send from any connected DK, the data will end up here
}





