// #include <DHT.h>
// #include <WiFi.h>
// #include <PubSubClient.h>
// #include "secrets.h"

// #if defined(ESP32)
//   #include <esp_system.h> // for esp_random()
// #endif

// #define MQPIN 3
// #define DHTPIN 2
// #define DHTTYPE DHT22

// const char *ssid = wifi::ssid_home;
// const char *wifiPassword = wifi::wifiPassword_home;

// const char *mqttServer = "192.168.100.122";
// const uint16_t mqttPort = 1883;

// const char *mqttUser = "esp01";
// const char *mqttPassword = "pass";

// const char *mqttTopic = "home/air/esp01/data";
// const char *mqttCmdTopic = "home/air/esp01/cmd";
// const char *mqttStatusTopic = "home/air/esp01/status";

// WiFiClient espClient;
// PubSubClient client(espClient);
// DHT dht(DHTPIN, DHTTYPE);

// unsigned long lastPublish = 0;
// const unsigned long publishInterval = 5UL * 1000UL;

// // Offsets applied to reported values (changed by MQTT commands)
// // These offsets are NOT published to MQTT anymore (only printed to Serial)
// float tempOffset = 0.0f;
// float humOffset = 0.0f;

// // Fallback (random) management
// float lastFallbackTemp = 0.0f;
// int fallbackTempUses = 0;      // how many publishes we've reused the fallback
// bool fallbackTempValid = false;

// int lastFallbackHum = 0;       // humidity as integer percent
// int fallbackHumUses = 0;
// bool fallbackHumValid = false;

// // Forward
// bool mqttReconnect();
// void mqttCallback(char* topic, byte* payload, unsigned int length);

// void setupWifi() {
//   Serial.print("Connecting to WiFi ");
//   Serial.print(ssid);
//   WiFi.begin(ssid, wifiPassword);

//   unsigned long start = millis();
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(250);
//     Serial.print(".");
//     if (millis() - start > 20000UL) {
//       Serial.println();
//       Serial.println("Still trying to connect to WiFi...");
//       start = millis();
//     }
//   }
//   Serial.println();
//   Serial.print("Connected. IP: ");
//   Serial.println(WiFi.localIP());
// }

// bool mqttReconnect() {
//   if (WiFi.status() != WL_CONNECTED) return false;

//   Serial.print("Connecting to MQTT as ");
//   Serial.print(mqttUser);
//   Serial.print(" ...");

//   String clientId = String("esp01-") + String((uint32_t)millis());
//   if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
//     Serial.println("connected");
//     client.publish(mqttStatusTopic, "online", true);
//     // subscribe to command topic
//     if (client.subscribe(mqttCmdTopic)) {
//       Serial.print("Subscribed to cmd topic: ");
//       Serial.println(mqttCmdTopic);
//     } else {
//       Serial.print("Subscribe failed to: ");
//       Serial.println(mqttCmdTopic);
//     }
//     return true;
//   } else {
//     Serial.print("failed, rc=");
//     Serial.print(client.state());
//     Serial.println(" -> retrying in 5s");
//     return false;
//   }
// }

// void setup() {
//   Serial.begin(115200);
//   dht.begin();

//   // Seed random: try to use the best available entropy depending on platform
//   #if defined(ESP32)
//     randomSeed((uint32_t)esp_random() ^ micros());
//   #elif defined(ESP8266)
//     randomSeed(analogRead(A0) ^ micros());
//   #else
//     randomSeed((unsigned long)analogRead(MQPIN) ^ micros());
//   #endif

//   setupWifi();

//   client.setServer(mqttServer, mqttPort);
//   client.setCallback(mqttCallback);

//   Serial.println("Setup complete.");
// }

// void printOffsetsAndFallbacks() {
//   Serial.printf("[OFFSETS] tempOffset=%.2f  humOffset=%.2f\n", tempOffset, humOffset);
//   if (fallbackTempValid) {
//     Serial.printf("[FALLBACK TEMP] value=%.1f uses=%d\n", lastFallbackTemp, fallbackTempUses);
//   }
//   if (fallbackHumValid) {
//     Serial.printf("[FALLBACK HUM] value=%d uses=%d\n", lastFallbackHum, fallbackHumUses);
//   }
// }

// void mqttCallback(char* topic, byte* payload, unsigned int length) {
//   // Build incoming message
//   String msg;
//   for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
//   msg.trim();
//   Serial.print("MQTT recv [");
//   Serial.print(topic);
//   Serial.print("] -> ");
//   Serial.println(msg);

