/*  LoRaWAN   Empfänger MQTT GPS-Daten LOG TFT 2,8 Zoll
    Version 9 Logging funktioniert / WEB download hinzugefügt / Gateway im Logging / mit Karte / File mit Daum und sortiert
    04.05.2026

    user_setup.h : 
    #define ILI9341_DRIVER

// SPI Pins (ESP32 Standard – kannst du so lassen)
#define TFT_MOSI 23
#define TFT_MISO 19
#define TFT_SCLK 18

#define TFT_CS   5
#define TFT_DC   16
#define TFT_RST  17   // oder -1 wenn nicht angeschlossen

// Backlight (falls vorhanden)
#define TFT_BL   4
#define TFT_BACKLIGHT_ON HIGH

// Frequenz
#define SPI_FREQUENCY 40000000

// Fonts
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define LOAD_GFXFF
#define SMOOTH_FONT


*/
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <WiFiClientSecure.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <SD.h>
#include <WebServer.h>

WebServer server(80);

const char* ap_ssid = "GPS-LoRaWAN";
const char* ap_pass = "12345678";


#define SD_CS 27
bool sdOK = false;

File gpxFile;
bool loggingActiveSD = false;

TFT_eSPI tft = TFT_eSPI();
WiFiClientSecure espClient;
PubSubClient client(espClient);

// ================= WLAN =================
const char* ssid = "FRITZ!Box 7590 FI";
const char* password = "57426211004645725795";

// ================= TTN =================
const char* mqtt_server = "eu1.cloud.thethings.network";
const int mqtt_port = 8883;

const char* mqtt_user = "015@ttn";

const char* mqtt_pass = "NNSXS.RVY3UODNE4SIH2WZZQTOH2ZSTXM7MATZQA66LXY.4TYHI7BMG3VNVYA2MDBA4A2KCQS5FPXDTXNT6ABQJEM25GHFU4HA";

const char* topic = "v3/015@ttn/devices/+/up";

// ================= LOG =================
#define MAX_LOG 10

String lastTimestamp = "";

struct GPSLog {
  String time;
  float lat;
  float lon;
  bool logging;
  String gw;
};

GPSLog logs[MAX_LOG];

// ================= CURRENT DATA =================
float lat = 0;
float lon = 0;
bool loggingState = false;
String gatewayID = "---";
int rssi = 0;
float snr = 0;

// ================= FLAG =================
volatile bool newData = false;

// ================= INIT LOGS =================
void initLogs() {
  for (int i = 0; i < MAX_LOG; i++) {
    logs[i].time = "---";
    logs[i].lat = 0;
    logs[i].lon = 0;
    logs[i].logging = false;
  }
}

// ================= MQTT CALLBACK =================
void callback(char* topic, byte* payload, unsigned int length) {

  

  String msg;
  for (int i = 0; i < length; i++) msg += (char)payload[i];

  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, msg)) return;

  String t = doc["received_at"].as<String>();
  lastTimestamp = t;

  if (!doc["uplink_message"]["decoded_payload"]) return;

  JsonArray meta = doc["uplink_message"]["rx_metadata"];

  if (!meta.isNull() && meta.size() > 0) {

    int bestIndex = 0;
    int bestRSSI = -999;

    for (int i = 0; i < meta.size(); i++) {

      int r = meta[i]["rssi"] | -999;

      if (r > bestRSSI) {
        bestRSSI = r;
        bestIndex = i;
      }
    }

    gatewayID = meta[bestIndex]["gateway_ids"]["gateway_id"].as<String>();
    rssi = meta[bestIndex]["rssi"] | 0;
    snr  = meta[bestIndex]["snr"]  | 0.0;
  }

  String device = doc["end_device_ids"]["device_id"];
  if (device != "15-03") return;

  JsonObject p = doc["uplink_message"]["decoded_payload"];

  lat = p["latitude"] | 0.0;
  lon = p["longitude"] | 0.0;

  //loggingState = p["logging"] | false;

  if (p["logging"].is<bool>()) {
    loggingState = p["logging"].as<bool>();
  }
  else if (p["logging"].is<int>()) {
    loggingState = p["logging"].as<int>() == 1;
  }
  else if (p["logging"].is<const char*>()) {
    String v = p["logging"].as<String>();
    v.toLowerCase();
    loggingState = (v == "true" || v == "1" || v == "on");
  }
  else {
    loggingState = false;
  }

  t = doc["received_at"].as<String>();
  String shortTime = (t.length() >= 19) ? t.substring(11, 19) : "---";

  // ================= SHIFT BUFFER =================
  for (int i = MAX_LOG - 1; i > 0; i--) {
    logs[i] = logs[i - 1];
  }

  logs[0] = { shortTime, lat, lon, loggingState, gatewayID };

  newData = true;

  // ===== SD LOGGING =====
  if (sdOK) {

    if (loggingState && !loggingActiveSD) {
      startGPX();
    }

    if (!loggingState && loggingActiveSD) {
      stopGPX();
    }

    if (loggingState && loggingActiveSD) {
      writeGPX(lat, lon, t, gatewayID, rssi, snr);
    }
  }

}

