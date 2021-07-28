/*
    Video: https://www.youtube.com/watch?v=oCMOYS71NIU
    Based on Neil Kolban example for IDF: https://github.com/nkolban/esp32-snippets/blob/master/cpp_utils/tests/BLE%20Tests/SampleNotify.cpp
    Ported to Arduino ESP32 by Evandro Copercini
    updated by chegewara

   Create a BLE server that, once we receive a connection, will send periodic notifications.
   The service advertises itself as: 4fafc201-1fb5-459e-8fcc-c5c9c331914b
   And has a characteristic of: beb5483e-36e1-4688-b7f5-ea07361b26a8

   The design of creating the BLE server is:
   1. Create a BLE Server
   2. Create a BLE Service
   3. Create a BLE Characteristic on the Service
   4. Create a BLE Descriptor on the characteristic
   5. Start the service.
   6. Start advertising.

   A connect hander associated with the server starts a background task that performs notification
   every couple of seconds.
*/
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
//#include <BLE2902.h>
//#include <BLE2904.h>
#include <EEPROM.h>

BLEServer* pServer = NULL;
BLECharacteristic* pCharacteristicA = NULL;
BLECharacteristic* pCharacteristicB = NULL;
BLECharacteristic* pCharacteristicC = NULL;
BLECharacteristic* pCharacteristicD = NULL;

bool deviceConnected = false;
bool oldDeviceConnected = false;
uint32_t value = 0;
uint32_t onState = 1;

//These will need to be updated to the GPIO pins for each control circuit.
int POWER = 13; //13
int TIMER_SWITCH = 2; 
//int WIFI_CONNECTION = 15; //15
int WIFI_CLIENT_CONNECTED = 15; //16
int Timer_LED = 17;  //17
int SPEED = 14; 
int LEFT = 12; 
int RIGHT = 13;
const int ANALOG_PIN = A0;

int onoff,powerOn,pumpEnabled = 0; 

volatile byte switch_state = HIGH;
boolean pumpOn = false;
boolean timer_state = false;
boolean timer_started = false;
boolean wifi_state = false;
boolean wifi_client_conn = false;
boolean pumptimer = true;
int startup_state;

int time_on, time_off;

int characteristic_value;

int result;

//Timer variables
hw_timer_t * timer = NULL;

volatile int interruptCounter;
int totalInterruptCounter;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;
 
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  interruptCounter++;
  portEXIT_CRITICAL_ISR(&timerMux);
 
}
int time_on_value;  //number of ON minutes store in EEPROM
int time_off_value; //number of OFF minutes store in EEPROM
int ontime;   //On time setting from mobile web app
int offtime;  //Off time setting from mobile web app

// See the following for generating UUIDs:
// https://www.uuidgenerator.net/

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"      //Livewell_Timer_Service_CBUUID
#define CHARACTERISTIC_A_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a6"    //Livewell_OnOff_Switch_Characteristic_CBUUID
#define CHARACTERISTIC_B_UUID "beb5483e-36e1-4688-b7f6-ea07361b26b7"    //Livewell_OFFTIME_Characteristic_CBUUID
#define CHARACTERISTIC_C_UUID "beb5483e-36e1-4688-b7f7-ea07361b26c8"    //Livewell_ONTIME_Characteristic_CBUUIDD
#define CHARACTERISTIC_D_UUID "beb5483e-36e1-4688-b7f8-ea07361b26d9"    //Livewell_TIMER_Characteristic_CBUUID

class pumpEnableCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      Serial.println("Characteristic Callback");
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("Pump Enabled State: ");
        
        char Str[] = {value[0], value[1], value[2], value[3], value[4]};
        
        Serial.println(atoi(Str));
        pumpEnabled = atoi(Str);
        Serial.println();
        Serial.println("*********");

        if (pumpEnabled == 1)
                  {
                    startup_state = 1;
                    EEPROM.write(0,startup_state);
                    EEPROM.commit();
                    byte value = EEPROM.read(0);
                    Serial.println("Pump Enabled Status: " + String(value));
                    //timer_state = true;
                    pumpOn = true;
                    //digitalWrite(Timer_LED, HIGH);
                    //digitalWrite(TIMER_SWITCH, HIGH);
                  }
         if (pumpEnabled == 0)
                  {
                    startup_state = 0;
                    EEPROM.write(0,startup_state);
                    EEPROM.commit();
                    byte value = EEPROM.read(0);
                    Serial.println("Pump Enabled Status: " + String(value));
                    //timer_state = false;
                    //pumpOn = false;
                    //digitalWrite(Timer_LED, LOW);
                    //digitalWrite(TIMER_SWITCH, LOW);
                  }
      }
    };
};

