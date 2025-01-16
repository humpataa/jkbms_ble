#include <Arduino.h>
#include <NimBLEDevice.h>

#define serviceUUID "FFE0"
#define charUUID "FFE1"

#define JKBMSaddress "c8:47:8c:e4:56:6b"   // mac address of JKBMS

byte getInfo[] =        {0xaa, 0x55, 0x90, 0xeb, 0x96, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10};
byte getdeviceInfo[] =  {0xaa, 0x55, 0x90, 0xeb, 0x97, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11};

NimBLEAdvertisedDevice advDevice;
NimBLEClient* pClient = nullptr;
NimBLERemoteService* pSvc = nullptr;
NimBLERemoteCharacteristic* pChr = nullptr;

bool doConnect  = false;
bool isConnected = false;
bool dataSent = false;
static uint32_t scanTimeMs = 5000;          // scan time in milliseconds, 0 = scan forever

byte receivedBytes[301];
bool requestSuccessful = false;
uint32_t waitress = 0;
bool hasNewData = false;
bool isNotified = false;
int frame = 0;

uint8_t crc(const uint8_t data[], const uint16_t len) {
  uint8_t crc = 0;
  for (int i = 0; i < len; i++) {
    crc = crc + data[i];
  }
  return crc;
}

// Callbacks for device connection
class ClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient* pClient) override {
      //Serial.printf("Connected\n");
    }

    void onDisconnect(NimBLEClient* pClient, int reason) override {
        Serial.printf("%s disconnected, reason = %d\n", pClient->getPeerAddress().toString().c_str(), reason);
        isConnected = false;
        hasNewData = false;
        isNotified = false;
    }
} clientCallbacks;

// Callbacks for scan 
class ScanCallbacks : public NimBLEScanCallbacks {
    void onResult(const NimBLEAdvertisedDevice* advertisedDevice) override {
        if (advertisedDevice->getAddress().toString() == JKBMSaddress && advertisedDevice->isAdvertisingService(NimBLEUUID(serviceUUID))) {
            Serial.printf("Device found: %s\n", advertisedDevice->toString().c_str());

            // stop scan before connecting
            NimBLEDevice::getScan()->stop();

            advDevice = *advertisedDevice;

            pClient = NimBLEDevice::createClient(advDevice.getAddress());
            if (pClient) {
              //Serial.printf("New client created\n");
              pClient->setClientCallbacks(&clientCallbacks, false);
            }

            // Ready to connect now
            doConnect = true;
        }
    }

    void onScanEnd(const NimBLEScanResults& results, int reason) override {
      Serial.printf("Scan ended, device not found, device count: %d; restarting scan\n", results.getCount());
      NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }
} scanCallbacks;

// Callback for incoming data
void notifyCallback(NimBLERemoteCharacteristic* pCharacteristic, uint8_t* pData, size_t length, bool isNotify) {
/*
  Serial.print("incoming data: ");
  for (size_t i = 0; i < length; i++) {
      Serial.print(pData[i], HEX);
      Serial.print(" ");
  }
  Serial.println();

  // if it is a new package, show type
  if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90) {
    Serial.print("package type: ");
    Serial.println(pData[4]);
  }
*/
  // check if it is the beginning of a new package type 2
  if (pData[0] == 0x55 && pData[1] == 0xAA && pData[2] == 0xEB && pData[3] == 0x90 && pData[4] == 0x02) {
    //Serial.println("new package");
    hasNewData = true;
    frame = 0;
  }

  // check if it is the end of a package
  if (pData[0] == 0xAA && pData[1] == 0x55 && pData[2] == 0x90 && !isNotified) {
    // this means a first package has completely arrived, time to send second request (see main loop)
    isNotified = true;
  }

  if (hasNewData) {
    for (int i = 0; i < length; i++)  {
      receivedBytes[frame] = pData[i];
      frame++;

      // if length is reached and crc fits, the response is good
      if (frame == 300 && (uint8_t)receivedBytes[299] == crc(receivedBytes, 299)) {
        //Serial.println("package finished");

        Serial.print("SOC: ");
        Serial.println(receivedBytes[141]);
        Serial.print("SOH: ");
        Serial.println(receivedBytes[158]);
        Serial.println(receivedBytes[166] > 0?"Charge ON":"Charge OFF");
        Serial.println(receivedBytes[167] > 0?"Discharge ON":"Discharge OFF");
        Serial.println(receivedBytes[169] > 0?"Balancing ON":"Balancing OFF");
        Serial.print("Temp1: ");
        Serial.println((float) (receivedBytes[130] * 0.1f));
        //Serial.println((float) (((receivedBytes[130] << 8) | receivedBytes[131]) * 0.1f));
        Serial.print("Temp2: ");
        Serial.println((float) (receivedBytes[132] * 0.1f));
        //Serial.println( (float) (((receivedBytes[132] << 8) | receivedBytes[133]) * 0.1f) );

        if (pClient) pClient->disconnect();
        hasNewData = false;
        isNotified = false;
        requestSuccessful = true;
      }

      if (frame > 300) {
        Serial.println("package doesn't look good");
        return;
      }
    }
  }
}