// ================= DISPLAY =================
void updateDisplay() {

  tft.fillScreen(TFT_BLACK);

  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE);
  tft.setCursor(10, 5);
  tft.println("GPS LOG");

  tft.drawLine(0, 25, 320, 25, TFT_DARKGREY);

  tft.setTextSize(1);
  tft.setTextColor(TFT_YELLOW);

  tft.setCursor(0, 30);   tft.print("Time");
  tft.setCursor(80, 30);  tft.print("Lat");
  tft.setCursor(170, 30); tft.print("Lon");
  tft.setCursor(260, 30); tft.print("S");
  tft.setCursor(290, 30); tft.print("GW");

  for (int i = 0; i < MAX_LOG; i++) {

    int y = 45 + i * 20;

    tft.setTextColor(TFT_WHITE);

    tft.setCursor(0, y);
    tft.print(logs[i].time);

    tft.setCursor(80, y);
    tft.print(logs[i].lat, 5);

    tft.setCursor(170, y);
    tft.print(logs[i].lon, 5);

    tft.setCursor(260, y);

    if (logs[i].logging) {
      tft.setTextColor(TFT_GREEN);
      tft.print("ON");
    } else {
      tft.setTextColor(TFT_RED);
      tft.print("OFF");
    }

    tft.setCursor(290, y);
    tft.setTextColor(TFT_CYAN);
    tft.print(logs[i].gw.substring(0,5));  // gekürzt fürs Display

  }
}

// ================= MQTT RECONNECT =================
void reconnect() {

  while (!client.connected()) {

    Serial.println("MQTT reconnect...");

    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), mqtt_user, mqtt_pass)) {
      Serial.println("MQTT OK");
      client.subscribe(topic);
    } else {
      Serial.print("MQTT Error: ");
      Serial.println(client.state());
      delay(3000);
    }
  }
}

void startGPX() {

  //String filename = "/track_" + String(millis()) + ".gpx";
  String filename = generateFilenameFromISO(lastTimestamp);
  gpxFile = SD.open(filename, FILE_WRITE);

  if (!gpxFile) {
    Serial.println("❌ GPX open failed");
    return;
  }

  Serial.println("📁 GPX START");

  gpxFile.println("<?xml version=\"1.0\" encoding=\"UTF-8\"?>");
  gpxFile.println("<gpx version=\"1.1\" creator=\"ESP32-GPS-Logger\" "
                "xmlns=\"http://www.topografix.com/GPX/1/1\" "
                "xmlns:xsi=\"http://www.w3.org/2001/XMLSchema-instance\" "
                "xsi:schemaLocation=\"http://www.topografix.com/GPX/1/1 "
                "http://www.topografix.com/GPX/1/1/gpx.xsd\">");
  gpxFile.println("<trk><name>Track</name><trkseg>");
  gpxFile.flush();

  loggingActiveSD = true;
}

void writeGPX(float lat, float lon, String timeStr, String gw, int rssi, float snr) {

  if (!loggingActiveSD || !gpxFile) return;

  gpxFile.print("<trkpt lat=\"");
  gpxFile.print(lat, 6);
  gpxFile.print("\" lon=\"");
  gpxFile.print(lon, 6);
  gpxFile.print("\"><time>");

  String cleanTime = timeStr;
  if (cleanTime.length() >= 19) {
    cleanTime = cleanTime.substring(0, 19) + "Z";
  }

  gpxFile.print(cleanTime);
  gpxFile.println("</time>");

  // 👉 NEU: Gateway Infos
  gpxFile.println("<extensions>");
  
  gpxFile.print("<gw>");
  gpxFile.print(gw);
  gpxFile.println("</gw>");

  gpxFile.print("<rssi>");
  gpxFile.print(rssi);
  gpxFile.println("</rssi>");

  gpxFile.print("<snr>");
  gpxFile.print(snr, 1);
  gpxFile.println("</snr>");

  gpxFile.println("</extensions>");

  gpxFile.println("</trkpt>");

  gpxFile.flush();
}


