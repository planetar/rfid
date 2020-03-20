/*
 * rfid04 has roots in bettkante fastled -> template  and rfid03
 *  fixes a number of template bugs so should be stripped down to overwrite template later
 * 
 * devel topics include:
 *  change comm from REST to MQTT cause
 *    frequent polls of rest///state lead to errors Connection refused, Timeout and break functionality
 *  do not change flags like alertDisp when read failed
 *  
 *  102 : neue Version ArduinojSON, wifiScan schaltbar
 *  103 : secrets aus include, lorien
 *  104 : fehlersuche, orgon
 *  105: anderes display (im neuen case!)
 */

 
#include <mqtt_kranich.h>
#include <wifi_orgon.h>


#include <ArduinoJson.h>
#include <U8g2lib.h>
#include <Wire.h>
#include <i2cdetect.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266mDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include <Streaming.h>
#include "CharStream.h"

#include <SPI.h>
#include <MFRC522.h>


constexpr uint8_t RST_PIN = 5;     // Configurable, see typical pin layout above
constexpr uint8_t SS_PIN = 4;     // Configurable, see typical pin layout above
 
MFRC522 rfid(SS_PIN, RST_PIN); // Instance of the class
 
MFRC522::MIFARE_Key key; 
 
// Init array that will store new NUID 
byte nuidPICC[4];


// das schmale display ist .91", 128x32  aber de passenden constructor fand ich nicht. 
// mit dem unten, der für 128x64 ist, geht es aber (und mit den versuchten Alternativen eben nicht)
// screen-inhalt wird offenar software-skaliert und sieht entsprechend aus, reicht hier aber
// _R2 heisst rotation 2x90° (also 180)
U8G2_SH1106_128X64_NONAME_F_HW_I2C display(U8G2_R2, /* reset=*/ U8X8_PIN_NONE);

const char* version = "1.05";
 
/************ WIFI and MQTT Information (CHANGE THESE FOR YOUR SETUP) ******************/


// data to conect with the rest api of the local openHab2 controller
const char* host = "smarthome.intern";
const int httpPort = 8080;
const char* HOSTNAME = "paok13";


String restUrl="http://smarthome.intern:8080/rest/items/";
String msgItem="rfid_msg";

String validItem="rfidValid";
String entryItem="rfidEntry";
String ctlItem="binState_Rfid";

// list of authorized cards (UID)
String knownCards[]={"5afa59a9","434718a9","707e1463","2cde1463","61588163"};

bool validCard=false;
int  validEntry;
bool lastState=false;

int  ledPin=D0;
int  led2Pin=D8;

bool rushLux=false;
bool showPir=false;
bool showErr=false;
bool loopDsp=false;
bool alertDsp=true;
bool toggle=false;

bool commError=false;
String commErrorMsg="";


#define INTERVAL_1 300
#define INTERVAL_2 5000
#define INTERVAL_3 60000
#define INTERVAL_4 90000
 
unsigned long time_1 = 0;
unsigned long time_2 = 0;
unsigned long time_3 = 0;
unsigned long time_4 = 0;


/**************************** FOR OTA **************************************************/
#define SENSORNAME "rfid" //change this to whatever you want to call your device



/************* MQTT TOPICS (change these topics as you wish)  **************************/
const char* state_topic = "sensor/rfid/state";
const char* set_topic =   "sensor/rfid/set";
const char* dbg_topic =   "sensor/rfid/dbg";
const char* scan_topic  = "wifiscan/rfid";

int scanId=0;
bool scanne=false;

/****************************************FOR JSON***************************************/
const int BUFFER_SIZE = JSON_OBJECT_SIZE(25);


// debug messages
const int numMsg=20;
int msgCount=0;
String msg="";
String arrMessages[numMsg];


WiFiClient espClient;
PubSubClient mqClient(espClient);

CharStream<30> strBuffer;



void setup() {
  Serial.begin(115200);
  Wire.begin(D3,D4);
  display.begin();
  i2cdetect();
  
  SPI.begin(); // Init SPI bus
  setupWifi();
  setupMq();
  setupOta();
  
  pinMode(ledPin, OUTPUT);
  pinMode(led2Pin, OUTPUT);

  debug (String(SENSORNAME)+" "+version,true);

  showLogoPage();
}