class MyServerCallbacks: public BLEServerCallbacks {
    void onConnect(BLEServer* pServer) {
      deviceConnected = true;
    };

    void onDisconnect(BLEServer* pServer) {
      deviceConnected = false;
    }
};

class offTimeCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      Serial.println("Characteristic Callback");
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("Off Time New value: ");
        
        char Str[] = {value[0], value[1], value[2], value[3], value[4]};
        
        Serial.println(atoi(Str));
        time_off = atoi(Str);
        Serial.printf("Off Time = ");
        //offtime = time_off;
        Serial.println(time_off);
        EEPROM.write(2,time_off);
        EEPROM.commit();
        delay(500);
        byte value = EEPROM.read(2);
        Serial.println("Off Time stored in EEPROM " + String(value));
        Serial.println();
        Serial.println("*********");
      }
    };
};


class onTimeCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      Serial.println("OT - Characteristic Callback");
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("On Time New value: ");
        
        char Str[] = {value[0], value[1], value[2], value[3], value[4]};
        
        Serial.println(atoi(Str));
        time_on = atoi(Str);
        Serial.print("On Time = ");
        //ontime = time_on;
        Serial.println(time_on);
        EEPROM.write(1,time_on);
        EEPROM.commit();
        delay(500);
        byte value = EEPROM.read(2);
        Serial.println("On Time Stored in EEPROM " + String(value));
        Serial.println();
        Serial.println("*********");
      }
    };
};

class timerCallback: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      Serial.println("T - Characteristic Callback");
      if (value.length() > 0) {
        Serial.println("*********");
        Serial.print("On Time New value: ");
        
        char Str[] = {value[0], value[1], value[2], value[3], value[4]};
        
        Serial.println(atoi(Str));
        offtime = atoi(Str);
        Serial.println();
        Serial.println("*********");
     }
    };
};

