# GPS-Tracker mit ESP32, GPS und LoRaWAN

Ein kompakter GPS-Tracker auf Basis eines **Heltec Wireless Tracker** mit integriertem GPS-Modul und LoRaWAN-Anbindung.

Der Tracker ermittelt regelmäßig seine aktuelle GPS-Position und überträgt Latitude und Longitude über LoRaWAN an einen LoRaWAN-Netzwerkserver, beispielsweise **The Things Network (TTN)**.

Zusätzlich verfügt das Gerät über ein kleines Display, einen Taster zur Aktivierung der Aufzeichnung sowie einen integrierten WLAN-Hotspot zur Konfiguration des Sendeintervalls.

---

## Projektübersicht

Der GPS-Tracker kombiniert folgende Funktionen:

* ESP32-Mikrocontroller
* integriertes GPS-Modul
* LoRaWAN-Funkübertragung
* Anzeige der GPS-Koordinaten auf dem integrierten Display
* einstellbares Übertragungsintervall
* Speicherung des Intervalls im LittleFS
* WLAN-Hotspot zur Konfiguration
* Taster zur Aktivierung der Aufzeichnung
* Übertragung des Aufzeichnungsstatus über LoRaWAN
* OTAA-Anmeldung bei LoRaWAN
* automatischer Failsafe bei fehlendem GPS-Fix bzw. GPS-Startverzögerung

---

## Hardware

### Verwendete Hardware

* **Heltec Wireless Tracker**
* integriertes ESP32
* integriertes LoRa-Modul
* integriertes GPS-Modul
* integriertes ST7735 TFT-Display
* Taster an GPIO 0

### GPS

Das GPS-Modul wird über `Serial1` angesprochen.

```text
GPS RX/TX:
GPIO 33 / GPIO 34
Baudrate: 115200
```

Das GPS wird über `VGNSS_CTRL` aktiviert.

```cpp
#define VGNSS_CTRL 3
```

---

## Display

Das integrierte ST7735-Display wird zur Anzeige des aktuellen GPS-Status verwendet.

Bei vorhandenem GPS-Fix werden Latitude und Longitude angezeigt:

```text
LAT: 51.xxxxxx
LON: 11.xxxxxx
```

Ist noch kein GPS-Fix vorhanden:

```text
Kein GPS Fix
```

Die Anzeige wird alle 3 Sekunden aktualisiert.

---

## LoRaWAN

Die Kommunikation erfolgt über LoRaWAN.

Verwendet wird:

* LoRaWAN Class A
* OTAA
* Region entsprechend der Einstellung `ACTIVE_REGION`
* Uplink Port **2**
* ADR aktiviert

Im Sketch ist vorgesehen:

```cpp
DeviceClass_t loraWanClass = CLASS_A;

bool overTheAirActivation = true;

bool loraWanAdr = true;

uint8_t appPort = 2;
```

Das Übertragungsintervall wird über `appTxDutyCycle` festgelegt.

---

## Datenübertragung

Die GPS-Koordinaten werden für die Übertragung auf Ganzzahlen skaliert.

Latitude und Longitude werden mit dem Faktor **100000** multipliziert.

Damit können die Koordinaten mit hoher Genauigkeit in 4 Byte übertragen werden.

Das Datenformat umfasst insgesamt 9 Byte:

| Byte | Inhalt         |
| ---- | -------------- |
| 0–3  | Latitude       |
| 4–7  | Longitude      |
| 8    | Logging-Status |

### Payload

```text
Byte 0-3   Latitude
Byte 4-7   Longitude
Byte 8     Logging
```

Der Logging-Status hat folgende Bedeutung:

```text
0 = Logging AUS
1 = Logging EIN
```

---

## TTN Payload Formatter

Für **The Things Network (TTN)** kann folgender Payload Formatter verwendet werden:

```javascript
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
```

Das Ergebnis steht anschließend beispielsweise als JSON zur Verfügung:

```json
{
  "latitude": 51.123456,
  "longitude": 11.654321,
  "logging": true
}
```

---

## Logging

Der Taster an GPIO 0 steuert den Aufzeichnungsstatus.

```cpp
#define BUTTON_PIN 0
```

Der Taster arbeitet mit `INPUT_PULLUP`.

```text
Taster gedrückt  → LOGGING START
Taster loslassen → LOGGING STOP
```

Der aktuelle Status wird als zusätzliches Byte im LoRaWAN-Payload übertragen.

---

## Einstellbares Sendeintervall

Das Übertragungsintervall kann über einen integrierten WLAN-Hotspot eingestellt werden.