bool connectToServer() {

  dataSent = false;

  // Check if we have a client we should reuse first
  if (NimBLEDevice::getCreatedClientCount()) {
      pClient = NimBLEDevice::getClientByPeerAddress(advDevice.getAddress());
  }

  if (!pClient->isConnected()) {
      // try to connect several times in case it doesn't work right away
      int tries = 0;
      bool connected = false;

      do {
        connected = pClient->connect(&advDevice);
        delay(200);
      } while (!connected && tries++ < 10);

      if (!connected) {
          // Created a client but failed to connect, don't need to keep it as it has no data
          NimBLEDevice::deleteClient(pClient);
          Serial.printf("Failed to connect, deleted client\n");
          return false;
      }
  }

  Serial.printf("Connected to: %s RSSI: %d\n", pClient->getPeerAddress().toString().c_str(), pClient->getRssi());

  pSvc = pClient->getService(NimBLEUUID(serviceUUID));
  if (pSvc) {
    pChr = pSvc->getCharacteristic(NimBLEUUID(charUUID));
    if (pChr) {

      // activate notify and set callback
      if (pChr->canNotify()) {
          pChr->subscribe(true, notifyCallback);
      }

      isConnected = true;
      waitress = millis();
    }
  } else {
    Serial.printf("service not found, disconnecting\n");
    Serial.println(pClient->disconnect());
  }

  return true;
}

void setup() {
  Serial.begin(115200);

  NimBLEDevice::init("JKBMS Checker");

  // Optional: set the transmit power
  //NimBLEDevice::setPower(3); // 3dbm
  NimBLEScan* pScan = NimBLEDevice::getScan();

  // Set the callbacks to call when scan events occur, no duplicates
  pScan->setScanCallbacks(&scanCallbacks, false);

  // Set scan interval (how often) and window (how long) in milliseconds
  pScan->setInterval(100);
  pScan->setWindow(100);
  pScan->setActiveScan(true);

  pScan->start(scanTimeMs);
}

void loop() {
    // Loop here until we find a device we want to connect to
    delay(10);

    // found a device, let's connect
    if (doConnect) {
        doConnect = false;
        if (!connectToServer()) {
          Serial.printf("Failed to connect, starting scan\n");
          delay(1000);
          NimBLEDevice::getScan()->start(scanTimeMs, false, true);
        }
    }

    // send first request when connected
    if (isConnected && !isNotified && !dataSent && pChr) {
      dataSent = true;
      pChr->writeValue(getInfo, sizeof(getInfo), false);
    }

    // send second request when first has been approved
    if (isConnected && isNotified && dataSent && pChr) {
      dataSent = false;
      pChr->writeValue(getdeviceInfo, sizeof(getdeviceInfo), false);
    }

    // all is good, repeat after some time
    if (requestSuccessful) {
      requestSuccessful = false;
      
      Serial.printf("Finished.\n");
      delay(60000);
      if (pClient) pClient->disconnect();
      delay(3000);
      NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }

    // if device has been connected for more than 20 s without proper response: startover
    if (isConnected && millis() > waitress + 20000) {

      requestSuccessful = false;
      if (pClient) pClient->disconnect();
      Serial.printf("Silence ... startover ...\n");
      delay(1000);
      NimBLEDevice::getScan()->start(scanTimeMs, false, true);
    }
}
