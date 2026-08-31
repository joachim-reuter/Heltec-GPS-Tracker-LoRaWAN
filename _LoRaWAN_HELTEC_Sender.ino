/* Version 26.06.2026  FINAL
Verbessertes Logging
Intervall speichern mit LittleFS und HotSpot


Payload formatters :
function decodeUplink(input) {

  var bytes = input.bytes;

  var lat = (bytes[0]<<24 | bytes[1]<<16 | bytes[2]<<8 | bytes[3]) / 100000;
  var lon = (bytes[4]<<24 | bytes[5]<<16 | bytes[6]<<8 | bytes[7]) / 100000;

  var logging = bytes[8] === 1;

  return {
    data: {
      latitude: lat,
      longitude: lon,
      logging: logging
    }
  };
}


*/


#include "LoRaWan_APP.h"
#include "Wire.h"
#include "HT_st7735.h"
#include "HT_TinyGPS++.h"
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>


WebServer server(80);
const char* ap_ssid = "ESP32-LoRa-Config";
const char* ap_pass = "12345678";

String intervar = "";

TinyGPSPlus GPS;
HT_st7735 st7735;

#define VGNSS_CTRL 3
#define Vext 36

bool gpsReady = false;


// ---- Button für Aufzeichnung ---------------

#define BUTTON_PIN 0

bool loggingActive = false;
bool lastButtonState = HIGH;




// === REQUIRED GLOBALS (Heltec Fix) ===

// OTAA (du nutzt OTAA → diese bleiben leer)
uint32_t devAddr = 0;
uint8_t nwkSKey[16] = {0};
uint8_t appSKey[16] = {0};


uint8_t devEui[] = { 0x70, 0xB3, 0xD5, 0x7E, 0xD0, 0x07, 0x70, 0x46 };  // 46 70 07 D0 7E D5 B3 70 
uint8_t appEui[] = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
uint8_t appKey[] = { 0xXX, 0xXX, 0xXX, 0x00, 0xXX, 0x85, 0x9A, 0xA5, 0x95, 0xFE, 0x9c, 0xD4, 0xDD, 0x72, 0x0E, 0xXX };


uint16_t userChannelsMask[6]={ 0x00FF,0x0000,0x0000,0x0000,0x0000,0x0000 };

LoRaMacRegion_t loraWanRegion = ACTIVE_REGION;

DeviceClass_t  loraWanClass = CLASS_A;

uint32_t appTxDutyCycle = 10000;  // 300000 5 min
bool overTheAirActivation = true;//OTAA security is better

bool loraWanAdr = true;

bool isTxConfirmed = false;

uint8_t appPort = 2;

uint8_t confirmedNbTrials = 4;

static void prepareTxFrame(uint8_t port)
{
  if (GPS.location.isValid()) {

    float lat = GPS.location.lat();
    float lon = GPS.location.lng();

    int32_t lat_int = lat * 100000;
    int32_t lon_int = lon * 100000;

    appDataSize = 9;

    appData[0] = (lat_int >> 24) & 0xFF;
    appData[1] = (lat_int >> 16) & 0xFF;
    appData[2] = (lat_int >> 8) & 0xFF;
    appData[3] = lat_int & 0xFF;

    appData[4] = (lon_int >> 24) & 0xFF;
    appData[5] = (lon_int >> 16) & 0xFF;
    appData[6] = (lon_int >> 8) & 0xFF;
    appData[7] = lon_int & 0xFF;

    Serial.println("GPS gesendet");

  } else {
    Serial.println("Kein GPS Fix");
    // appDataSize = 9;
    // for (int i = 0; i < 8; i++) appData[i] = 0;  // Prüfen ob notwendig
  }
  appData[8] = loggingActive ? 1 : 0;

}
//###########################################################################################
// Teil LittleFS
//###########################################################################################

// -------------------  Filesystem initialisieren --------------------------------------
void initFS()
{
  if (!LittleFS.begin(true))
  {
    Serial.println("LittleFS Mount fehlgeschlagen");
    return;
  }
  Serial.println("LittleFS OK");
}

// -------------------- LittleFS Loader Intervall -------------------------------------
bool loadIntervall()
{
  fs::File f = LittleFS.open("/intervall.txt", "r");

  if (!f)
  {
    Serial.println("intervall.txt fehlt");
    return false;
  }

  while (f.available())
  {
    String line = f.readStringUntil('\n');
    line.trim();

    if (line.startsWith("intervall="))
    {
      intervar = line.substring(10);
    }

  }

  f.close();

  Serial.print("Intervall= ");
  Serial.println(intervar);

  return true;
}

// ------------- Wert auf LittleFS speichern ------------
bool saveIntervall(String value)
{
  fs::File f = LittleFS.open("/intervall.txt", "w");
  if (!f)
  {
    Serial.println("Fehler beim Schreiben");
    return false;
  }

  f.println("intervall=" + value);
  f.close();

  Serial.println("Intervall gespeichert: " + value);
  return true;
}