Der ESP32 stellt dazu ein eigenes WLAN bereit:

```text
SSID: ESP32-LoRa-Config
Passwort: 12345678
```

Nach dem Verbinden kann über die IP-Adresse des ESP32 eine kleine Konfigurationsseite geöffnet werden.

Dort kann das Intervall eingegeben und gespeichert werden.

---

## LittleFS

Das konfigurierte Intervall wird dauerhaft im internen Flash-Speicher des ESP32 gespeichert.

Verwendet wird **LittleFS**.

Die Datei:

```text
/intervall.txt
```

enthält beispielsweise:

```text
intervall=10000
```

Nach einem Neustart wird der gespeicherte Wert automatisch geladen.

Dadurch bleibt die Einstellung des Sendeintervalls auch nach dem Ausschalten des Gerätes erhalten.

---

## WLAN-Konfiguration

Der ESP32 arbeitet für die Konfiguration als Access Point.

```cpp
WiFi.softAP(ap_ssid, ap_pass);
```

Die IP-Adresse des Access Points wird beim Start über den seriellen Monitor ausgegeben.

Beispiel:

```text
AP gestartet
IP: 192.168.4.1
```

Anschließend kann die Konfigurationsseite über diese Adresse geöffnet werden.

---

## GPS-Fix

Nach dem Einschalten wartet das System zunächst auf einen gültigen GPS-Fix.

Während dieser Zeit wird über den seriellen Monitor ausgegeben:

```text
Warte auf GPS Fix...
```

Nach maximal 120 Sekunden wird auch ohne gültigen GPS-Fix ein Sendevorgang ausgelöst.

Dadurch bleibt der LoRaWAN-Ablauf auch bei schlechten GPS-Empfangsbedingungen funktionsfähig.

---

## Programmablauf

Vereinfacht arbeitet der Tracker nach folgendem Schema:

```text
ESP32 starten
     │
     ├── LittleFS initialisieren
     │
     ├── Sendeintervall laden
     │
     ├── WLAN Access Point starten
     │
     ├── LoRaWAN initialisieren
     │
     ├── GPS einschalten
     │
     └── System starten
             │
             ▼
        GPS-Daten lesen
             │
             ▼
        GPS-Fix vorhanden?
          │          │
         Nein       Ja
          │          │
          └────┬─────┘
               ▼
        Payload erstellen
               │
               ▼
          LoRaWAN Uplink
               │
               ▼
        Warten auf nächstes
        Übertragungsintervall
```

---

## Arduino-Bibliotheken

Für die Kompilierung werden unter anderem folgende Bibliotheken bzw. Komponenten verwendet:

```cpp
#include "LoRaWan_APP.h"
#include "Wire.h"
#include "HT_st7735.h"
#include "HT_TinyGPS++.h"
#include <FS.h>
#include <LittleFS.h>
#include <WiFi.h>
#include <WebServer.h>
```

Die LoRaWAN-Funktionen stammen aus der Heltec-Umgebung.

---

## Dateien

Das Repository enthält den Arduino-Sketch:

```text
GPS-Tracker/
│
├── GPS-Tracker.ino
├── README.md
└── images/
    └── ...
```

Falls weitere Bilder oder Dokumentationen vorhanden sind, können diese im Verzeichnis `images` abgelegt werden.

---

## Sicherheitshinweis

**Wichtig:** LoRaWAN-Zugangsdaten wie `AppKey`, `AppEUI` und gegebenenfalls weitere persönliche Geräteparameter dürfen nicht mit echten Werten in ein öffentliches GitHub-Repository hochgeladen werden.

Im veröffentlichten Sketch sollten diese Werte beispielsweise durch Platzhalter ersetzt werden:

```cpp
uint8_t devEui[] = {
  // eigene DevEUI eintragen
};

uint8_t appEui[] = {
  // eigene AppEUI eintragen
};

uint8_t appKey[] = {
  // eigenen AppKey eintragen
};
```

Der originale AppKey sollte niemals öffentlich veröffentlicht werden.

---

## Entwicklungsstand

**Version:** 26.06.2026 FINAL

Schwerpunkt dieser Version:

* verbessertes Logging
* Speicherung des Sendeintervalls mit LittleFS
* Konfiguration über WLAN-Hotspot
* GPS-Positionsübertragung über LoRaWAN
* Anzeige der GPS-Koordinaten
* Übertragung des Logging-Status

---

## Lizenz

Dieses Projekt wird auf GitHub zur privaten Dokumentation und als Beispielprojekt veröffentlicht.

Eine eigene Lizenz kann bei Bedarf ergänzt werden.
