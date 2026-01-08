// #include <DHT.h>
// #include <WiFi.h>
// #include <PubSubClient.h>

// #if defined(ESP32)
//   #include <esp_system.h> // for esp_random()
// #endif

// #define MQPIN 3
// #define DHTPIN 2
// #define DHTTYPE DHT11

// const char *wifiPassword = "library@itu1234*";
// const char *ssid = "ITU-Library";

// const char *mqttServer = "172.16.22.197";
// const uint16_t mqttPort = 1883;

// const char *mqttUser = "esp01";
// const char *mqttPassword = "pass";

// const char *mqttTopic = "home/air/esp01/data";

// WiFiClient espClient;
// PubSubClient client(espClient);
// DHT dht(DHTPIN, DHTTYPE);

// unsigned long lastPublish = 0;
// const unsigned long publishInterval = 10UL * 1000UL;

// void setupWifi() {
//   Serial.print("Connecting to WiFi ");
//   Serial.print(ssid);
//   WiFi.begin(ssid, wifiPassword);

//   unsigned long start = millis();
//   while (WiFi.status() != WL_CONNECTED) {
//     delay(250);
//     Serial.print(".");
//     // avoid locking forever: print status every 10s
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
//     client.publish("home/air/esp01/status", "online", true);
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
//     // analogRead(A0) is usually available on ESP8266
//     randomSeed(analogRead(A0) ^ micros());
//   #else
//     // fallback: try reading MQ pin (may be 0) + micros()
//     randomSeed((unsigned long)analogRead(MQPIN) ^ micros());
//   #endif

//   setupWifi();

//   client.setServer(mqttServer, mqttPort);
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

//     // Failsafe for temperature: random between 30.0 and 35.0 (one decimal)
//     if (isnan(t)) {
//       int tr = random(300, 351); // 300..350 -> 30.0..35.0
//       t = tr / 10.0;
//       Serial.print("DHT temp NAN -> fallback temp: ");
//       Serial.println(t);
//       usedFallback = true;
//     }

//     // Failsafe for humidity: random between 30.0 and 60.0 (one decimal)
//     if (isnan(h)) {
//       int hr = random(300, 601); // 300..600 -> 30.0..60.0
//       h = hr / 10.0;
//       Serial.print("DHT hum  NAN -> fallback hum:  ");
//       Serial.println(h);
//       usedFallback = true;
//     }

//     if (usedFallback) {
//       Serial.println("Using fallback DHT values (sensor read failed).");
//     }

//     // Build JSON payload
//     String payload = "{";
//     payload += "\"temperature\":" + String(t, 1) + ",";
//     payload += "\"humidity\":" + String(h, 1) + ",";
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

#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

#if defined(ESP32)
  #include <esp_system.h> // for esp_random()
#endif

#define MQPIN 3
#define DHTPIN 2
#define DHTTYPE DHT11

const char *wifiPassword = "library@itu1234*";
const char *ssid = "ITU-Library";

const char *mqttServer = "172.16.22.197";
const uint16_t mqttPort = 1883;

const char *mqttUser = "esp01";
const char *mqttPassword = "pass";

const char *mqttTopic = "home/air/esp01/data";
const char *mqttCmdTopic = "home/air/esp01/cmd";
const char *mqttStatusTopic = "home/air/esp01/status";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastPublish = 0;
const unsigned long publishInterval = 10UL * 1000UL;

// Offsets applied to reported values (changed by MQTT commands)
float tempOffset = 0.0f;
float humOffset = 0.0f;

// Forward
bool mqttReconnect();
void mqttCallback(char* topic, byte* payload, unsigned int length);

void setupWifi() {
  Serial.print("Connecting to WiFi ");
  Serial.print(ssid);
  WiFi.begin(ssid, wifiPassword);

  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print(".");
    // avoid locking forever: print status every 10s
    if (millis() - start > 20000UL) {
      Serial.println();
      Serial.println("Still trying to connect to WiFi...");
      start = millis();
    }
  }
  Serial.println();
  Serial.print("Connected. IP: ");
  Serial.println(WiFi.localIP());
}

bool mqttReconnect() {
  if (WiFi.status() != WL_CONNECTED) return false;

  Serial.print("Connecting to MQTT as ");
  Serial.print(mqttUser);
  Serial.print(" ...");

  String clientId = String("esp01-") + String((uint32_t)millis());
  if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
    Serial.println("connected");
    client.publish(mqttStatusTopic, "online", true);
    // subscribe to command topic
    if (client.subscribe(mqttCmdTopic)) {
      Serial.print("Subscribed to cmd topic: ");
      Serial.println(mqttCmdTopic);
    } else {
      Serial.print("Subscribe failed to: ");
      Serial.println(mqttCmdTopic);
    }
    return true;
  } else {
    Serial.print("failed, rc=");
    Serial.print(client.state());
    Serial.println(" -> retrying in 5s");
    return false;
  }
}

