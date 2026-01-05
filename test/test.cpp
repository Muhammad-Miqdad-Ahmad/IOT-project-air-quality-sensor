// #include <WiFi.h>
// #include <PubSubClient.h>

// // === CONFIG ===
// const char *ssid = "IBRAHIM.";
// const char *wifiPassword = "ibrahim185a";

// const char *mqttServer   = "192.168.100.14";
// const uint16_t mqttPort  = 1883;
// const char *mqttUser     = "esp01";
// const char *mqttPassword = "pass";

// const int LED_PIN = 8;          // ESP32-C3 Super Mini onboard LED (common)
// const bool LED_ACTIVE_LOW = true; // usually active-low on C3 Super Mini

// WiFiClient espClient;
// PubSubClient client(espClient);

// unsigned long lastHeartbeat = 0;
// const unsigned long HEARTBEAT_MS = 2000;

// void writeLed(bool on) {
//   if (LED_ACTIVE_LOW) digitalWrite(LED_PIN, on ? LOW : HIGH);
//   else digitalWrite(LED_PIN, on ? HIGH : LOW);
// }

// // Map WiFi.status() to human text
// const char* wifiStatusToString(int s) {
//   switch (s) {
//     case WL_IDLE_STATUS:      return "WL_IDLE_STATUS (0)";
//     case WL_NO_SSID_AVAIL:    return "WL_NO_SSID_AVAIL (1)";
//     case WL_SCAN_COMPLETED:   return "WL_SCAN_COMPLETED (2)";
//     case WL_CONNECTED:        return "WL_CONNECTED (3)";
//     case WL_CONNECT_FAILED:   return "WL_CONNECT_FAILED (4)";
//     case WL_CONNECTION_LOST:  return "WL_CONNECTION_LOST (5)";
//     case WL_DISCONNECTED:     return "WL_DISCONNECTED (6)";
//     default:                  return "UNKNOWN";
//   }
// }

// // Map some common PubSubClient state codes to human text (most common ones)
// String mqttStateToString(int s) {
//   // PubSubClient returns negative values for failures in many builds.
//   // Common observed values:
//   //  0  => connected (sometimes)
//   // -1  => disconnected
//   // -2  => connect failed (network)
//   // -3  => connection lost
//   // -4  => connection timeout
//   switch (s) {
//     case 0: return "0 (probably MQTT_CONNECTED)";
//     case -1: return "-1 (MQTT_DISCONNECTED)";
//     case -2: return "-2 (MQTT_CONNECT_FAILED - network issue / no route)";
//     case -3: return "-3 (MQTT_CONNECTION_LOST)";
//     case -4: return "-4 (MQTT_CONNECTION_TIMEOUT)";
//     default: return String(s) + " (unknown code)";
//   }
// }

// void printWifiInfo() {
//   Serial.printf("[WIFI] status=%s\n", wifiStatusToString(WiFi.status()));
//   if (WiFi.status() == WL_CONNECTED) {
//     Serial.print("[WIFI] IP: ");
//     Serial.println(WiFi.localIP());
//     Serial.print("[WIFI] RSSI: ");
//     Serial.println(WiFi.RSSI());
//   }
// }

// bool tryConnectMQTT() {
//   if (client.connected()) return true;

//   Serial.print("[MQTT] Connecting to ");
//   Serial.print(mqttServer);
//   Serial.print(":");
//   Serial.print(mqttPort);
//   Serial.print(" as ");
//   Serial.println(mqttUser);

//   String clientId = String("c3dbg-") + String((uint32_t)random());
//   bool ok = client.connect(clientId.c_str(), mqttUser, mqttPassword);
//   if (ok) {
//     Serial.println("[MQTT] CONNECTED");
//     client.publish("home/air/esp01/status", "debug-online", true);
//     return true;
//   } else {
//     int st = client.state();
//     Serial.print("[MQTT] CONNECT FAILED -> state=");
//     Serial.print(st);
//     Serial.print(" : ");
//     Serial.println(mqttStateToString(st));
//     return false;
//   }
// }