// --- Homepage -------------------------------
String htmlPage()
{
  String page =
  "<!DOCTYPE html><html><head><meta charset='utf-8'>"
  "<title>ESP32 Intervall</title></head><body>"
  "<h2>Intervall einstellen</h2>"
  "<form action='/save' method='POST'>"
  "<input name='intervall' type='text' value='" + intervar + "'>"
  "<input type='submit' value='Speichern'>"
  "</form>"
  "</body></html>";

  return page;
}

// --------- WEB Aufruf --------------------------
void handleRoot()
{
  server.send(200, "text/html", htmlPage());
}

// ----------- WEB Wert Speichern ----------------
void handleSave()
{
  if (server.hasArg("intervall"))
  {
    intervar = server.arg("intervall");

    saveIntervall(intervar);
  }

  server.sendHeader("Location", "/");
  server.send(303);
  Serial.println(intervar);
  
}

// ----------------  HOT- Spot initialisieren ---------------------
void initWiFiAP()
{
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.println("AP gestartet");
  Serial.print("IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", handleRoot);
  server.on("/save", HTTP_POST, handleSave);

  server.begin();
}




//###########################################################################################

void setup() {
  Serial.begin(115200);
 
  initFS();

  if(!loadIntervall())
  {
    intervar = "0";
  }

  appTxDutyCycle = static_cast<uint32_t>(intervar.toInt());
  Serial.print("appTxDutyCycle = ");
  Serial.println(appTxDutyCycle);


  initWiFiAP();


  // LoRa Init
  Mcu.begin(HELTEC_BOARD,SLOW_CLK_TPYE);

  pinMode(Vext, OUTPUT);
  digitalWrite(Vext, LOW);

  // Display
  st7735.st7735_init();


  // Button
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  // GPS einschalten
  pinMode(VGNSS_CTRL, OUTPUT);
  digitalWrite(VGNSS_CTRL, HIGH);

  Serial1.begin(115200, SERIAL_8N1, 33, 34);
  delay(2000); // GPS stabilisieren
  Serial.println("System gestartet");

  deviceState = DEVICE_STATE_INIT;
}


void loop()
{

  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Schalter direkt auswerten
  loggingActive = (currentButtonState == LOW);

  // optional Debug
  static bool oldState = false;

  if (oldState != loggingActive) {
    Serial.println(loggingActive ? "LOGGING START" : "LOGGING STOP");
    oldState = loggingActive;
  }

  while (Serial1.available()) {
   GPS.encode(Serial1.read());
  } 

  if (GPS.location.isValid()) {
    gpsReady = true;
  }

  static unsigned long lastDisplay = 0;

  if (millis() - lastDisplay > 3000) {
    lastDisplay = millis();

    if (GPS.location.isValid()) {
      st7735.st7735_fill_screen(ST7735_BLACK);
      st7735.st7735_write_str(0, 20, "LAT: " + String(GPS.location.lat(), 6));
      st7735.st7735_write_str(0, 40, "LON: " + String(GPS.location.lng(), 6));
    } else {
      st7735.st7735_fill_screen(ST7735_BLACK);
      st7735.st7735_write_str(0, 20, "Kein GPS Fix");
    }
  }


 switch( deviceState )
  {
    case DEVICE_STATE_INIT:
    {
      #if(LORAWAN_DEVEUI_AUTO)
        LoRaWAN.generateDeveuiByChipID();
      #endif
      LoRaWAN.init(loraWanClass,loraWanRegion);
      break;
    }

    case DEVICE_STATE_JOIN:
    {
      Serial.println("Versuche Join...");  
      LoRaWAN.join();
      break;
    }
    
    case DEVICE_STATE_SEND:
    {
    
      static unsigned long gpsStart = millis();
      if (!gpsReady) {
         if (millis() - gpsStart < 120000) {
          Serial.println("Warte auf GPS Fix...");
          Serial.println(intervar);
          deviceState = DEVICE_STATE_SEND;
          delay(1000);
        } else {
          Serial.println("GPS Timeout → sende trotzdem");
          prepareTxFrame(appPort);
          LoRaWAN.send();
          deviceState = DEVICE_STATE_CYCLE;
        }
        break;
      }

      prepareTxFrame(appPort);
      LoRaWAN.send();
      deviceState = DEVICE_STATE_CYCLE;
      break;
      }   

    case DEVICE_STATE_CYCLE:
      {
        // Schedule next packet transmission
        txDutyCycleTime = appTxDutyCycle + randr( -APP_TX_DUTYCYCLE_RND, APP_TX_DUTYCYCLE_RND );
        LoRaWAN.cycle(txDutyCycleTime);
        deviceState = DEVICE_STATE_SLEEP;
        break;
      }
    case DEVICE_STATE_SLEEP:
      {
        LoRaWAN.sleep(loraWanClass);
        break;
      }
      default:
      {
        deviceState = DEVICE_STATE_INIT;
        break;
      }
    }
    server.handleClient();
  }