void stopGPX() {

  if (!gpxFile) return;

  gpxFile.println("</trkseg></trk>");
  gpxFile.println("</gpx>");
  gpxFile.close();

  Serial.println("💾 GPX CLOSED");

  loggingActiveSD = false;
}

void handleRoot() {

  String files[50];   // max 50 Dateien
  int count = 0;

  File root = SD.open("/");
  File file = root.openNextFile();

  while (file && count < 50) {
    files[count++] = String(file.name());
    file = root.openNextFile();
  }

  // 👉 SORTIEREN (alphabetisch)
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {
      if (files[i] > files[j]) {
        String tmp = files[i];
        files[i] = files[j];
        files[j] = tmp;
      }
    }
  }

  // 👉 HTML bauen (NEUESTE OBEN = rückwärts)
  String html = "<h1>GPX Dateien</h1>";

  for (int i = count - 1; i >= 0; i--) {

    String name = files[i];

    html += name;
    html += " <a href=\"/download?f=" + name + "\">[Download]</a>";
    html += " <a href=\"/delete?f=" + name + "\">[Delete]</a><br>";
  }

  server.send(200, "text/html", html);
}

void handleDownload() {

  if (!server.hasArg("f")) {
    server.send(400, "text/plain", "No file");
    return;
  }

  String filename = server.arg("f");

  if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  File file = SD.open(filename);

  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  // 👉 Dateiname extrahieren (ohne Slash)
  String nameOnly = filename.substring(1);

  // 👉 WICHTIG: Download-Header setzen
  server.sendHeader("Content-Type", "application/gpx+xml");
  server.sendHeader("Content-Disposition", "attachment; filename=" + nameOnly);
  server.sendHeader("Connection", "close");

  server.streamFile(file, "application/gpx+xml");

  file.close();
}

void handleDelete() {

  if (!server.hasArg("f")) {
    server.send(400, "text/plain", "No file");
    return;
  }

  String filename = server.arg("f");
    if (!filename.startsWith("/")) {
    filename = "/" + filename;
  }

  if (SD.remove(filename)) {
    server.send(200, "text/plain", "Deleted");
  } else {
    server.send(500, "text/plain", "Delete failed");
  }
}

void handleGPXData() {

  if (!server.hasArg("f")) {
    server.send(400, "text/plain", "No file");
    return;
  }

  String filename = server.arg("f");
  if (!filename.startsWith("/")) filename = "/" + filename;

  File file = SD.open(filename);
  if (!file) {
    server.send(404, "text/plain", "File not found");
    return;
  }

  String json = "[";
  bool first = true;

  while (file.available()) {

    String line = file.readStringUntil('\n');

    if (line.indexOf("<trkpt") >= 0) {

      float lat = 0, lon = 0;
      String time = "", gw = "";
      int rssi = 0;
      float snr = 0;

      // lat
      int p1 = line.indexOf("lat=\"");
      if (p1 >= 0) {
        lat = line.substring(p1+5).toFloat();
      }

      // lon
      int p2 = line.indexOf("lon=\"");
      if (p2 >= 0) {
        lon = line.substring(p2+5).toFloat();
      }

      // nächste Zeilen lesen
      while (file.available()) {

        String l = file.readStringUntil('\n');

        if (l.indexOf("<time>") >= 0) {
          int a = l.indexOf(">")+1;
          int b = l.indexOf("</");
          time = l.substring(a,b);
        }

        if (l.indexOf("<gw>") >= 0) {
          int a = l.indexOf(">")+1;
          int b = l.indexOf("</");
          gw = l.substring(a,b);
        }

        if (l.indexOf("<rssi>") >= 0) {
          rssi = l.substring(l.indexOf(">")+1).toInt();
        }

        if (l.indexOf("<snr>") >= 0) {
          snr = l.substring(l.indexOf(">")+1).toFloat();
        }

        if (l.indexOf("</trkpt>") >= 0) break;
      }

      if (!first) json += ",";
      first = false;

      json += "{";
      json += "\"lat\":" + String(lat,6) + ",";
      json += "\"lon\":" + String(lon,6) + ",";
      json += "\"time\":\"" + time + "\",";
      json += "\"gw\":\"" + gw + "\",";
      json += "\"rssi\":" + String(rssi) + ",";
      json += "\"snr\":" + String(snr,1);
      json += "}";
    }
  }

  json += "]";
  file.close();

  server.send(200, "application/json", json);
}