// void setup() {
//   pinMode(LED_PIN, OUTPUT);
//   writeLed(false); // start with LED off
//   Serial.begin(115200);
//   delay(200);
//   Serial.println("=== ESP32-C3 DEBUG START ===");

//   // WiFi start
//   Serial.printf("[WIFI] SSID: %s\n", ssid);
//   WiFi.begin(ssid, wifiPassword);

//   // Use waitForConnectResult to block until we get an answer (safer on C3)
//   Serial.println("[WIFI] waiting for connect result (10s timeout)...");
//   u_int8_t res = WiFi.waitForConnectResult(10000); // wait up to 10s
//   Serial.print("[WIFI] waitForConnectResult -> ");
//   Serial.println((int)res);
//   Serial.printf("[WIFI] status after wait: %s\n", wifiStatusToString(WiFi.status()));

//   // If not connected, keep trying but blink fast to show we're stuck in WiFi
//   int wifiRetries = 0;
//   while (WiFi.status() != WL_CONNECTED && wifiRetries < 6) {
//     Serial.print("[WIFI] retry ");
//     Serial.println(wifiRetries + 1);
//     // fast blink while attempting
//     for (int i = 0; i < 8; ++i) { writeLed(!digitalRead(LED_PIN)); delay(150); }
//     WiFi.begin(ssid, wifiPassword);
//     res = WiFi.waitForConnectResult(5000);
//     wifiRetries++;
//   }
//   printWifiInfo();

//   // Setup MQTT client
//   client.setServer(mqttServer, mqttPort);

//   // set last heartbeat so first message comes quickly
//   lastHeartbeat = millis() - HEARTBEAT_MS;
//   randomSeed(esp_random());
// }

// void loop() {
//   // Heartbeat + action indicator
//   if (millis() - lastHeartbeat >= HEARTBEAT_MS) {
//     lastHeartbeat = millis();
//     Serial.print("[HEART] millis=");
//     Serial.println(millis());
//     printWifiInfo();

//     // LED steady ON while connected to MQTT, else blink accordingly
//     if (WiFi.status() == WL_CONNECTED && client.connected()) {
//       writeLed(true); // connected steady on
//     } else if (WiFi.status() == WL_CONNECTED && !client.connected()) {
//       // trying MQTT -> slow blink
//       for (int i = 0; i < 2; ++i) { writeLed(false); delay(200); writeLed(true); delay(200); }
//     } else {
//       // wifi not connected -> fast blink once
//       for (int i = 0; i < 3; ++i) { writeLed(!digitalRead(LED_PIN)); delay(120); }
//     }
//   }

//   // If WiFi is connected, try MQTT (non-blocking attempt)
//   if (WiFi.status() == WL_CONNECTED) {
//     if (!client.connected()) {
//       tryConnectMQTT();
//     }
//     client.loop();

//     // If connected, publish a small test every 10s
//     static unsigned long lastPub = 0;
//     if (client.connected() && millis() - lastPub > 10000) {
//       lastPub = millis();
//       String payload = "{\"test\":\"hello\",\"t\":";
//       payload += String(millis()/1000);
//       payload += "}";
//       bool ok = client.publish("home/air/esp01/debug", payload.c_str());
//       Serial.print("[MQTT] publish debug -> ");
//       Serial.println(ok ? "OK" : "FAILED");
//       // 3 quick blinks to indicate publish
//       for (int i=0;i<3;i++){ writeLed(false); delay(80); writeLed(true); delay(80); }
//     }
//   } else {
//     // try to reconnect WiFi in background every 10 seconds
//     static unsigned long lastWifiAttempt = 0;
//     if (millis() - lastWifiAttempt > 10000) {
//       lastWifiAttempt = millis();
//       Serial.println("[WIFI] attempting to reconnect...");
//       WiFi.begin(ssid, wifiPassword);
//     }
//   }
// }