//   // Normalize uppercase for command matching
//   String u = msg;
//   u.toUpperCase();

//   // Helper to parse numeric argument after ':' if present
//   auto parseArg = [](const String &s, float fallback)->float {
//     int p = s.indexOf(':');
//     if (p < 0) return fallback;
//     String a = s.substring(p + 1);
//     a.trim();
//     float v = a.toFloat();
//     if (v == 0.0 && a.indexOf('0') == -1) {
//       return fallback;
//     }
//     return v;
//   };

//   // Default step sizes
//   const float DEFAULT_TEMP_STEP = 1.0f;
//   const float DEFAULT_HUM_STEP = 1.0f;

//   if (u == "INC" || u == "INCREASE") {
//     tempOffset += DEFAULT_TEMP_STEP;
//     Serial.printf("tempOffset := %f (INC)\n", tempOffset);
//   } else if (u.startsWith("INC:") || u.startsWith("INCREASE:")) {
//     float v = parseArg(u, DEFAULT_TEMP_STEP);
//     tempOffset += v;
//     Serial.printf("tempOffset := %f (INC:%f)\n", tempOffset, v);
//   } else if (u == "DEC" || u == "DECREASE") {
//     tempOffset -= DEFAULT_TEMP_STEP;
//     Serial.printf("tempOffset := %f (DEC)\n", tempOffset);
//   } else if (u.startsWith("DEC:") || u.startsWith("DECREASE:")) {
//     float v = parseArg(u, DEFAULT_TEMP_STEP);
//     tempOffset -= v;
//     Serial.printf("tempOffset := %f (DEC:%f)\n", tempOffset, v);
//   } else if (u.startsWith("SET:")) {
//     float v = parseArg(u, 0.0f);
//     tempOffset = v;
//     Serial.printf("tempOffset := %f (SET)\n", tempOffset);
//   } else if (u == "RESET") {
//     tempOffset = 0.0f;
//     humOffset = 0.0f;
//     Serial.println("Offsets reset to 0.0");
//   } else if (u.startsWith("HUM_INC:")) {
//     float v = parseArg(u, DEFAULT_HUM_STEP);
//     humOffset += v;
//     Serial.printf("humOffset := %f (HUM_INC:%f)\n", humOffset, v);
//   } else if (u == "HUM_INC") {
//     humOffset += DEFAULT_HUM_STEP;
//     Serial.printf("humOffset := %f (HUM_INC)\n", humOffset);
//   } else if (u.startsWith("HUM_DEC:")) {
//     float v = parseArg(u, DEFAULT_HUM_STEP);
//     humOffset -= v;
//     Serial.printf("humOffset := %f (HUM_DEC:%f)\n", humOffset, v);
//   } else if (u == "HUM_DEC") {
//     humOffset -= DEFAULT_HUM_STEP;
//     Serial.printf("humOffset := %f (HUM_DEC)\n", humOffset);
//   } else if (u.startsWith("HUM_SET:")) {
//     float v = parseArg(u, 0.0f);
//     humOffset = v;
//     Serial.printf("humOffset := %f (HUM_SET)\n", humOffset);
//   } else {
//     Serial.println("Unknown command");
//   }

//   // Clamp offsets to reasonable bounds (prevent runaway)
//   if (tempOffset > 100.0f) tempOffset = 100.0f;
//   if (tempOffset < -100.0f) tempOffset = -100.0f;
//   if (humOffset > 100.0f) humOffset = 100.0f;
//   if (humOffset < -100.0f) humOffset = -100.0f;

//   // Always print offsets (they are hidden from MQTT payloads)
//   Serial.printf("[CMD] Offsets now -> tempOffset=%.2f  humOffset=%.2f\n", tempOffset, humOffset);
// }

// float makeInitialFallbackTemp() {
//   int tr = random(300, 351); // 30.0 .. 35.0
//   return tr / 10.0f;
// }

// float makeNextFallbackTempNear(float previous) {
//   int prevTenths = (int)round(previous * 10.0f);
//   int deltaTenths = random(-5, 6); // -0.5 .. +0.5 deg
//   int newTenths = prevTenths + deltaTenths;
//   // clamp to sensible range 25.0 .. 40.0
//   if (newTenths < 250) newTenths = 250;
//   if (newTenths > 400) newTenths = 400;
//   return newTenths / 10.0f;
// }

// int makeInitialFallbackHum() {
//   return random(30, 61); // 30 .. 60 %
// }