void loop() {


  ArduinoOTA.handle();
  mqClient.loop();



  timed();
}

void timed(){
    if(millis() > time_1 + INTERVAL_1){
        time_1 = millis();

        blynk();
    }
   
    if(millis() > time_2 + INTERVAL_2){
        time_2 = millis();
        
   
        // reconnect
        if (!mqClient.connected()) {
          mqConnect();
        }
        
        check_rfid();
    }
   
    if(millis() > time_3 + INTERVAL_3){
        time_3 = millis();
        
    }

    if(millis() > time_4 + INTERVAL_4){
        time_4 = millis();
 
        if (scanne) {
          scanWifi();
        }   
       
    }  
}

void blynk(){
  
  if (!validCard){
    if (alertDsp){
      
      if (toggle){
        digitalWrite (ledPin, HIGH);
        digitalWrite (led2Pin, LOW);
      }else {
        digitalWrite (ledPin, LOW);
        digitalWrite (led2Pin, HIGH);    
      }
      toggle = !toggle;
      
    }else {
        digitalWrite (ledPin, LOW);
        digitalWrite (led2Pin, LOW);   
    }
  }

   //readCtrl();   
}


void check_rfid() {
  
  checkRfid();
  if (lastState != validCard){
    reportState();
  }
  lastState=validCard;

}



void readCtrl(void){
  // die Bits fuer nicht verwandtes bleiben reserviert

  byte n=0;
  String result = getRestItemState(ctlItem);
  if (result=="fail"){
    return;
  }
  byte data = result.toInt();
  // aktiviere Display
  rushLux=(data & (1<<n));
  n++;
  // 
  showPir=(data & (1<<n));
  n++;
  showErr=(data & (1<<n));
  n++;
  //
  loopDsp=(data & (1<<n));
  n++;
  alertDsp=(data & (1<<n));
  Serial << F("rushLux: ")  << rushLux << "\n";
  Serial << F("showPir: ")  << showPir << "\n";
  Serial << F("showErr: ")  << showErr << "\n";
  Serial << F("loopDsp: ")  << loopDsp << "\n";
  Serial << F("alertDsp: ")  << alertDsp << "\n";
}

// postRestItem unzuverlaessig (Ueberlast?)
void reportState(void) {
  String haveValid,haveEntry;
  if (validCard){
      digitalWrite (ledPin, HIGH);
      digitalWrite (led2Pin, LOW);
      Serial << F("we have a winner: ") << String(validEntry) << "\n";
      postRestItem(validItem,"ON");
      postRestItem(entryItem,String(validEntry));
      haveValid="ON";
      haveEntry=String(validEntry);
  } else {
      digitalWrite (ledPin, LOW);
      Serial.print(F("niente \n")); 
      postRestItem(validItem,"OFF");
      postRestItem(entryItem,"999999999");
      haveValid="OFF";
      haveEntry="999999999";
  }
    //sendState();
  sendState(haveValid,haveEntry);  
  showState(haveValid,haveEntry); 
}

void checkRfid() {
  rfid.PCD_Init(); // Init MFRC522 
 
  for (byte i = 0; i < 6; i++) {
    key.keyByte[i] = 0xFF;
  }


  if ( ! rfid.PICC_IsNewCardPresent())
  {
    validCard=false;
    return;
  }


   if ( ! rfid.PICC_ReadCardSerial()){  
     Serial.println(F("unreadable "));
     validCard=false;
     return;     
   }
   
    MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);

    String strUID ="";
    validCard=false;

    for (byte i = 0; i < 4; i++) {
      strUID += String(rfid.uid.uidByte[i],HEX);
    }   
    for (int i=0;i<sizeof(knownCards);i++){
      if (knownCards[i]==strUID) {
        validCard=true;
        validEntry=i;
      }
    }

 
  // Halt PICC
  rfid.PICC_HaltA();
 
  // Stop encryption on PCD
  rfid.PCD_StopCrypto1();
}
 