void setup() {

  pinMode(POWER, OUTPUT);
  //pinMode(TIMER_SWITCH, OUTPUT);
  //pinMode(WIFI_CONNECTION, OUTPUT);
  pinMode(WIFI_CLIENT_CONNECTED, OUTPUT);
  pinMode(Timer_LED, OUTPUT);
  pinMode(SPEED, OUTPUT);
  
  digitalWrite(POWER, LOW);
  //digitalWrite(TIMER_SWITCH, LOW);
  //digitalWrite(WIFI_CONNECTION, HIGH);
  digitalWrite(WIFI_CLIENT_CONNECTED, LOW);
  digitalWrite(Timer_LED, LOW);
  digitalWrite(SPEED, LOW);
  
  Serial.begin(115200);

  Serial.println("Start up state of pump is OFF");
  timer_state = false;
  pumpOn = false;
  pinMode(TIMER_SWITCH, OUTPUT);
  digitalWrite(TIMER_SWITCH, LOW);
  digitalWrite(Timer_LED, LOW);
  
  EEPROM.begin(3); //Index of three for - On/Off state 1 or 0, OnTime value, OffTime value

  //Determine the value set for ON Time in EEPROM
  time_on_value = EEPROM.read(1);
  if (time_on_value > 0) 
  {
    //ontime = ontime_value;
    time_on = time_on_value;
    Serial.println("Startup on Time setting is " + String(time_on));
  }
    
  //Determine the value set for OFF Time inEEPROM
  time_off_value = EEPROM.read(2);
  if (time_off_value > 0) 
  {
    //offtime = offtime_value;
    time_off = time_off_value;
    Serial.println("Startup off Time setting is " + String(time_off));
  }

  //Setup timer interrupt  
  timer = timerBegin(0, 80, true);
  timerAttachInterrupt(timer, &onTimer, true);
  timerAlarmWrite(timer, 1000000, true);
  timerAlarmEnable(timer);

  // Create the BLE Device
  BLEDevice::init("SArkT");

  // Create the BLE Server
  pServer = BLEDevice::createServer();
  pServer->setCallbacks(new MyServerCallbacks());

  // Create the BLE Service
  BLEService *pService = pServer->createService(SERVICE_UUID);

  // Create a BLE Characteristic A
  pCharacteristicA = pService->createCharacteristic(
                      CHARACTERISTIC_A_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );
                    
// Create a BLE Characteristic B
  pCharacteristicB = pService->createCharacteristic(
                      CHARACTERISTIC_B_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

// Create a BLE Characteristic C
  pCharacteristicC = pService->createCharacteristic(
                      CHARACTERISTIC_C_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

// Create a BLE Characteristic D
  pCharacteristicD = pService->createCharacteristic(
                      CHARACTERISTIC_D_UUID,
                      BLECharacteristic::PROPERTY_READ   |
                      BLECharacteristic::PROPERTY_WRITE_NR  |
                      BLECharacteristic::PROPERTY_NOTIFY |
                      BLECharacteristic::PROPERTY_INDICATE
                    );

  // https://www.bluetooth.com/specifications/gatt/viewer?attributeXmlFile=org.bluetooth.descriptor.gatt.client_characteristic_configuration.xml
  // Create a BLE Descriptor
  //pCharacteristicA->addDescriptor(new BLE2902());
  //pCharacteristicB->addDescriptor(new BLE2904());

  pCharacteristicA->setCallbacks(new pumpEnableCallback());
  pCharacteristicB->setCallbacks(new offTimeCallback());
  pCharacteristicC->setCallbacks(new onTimeCallback());
  pCharacteristicD->setCallbacks(new timerCallback());

  pService->start();

  // Start advertising
  BLEAdvertising *pAdvertising = BLEDevice::getAdvertising();
  pAdvertising->addServiceUUID(SERVICE_UUID);
  pAdvertising->setScanResponse(false);
  pAdvertising->setMinPreferred(0x0);  // set value to 0x00 to not advertise this parameter
  BLEDevice::startAdvertising();
  Serial.println("Waiting a client connection to notify...");
}

void loop() {
    // notify changed value
   if (deviceConnected) {
        Serial.println("device connected");
        //value++;
        digitalWrite(WIFI_CLIENT_CONNECTED, HIGH);
        delay(1000); // bluetooth stack will go into congestion, if too many packets are sent, in 6 hours test i was able to go as low as 3ms
        //pCharacteristicB->setValue((uint8_t*)&onState, 4);
        //pCharacteristicB->notify(); 
        pCharacteristicA->setValue((uint8_t*)&pumpEnabled, 4);
        pCharacteristicA->notify(); 
    }
    // disconnecting
    if (!deviceConnected && oldDeviceConnected) {
        delay(500); // give the bluetooth stack the chance to get things ready
        digitalWrite(WIFI_CLIENT_CONNECTED, LOW);
        pServer->startAdvertising(); // restart advertising
        Serial.println("start advertising");
        oldDeviceConnected = deviceConnected;
    }
    // connecting
    if (deviceConnected && !oldDeviceConnected) {
        // do stuff here on connecting
        Serial.println(" Device is connecting");
        oldDeviceConnected = deviceConnected;  
    }
    
    if (interruptCounter > 0) {
 
      portENTER_CRITICAL(&timerMux);
      interruptCounter--;
      portEXIT_CRITICAL(&timerMux);
          
      if (pumpEnabled == 1){
          pumpOn = true;
        } else {
          totalInterruptCounter = 0;
          pumpOn = false;
        }
      
    if (pumpOn == true) {
     if (pumptimer == true){
        Serial.print("Pump is ON -");
        digitalWrite(Timer_LED, HIGH);
        digitalWrite(TIMER_SWITCH, HIGH);
        ontime++;
        pCharacteristicD->setValue((uint8_t*)&ontime, 4);
        pCharacteristicD->notify();
        Serial.print(" On Time - ");
        Serial.print(ontime);
        Serial.print(" - ");
        Serial.println(time_on);
          if (ontime == time_on) {
            Serial.println("pumpOn is FALSE");
            pumptimer = false;
            ontime = 0;
          }
        }
      if (pumptimer == false) {
        digitalWrite(Timer_LED, LOW);
        digitalWrite(TIMER_SWITCH, LOW);
        Serial.print("Pump is OFF -");
        offtime++;
        pCharacteristicD->setValue((uint8_t*)&offtime, 4);
        pCharacteristicD->notify();
        Serial.print(" Off Time - ");
        Serial.print(offtime);
        Serial.print(" - ");
        Serial.println(time_off);
        if (offtime == time_off) {
          Serial.println("pumpOn is TRUE");
          pumptimer = true;
          offtime = 0;
        }
      }
      totalInterruptCounter++; 
    }
  }
}