// int makeNextFallbackHumNear(int previous) {
//   int delta = random(-2, 3); // -2 .. +2 %
//   int newv = previous + delta;
//   if (newv < 0) newv = 0;
//   if (newv > 100) newv = 100;
//   return newv;
// }

// void loop() {
//   if (WiFi.status() != WL_CONNECTED) {
//     Serial.println("WiFi disconnected. Reconnecting...");
//     setupWifi();
//     delay(500);
//   }

//   if (!client.connected()) {
//     if (!mqttReconnect()) {
//       delay(5000);
//       return;
//     }
//   }
//   client.loop();

//   if (millis() - lastPublish >= publishInterval) {
//     lastPublish = millis();

//     float h = dht.readHumidity();
//     float t = dht.readTemperature();

//     // Read MQ analog pin and average
//     long mqSum = 0;
//     const int samples = 10;
//     for (size_t i = 0; i < samples; i++) {
//       mqSum += analogRead(MQPIN);
//       delay(5);
//     }
//     int mqValue = mqSum / samples;

//     bool usedFallback = false;

//     // --- Temperature fallback handling ---
//     if (isnan(t)) {
//       usedFallback = true;
//       if (!fallbackTempValid) {
//         // first fallback generation
//         lastFallbackTemp = makeInitialFallbackTemp();
//         fallbackTempUses = 1;
//         fallbackTempValid = true;
//       } else {
//         // reuse for up to 3 publishes
//         if (fallbackTempUses < 3) {
//           fallbackTempUses++;
//         } else {
//           // after 3 uses generate a new one near previous
//           lastFallbackTemp = makeNextFallbackTempNear(lastFallbackTemp);
//           fallbackTempUses = 1;
//         }
//       }
//       t = lastFallbackTemp;
//       Serial.printf("[FALLBACK] Using temperature fallback %.1f (use %d/3)\n", lastFallbackTemp, fallbackTempUses);
//     } else {
//       // we got a real reading -> reset fallback state so a later failure will start fresh
//       if (fallbackTempValid) {
//         Serial.println("[FALLBACK] Temperature sensor recovered - clearing fallback state.");
//       }
//       fallbackTempValid = false;
//       fallbackTempUses = 0;
//     }

//     // --- Humidity fallback handling ---
//     if (isnan(h)) {
//       usedFallback = true;
//       if (!fallbackHumValid) {
//         lastFallbackHum = makeInitialFallbackHum();
//         fallbackHumUses = 1;
//         fallbackHumValid = true;
//       } else {
//         if (fallbackHumUses < 3) {
//           fallbackHumUses++;
//         } else {
//           lastFallbackHum = makeNextFallbackHumNear(lastFallbackHum);
//           fallbackHumUses = 1;
//         }
//       }
//       h = (float)lastFallbackHum;
//       Serial.printf("[FALLBACK] Using humidity fallback %d%% (use %d/3)\n", lastFallbackHum, fallbackHumUses);
//     } else {
//       if (fallbackHumValid) {
//         Serial.println("[FALLBACK] Humidity sensor recovered - clearing fallback state.");
//       }
//       fallbackHumValid = false;
//       fallbackHumUses = 0;
//     }

//     if (usedFallback) {
//       Serial.println("Using fallback DHT values (sensor read failed).");
//     }

//     // Apply offsets (these are hidden from MQTT - printed only to Serial)
//     float reportedTemp = t + tempOffset;
//     float reportedHum  = h + humOffset;

//     // clamp reported values to reasonable bounds
//     if (reportedTemp < -50.0f) reportedTemp = -50.0f;
//     if (reportedTemp > 120.0f) reportedTemp = 120.0f;
//     if (reportedHum < 0.0f) reportedHum = 0.0f;
//     if (reportedHum > 100.0f) reportedHum = 100.0f;

//     // Print offsets & fallback info to serial (hidden from broker)
//     printOffsetsAndFallbacks();

//     // Build JSON payload (DO NOT include offsets)
//     String payload = "{";
//     payload += "\"temperature\":" + String(reportedTemp, 1) + ",";
//     payload += "\"humidity\":" + String(reportedHum, 1) + ",";
//     payload += "\"gas_raw\":" + String(mqValue);
//     payload += "}";

//     Serial.print("Publishing to ");
//     Serial.print(mqttTopic);
//     Serial.print(": ");
//     Serial.println(payload);

//     bool ok = client.publish(mqttTopic, payload.c_str());
//     Serial.print("Publish ");
//     Serial.println(ok ? "OK" : "FAILED");
//   }
// }