///////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


void scanWifi(){
  WiFi.disconnect();
  delay(100);

  /*
   * pubsub mag es nicht, die ganze Liste auf einen rutsch zu posten
   * Deshalb mal ein post je netz
   * umzuordnen zu können, scanId und reportId angeben
   */
   
  scanId++;
   
  // WiFi.scanNetworks will return the number of networks found
  int netCnt = WiFi.scanNetworks();
  
  setupWifi();
  delay(50);
  if (!mqClient.connected()) {
        mqConnect();
  }  
  delay(50);    
  debug(String(netCnt)+" SSIDS erkannt",false);
  
  if (netCnt == 0) {
    Serial << F("no networks found\n");
  } else {
    //Serial << (netCnt) << F(" networks found\n");

   
    for (int i = 0; i < netCnt; ++i) {
      StaticJsonDocument<256> doc;
    
      doc["scanner"] = String(SENSORNAME); 
      doc["netCnt"] = netCnt;
      doc["scanId"] = scanId;
    
      JsonArray nets = doc.createNestedArray("nets");

      JsonObject obj = nets.createNestedObject();
      
      obj["bssid"] = WiFi.BSSIDstr(i);
      obj["ssid"] = WiFi.SSID(i);
      obj["rssi"] = WiFi.RSSI(i);
      obj["chnl"] = WiFi.channel(i);
      obj["enc"]  = WiFi.encryptionType(i);
      //obj["isHidden"] = WiFi.isHidden(i);
      
//    encryption codes  
//      TKIP (WPA) = 2
//      WEP = 5
//      CCMP (WPA) = 4
//      NONE = 7
//      AUTO = 8
            
      doc["dtlId"] = i;

      char buffer[256];
      serializeJson(doc, buffer);
      mqClient.publish(scan_topic, buffer);
      
      //Serial << WiFi.SSID(i) << F(" | ") << WiFi.RSSI(i) << F(" | ") << WiFi.BSSIDstr(i) << F(" | ") << WiFi.channel(i) << F(" | ") << WiFi.isHidden(i)  << F("\n");
      
      delay(10);

    }
  }
}




/********************************** START CALLBACK*****************************************/
void callback(char* topic, byte* payload, unsigned int length) {
 

  StaticJsonDocument<256> root;
  deserializeJson(root, payload, length);
  
  const char* on_cmd = "ON";
  const char* off_cmd = "OFF";

 if (root.containsKey("scanWifi")) {
    if (strcmp(root["scanWifi"], "ON") == 0) {
      scanne = true;
    }
    else if (strcmp(root["scanWifi"], "OFF") == 0) {
      scanne = false;
    }
  }


  if (root.containsKey("rushLux")) {
    if (strcmp(root["rushLux"], on_cmd) == 0) {
      rushLux = true;
    }
    else if (strcmp(root["rushLux"], off_cmd) == 0) {
      rushLux = false;
    }
  }

  if (root.containsKey("showPir")) {
    if (strcmp(root["showPir"], on_cmd) == 0) {
      showPir = true;
    }
    else if (strcmp(root["showPir"], off_cmd) == 0) {
      showPir = false;
    }
  }

  if (root.containsKey("showErr")) {
    if (strcmp(root["showErr"], on_cmd) == 0) {
      showErr = true;
    }
    else if (strcmp(root["showErr"], off_cmd) == 0) {
      showErr = false;
    }
  }


  // String(validEntry) 
  String haveValid = (validCard) ? "ON" : "OFF";
  sendState(haveValid,String(validEntry));
}


/********************************** START SEND STATE*****************************************/
//void sendState(String haveValid="OFF",String haveEntry="88888888") {
void sendState(String haveValid,String haveEntry) {
  StaticJsonDocument<512> root;
  
  const char* on_cmd = "ON";
  const char* off_cmd = "OFF";

  root["rfidValid"] = haveValid;
  root["rfidEntry"] = haveEntry;
  root["lastState"] = lastState;
  root["validCard"] = validCard;
 
  root["commError"] = commError;
  root["commErrorMsg"] = commErrorMsg;
  
 
  char buffer[512];
  size_t n = serializeJson(root, buffer);

}