void handleFileList() {

  String files[50];
  int count = 0;

  File root = SD.open("/");
  File file = root.openNextFile();

  while (file && count < 50) {

    String name = String(file.name());

    if (name.endsWith(".gpx")) {
      files[count++] = name;
    }

    file = root.openNextFile();
  }

  // 👉 SORTIEREN nach Nummer (millis) → neueste zuerst
  for (int i = 0; i < count - 1; i++) {
    for (int j = i + 1; j < count; j++) {

      long n1 = files[i].substring(files[i].indexOf("_") + 1).toInt();
      long n2 = files[j].substring(files[j].indexOf("_") + 1).toInt();

      if (n2 > n1) {
        String tmp = files[i];
        files[i] = files[j];
        files[j] = tmp;
      }
    }
  }

  // 👉 JSON bauen
  String json = "[";

  for (int i = 0; i < count; i++) {

    if (i > 0) json += ",";

    String clean = files[i];
    if (clean.startsWith("/")) clean = clean.substring(1);

    json += "\"" + clean + "\"";
  }

  json += "]";

  server.send(200, "application/json", json);
}

String generateFilenameFromISO(String iso) {

  if (iso.length() < 19) {
    return "/track_unknown_" + String(millis()) + ".gpx";
  }

  // Beispiel: 2026-04-28T18:35:12
  String date = iso.substring(0, 10);   // 2026-04-28
  String time = iso.substring(11, 19);  // 18:35:12

  date.replace("-", "");
  time.replace(":", "");

  return "/track_" + date + "_" + time + ".gpx";
}


// ================= SETUP =================
void setup() {

  Serial.begin(115200);

  initLogs();

  WiFi.mode(WIFI_AP_STA);

  // --- WLAN verbinden ---
  WiFi.begin(ssid, password);

  Serial.print("Verbinde WLAN");

  while (WiFi.status() != WL_CONNECTED) {
    delay(300);
    Serial.print(".");
  }

  Serial.println("\nWiFi OK");
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  // --- Hotspot starten ---
  WiFi.softAP(ap_ssid, ap_pass);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());



  server.on("/", handleRoot);
  server.on("/files", handleFileList);
  server.on("/download", handleDownload);
  server.on("/delete", handleDelete);
  server.on("/map", handleMap);
  server.on("/gpxdata", handleGPXData);
  server.begin();

  // --- SD INIT ---
  SPI.begin(18, 19, 23, SD_CS);

  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);

  if (SD.begin(SD_CS)) {
    Serial.println("✅ SD OK");
    sdOK = true;
  } else {
    Serial.println("❌ SD FAIL");
  }

  // --- SD Test ---
  if (sdOK) {
    File f = SD.open("/test.txt", FILE_WRITE);
    if (f) {
      f.println("SD TEST OK");
      f.close();
      Serial.println("Write OK");
    } else {
      Serial.println("Write FAIL");
    }
  }
  // --- 

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);

  espClient.setInsecure();

  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(callback);
  client.setBufferSize(2048);
}

// ================= LOOP =================
void loop() {

  server.handleClient();

  if (!client.connected()) reconnect();
  client.loop();

  if (newData) {
    newData = false;
    updateDisplay();
  }
}

void handleMap() {

  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset='utf-8'/>
<title>GPS Map</title>

<link rel='stylesheet' href='https://unpkg.com/leaflet@1.9.4/dist/leaflet.css'/>

<style>
#map { height: 100vh; }
</style>
</head>

<body>

<select id='file'></select>
<button onclick='loadGPX()'>Load</button>

<div id='map'></div>

<script src='https://unpkg.com/leaflet@1.9.4/dist/leaflet.js'></script>

<script>
let map = L.map('map').setView([51,11], 13);

L.tileLayer('https://tile.openstreetmap.org/{z}/{x}/{y}.png', {
  maxZoom: 19
}).addTo(map);

let layer;

fetch('/files')
.then(r => r.json())
.then(files => {
  let sel = document.getElementById('file');

  files.forEach(f => {
    let o = document.createElement('option');
    o.value = f;
    o.text = f;
    sel.add(o);
  });
});

function loadGPX(){

  let f = document.getElementById('file').value;

  fetch('/gpxdata?f=' + f)
  .then(r => r.json())
  .then(data => {

    if(layer) map.removeLayer(layer);

    let points = [];

    layer = L.layerGroup().addTo(map);

    data.forEach(p => {

      let marker = L.circleMarker([p.lat, p.lon]).addTo(layer);

      marker.bindPopup(
        'GW: ' + p.gw + '<br>' +
        'RSSI: ' + p.rssi + '<br>' +
        'SNR: ' + p.snr + '<br>' +
        p.time
      );

      points.push([p.lat, p.lon]);
    });

    let poly = L.polyline(points).addTo(layer);
    map.fitBounds(poly.getBounds());
  });
}
</script>

</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}