void setup() {
  Serial.begin(115200);
  dht.begin();

  // Seed random: try to use the best available entropy depending on platform
  #if defined(ESP32)
    randomSeed((uint32_t)esp_random() ^ micros());
  #elif defined(ESP8266)
    // analogRead(A0) is usually available on ESP8266
    randomSeed(analogRead(A0) ^ micros());
  #else
    // fallback: try reading MQ pin (may be 0) + micros()
    randomSeed((unsigned long)analogRead(MQPIN) ^ micros());
  #endif

  setupWifi();

  client.setServer(mqttServer, mqttPort);
  client.setCallback(mqttCallback);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  // Build incoming message
  String msg;
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  msg.trim();
  Serial.print("MQTT recv [");
  Serial.print(topic);
  Serial.print("] -> ");
  Serial.println(msg);

  // Normalize uppercase for command matching
  String u = msg;
  u.toUpperCase();

  // Helper to parse numeric argument after ':' if present
  auto parseArg = [](const String &s, float fallback)->float {
    int p = s.indexOf(':');
    if (p < 0) return fallback;
    String a = s.substring(p + 1);
    a.trim();
    float v = a.toFloat();
    if (v == 0.0 && a.indexOf('0') == -1) {
      // toFloat returned 0 but string was not a zero-like -> parse failed
      return fallback;
    }
    return v;
  };

  // Default step sizes
  const float DEFAULT_TEMP_STEP = 1.0f;
  const float DEFAULT_HUM_STEP = 1.0f;

  if (u == "INC" || u == "INCREASE") {
    // increase temp by default step
    tempOffset += DEFAULT_TEMP_STEP;
    Serial.printf("tempOffset := %f (INC)\n", tempOffset);
  } else if (u.startsWith("INC:") || u.startsWith("INCREASE:")) {
    float v = parseArg(u, DEFAULT_TEMP_STEP);
    tempOffset += v;
    Serial.printf("tempOffset := %f (INC:%f)\n", tempOffset, v);
  } else if (u == "DEC" || u == "DECREASE") {
    tempOffset -= DEFAULT_TEMP_STEP;
    Serial.printf("tempOffset := %f (DEC)\n", tempOffset);
  } else if (u.startsWith("DEC:") || u.startsWith("DECREASE:")) {
    float v = parseArg(u, DEFAULT_TEMP_STEP);
    tempOffset -= v;
    Serial.printf("tempOffset := %f (DEC:%f)\n", tempOffset, v);
  } else if (u.startsWith("SET:")) {
    float v = parseArg(u, 0.0f);
    tempOffset = v;
    Serial.printf("tempOffset := %f (SET)\n", tempOffset);
  } else if (u == "RESET") {
    tempOffset = 0.0f;
    humOffset = 0.0f;
    Serial.println("Offsets reset to 0.0");
  } else if (u.startsWith("HUM_INC:")) {
    float v = parseArg(u, DEFAULT_HUM_STEP);
    humOffset += v;
    Serial.printf("humOffset := %f (HUM_INC:%f)\n", humOffset, v);
  } else if (u == "HUM_INC") {
    humOffset += DEFAULT_HUM_STEP;
    Serial.printf("humOffset := %f (HUM_INC)\n", humOffset);
  } else if (u.startsWith("HUM_DEC:")) {
    float v = parseArg(u, DEFAULT_HUM_STEP);
    humOffset -= v;
    Serial.printf("humOffset := %f (HUM_DEC:%f)\n", humOffset, v);
  } else if (u == "HUM_DEC") {
    humOffset -= DEFAULT_HUM_STEP;
    Serial.printf("humOffset := %f (HUM_DEC)\n", humOffset);
  } else if (u.startsWith("HUM_SET:")) {
    float v = parseArg(u, 0.0f);
    humOffset = v;
    Serial.printf("humOffset := %f (HUM_SET)\n", humOffset);
  } else {
    Serial.println("Unknown command");
  }

  // Clamp offsets to reasonable bounds (prevent runaway)
  if (tempOffset > 100.0f) tempOffset = 100.0f;
  if (tempOffset < -100.0f) tempOffset = -100.0f;
  if (humOffset > 100.0f) humOffset = 100.0f;
  if (humOffset < -100.0f) humOffset = -100.0f;
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    setupWifi();
    delay(500);
  }

  if (!client.connected()) {
    if (!mqttReconnect()) {
      delay(5000);
      return;
    }
  }
  client.loop();

  if (millis() - lastPublish >= publishInterval) {
    lastPublish = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();

    // Read MQ analog pin and average
    long mqSum = 0;
    const int samples = 10;
    for (size_t i = 0; i < samples; i++) {
      mqSum += analogRead(MQPIN);
      delay(5);
    }
    int mqValue = mqSum / samples;

    bool usedFallback = false;

    // Failsafe for temperature: random between 30.0 and 35.0 (one decimal)
    if (isnan(t)) {
      int tr = random(300, 351); // 300..350 -> 30.0..35.0
      t = tr / 10.0;
      Serial.print("DHT temp NAN -> fallback temp: ");
      Serial.println(t);
      usedFallback = true;
    }

    // Failsafe for humidity: random between 30.0 and 60.0 (one decimal)
    if (isnan(h)) {
      int hr = random(300, 601); // 300..600 -> 30.0..60.0
      h = hr / 10.0;
      Serial.print("DHT hum  NAN -> fallback hum:  ");
      Serial.println(h);
      usedFallback = true;
    }

    if (usedFallback) {
      Serial.println("Using fallback DHT values (sensor read failed).");
    }

    // Apply offsets
    float reportedTemp = t + tempOffset;
    float reportedHum  = h + humOffset;

    // Build JSON payload (includes offsets and fallback flag)
    String payload = "{";
    payload += "\"temperature\":" + String(reportedTemp, 1) + ",";
    payload += "\"humidity\":" + String(reportedHum, 1) + ",";
    payload += "\"gas_raw\":" + String(mqValue) + ",";
    payload += "\"temp_offset\":" + String(tempOffset, 2) + ",";
    payload += "\"hum_offset\":" + String(humOffset, 2) + ",";
    payload += "\"fallback\":" + String(usedFallback ? "true" : "false");
    payload += "}";

    Serial.print("Publishing to ");
    Serial.print(mqttTopic);
    Serial.print(": ");
    Serial.println(payload);

    bool ok = client.publish(mqttTopic, payload.c_str());
    Serial.print("Publish ");
    Serial.println(ok ? "OK" : "FAILED");
  }
}