// lastState=validCard;



///////////////////////////////////////////////////////////////////////////////

// send a message to mq
void sendDbg(String msg){
  StaticJsonDocument<512> doc;
 
  doc["dbg"]=msg;
  

  char buffer[512];
  size_t n = serializeJson(doc, buffer);

  mqClient.publish(dbg_topic, buffer, n);
}


// called out of timed_loop async
void checkDebug(){
  if (msgCount>0){
    
    String message = arrMessages[0];

     for (int i = 0; i < numMsg-1; i++) {
      arrMessages[i]=arrMessages[i+1];
    }
    arrMessages[numMsg-1]="";
    msgCount--;
    sendDbg(message);
  }
  
  
}

// stuff the line into an array. Another function will send it to mq later
void debug(String dbgMsg, boolean withSerial){
  Serial << "dbgMsg: " << dbgMsg <<  "\n";
  
  if (withSerial) {
    Serial.println( dbgMsg );
  }
  if (msgCount<numMsg){
    arrMessages[msgCount]=dbgMsg;
    msgCount++;
  }
  
}


///////////////////////////////////////////////////////////////////////////


void setupWifi(){
  // Connect to WiFi

  // make sure there is no default AP set up unintended
  WiFi.mode(WIFI_STA);
  //WiFi.hostname(HOSTNAME);
  
  WiFi.begin(ssid, password);
 
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
  }
  msg = "WiFi connected, local IP: "+WiFi.localIP().toString();
  debug(msg,true);
  
}

void setupMq(){
  // pubsub setup
  mqClient.setServer(mqtt_server, mqtt_port);
  mqClient.setCallback(callback);
  mqConnect();  
}


void setupOta(){

  //OTA SETUP
  ArduinoOTA.setPort(OTAport);
  // Hostname defaults to esp8266-[ChipID]
  ArduinoOTA.setHostname(SENSORNAME);

  // No authentication by default
  ArduinoOTA.setPassword((const char *)OTApassword);

  ArduinoOTA.onStart([]() {
    debug("Starting OTA",false);
  });
  ArduinoOTA.onEnd([]() {
    debug("End OTA",false);
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    //Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    if (error == OTA_AUTH_ERROR) debug("OTA Auth Failed",false);
    else if (error == OTA_BEGIN_ERROR) debug("OTA Begin Failed",false);
    else if (error == OTA_CONNECT_ERROR) debug("OTA Connect Failed",false);
    else if (error == OTA_RECEIVE_ERROR) debug("OTA Receive Failed",false);
    else if (error == OTA_END_ERROR) debug("OTA End Failed",false);
  });
  ArduinoOTA.begin();
  
}



/********************************** START mosquitto *****************************************/

void mqConnect() {
  // Loop until we're reconnected
  while (!mqClient.connected()) {

    // Attempt to connect
    if (mqClient.connect(SENSORNAME, mqtt_username, mqtt_password)) {
      
      mqClient.subscribe(set_topic);      
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqClient.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }

}

////////////////////////////////////


void postRestItem(String itemName,String msg){
   WiFiClient client;
  
  if (!client.connect(host, httpPort)) {
    Serial <<  F("connection for post on ") << host << ":" << httpPort << " about " << itemName << " -> "<< msg << F(" failed \n");
    commError=true;
    commErrorMsg="post "+itemName+" failed";
    return;
  } else {
    Serial <<  F("connection for post on ") << host << ":" << httpPort << " about " << itemName << " -> "<< msg << F(" worked \n");
    commError=false;
    commErrorMsg="ok";
  }
  
  
  String pubStringLength = String(msg.length(), DEC);
  
  // We now create a URI for the request
  
  String req="POST /rest/items/"+itemName+" HTTP/1.1";
  String hst="Host: "+String(host);
  
  Serial << F("Requesting POST: ") << itemName << " " << msg << "\n";
  
  // Send request to the server:
  client.println(req);
  client.println(hst);
  client.println("Content-Type: text/plain");
  client.println("Accept: application/json");
  client.println("Connection: close");
  client.print("Content-Length: ");
  client.println(pubStringLength);
  client.println();
  client.print(msg);
  client.println();
  delay(20);
  
  // Read all the lines of the reply from server and print them to Serial
  while (client.available()) {
    String line = client.readStringUntil('\r');
    Serial <<  line << "\n";
  }
}

String getRestItemState(String itemName){
  HTTPClient http;
  String dUrl=restUrl+itemName+"/state";
  http.begin(dUrl);
  String result="fail";
  int httpCode = http.GET();
  
    if(httpCode > 0) {
        // HTTP header has been sent and Server response header has been handled
        //Serial.printf("[HTTP] GET... code: %d\n", httpCode);
  
        // file found at server
        if(httpCode == HTTP_CODE_OK) {
            result = http.getString();
            commError=false;
            commErrorMsg="ok";
           
        }
    } else {
      // got an error
        Serial << F("[HTTP] GET... failed, error: %s\n") << http.errorToString(httpCode).c_str() << "\n" << dUrl <<"\n";
        commError=true;
        commErrorMsg="get "+itemName+" failed";
    }
    http.end();
  return result;
}

/////////////////////////////////////////////////

void showState(String haveValid, String haveEntry) {
  
  
  display.clearBuffer();         // clear the internal memory
  display.setFont( u8g2_font_profont17_tf);  // choose a suitable font

  
  strBuffer.start() << F("Connect:   ") << haveValid;
  display.drawStr (0, 18, strBuffer);
  
  strBuffer.start() << F("Entry: ") << haveEntry ;
  display.drawUTF8 (0, 47, strBuffer);
 

  display.sendBuffer();
}

void showLogoPage(void){

  #define logo_width 82
  #define logo_height 42
  static unsigned char logo_bits[] = {
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0xa0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xa0,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x28, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x15, 0x28, 0x80, 0x02, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x50, 0x20, 0x04, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90,
   0x4a, 0x14, 0x00, 0x01, 0x50, 0x00, 0x00, 0x00, 0x00, 0x00, 0x4a, 0x10,
   0x08, 0x80, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08, 0x40, 0x08,
   0x80, 0x02, 0x20, 0x00, 0x00, 0x00, 0x00, 0x80, 0x02, 0x00, 0x02, 0x40,
   0x00, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02, 0x00, 0x0a, 0x40, 0x01,
   0x28, 0x00, 0x00, 0x00, 0x00, 0x40, 0x01, 0x00, 0x04, 0x80, 0x00, 0x10,
   0x00, 0x00, 0x00, 0x00, 0x20, 0x00, 0x00, 0x04, 0x80, 0x00, 0x10, 0x00,
   0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x01, 0x20, 0x01, 0x04, 0x00, 0x00,
   0x00, 0x00, 0x40, 0x00, 0x00, 0x05, 0x40, 0x00, 0x14, 0x00, 0x00, 0x00,
   0x00, 0x10, 0x00, 0x00, 0x02, 0x90, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00,
   0x50, 0x00, 0x00, 0x02, 0x20, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00, 0x20,
   0x00, 0x80, 0x00, 0x10, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x20, 0x00,
   0x80, 0x02, 0x50, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x10, 0x00, 0x20,
   0x01, 0x20, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00, 0x50, 0x00, 0x20, 0x01,
   0x24, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x40, 0x00, 0x48, 0x02, 0x48,
   0x80, 0x00, 0x00, 0x00, 0x00, 0x00, 0x90, 0x02, 0x90, 0x04, 0x92, 0x80,
   0x02, 0x00, 0x00, 0x00, 0x00, 0x40, 0x54, 0x05, 0xa9, 0x24, 0x55, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x40, 0x41, 0x04, 0x22, 0x41, 0x44, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x2a, 0x01, 0x95, 0x80, 0x2a, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

  display.clearBuffer();
  display.drawXBM( 23, 11, logo_width, logo_height, logo_bits);
  display.sendBuffer();
}
