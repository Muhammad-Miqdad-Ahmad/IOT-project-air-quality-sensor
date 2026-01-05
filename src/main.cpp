#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define MQPIN 0
#define DHTPIN 2
#define DHTTYPE DHT11

const char *ssid = "Cyber Surge";
const char *wifiPassword = "cout<<INTERNETpassword";

const char *mqttServer = "192.168.100.14";
const uint16_t mqttPort = 1883;

const char *mqttUser = "esp01";
const char *mqttPassword = "pass";

const char *mqttTopic = "home/air/esp01/data";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

unsigned long lastPublish = 0;
const unsigned long publishInterval = 60UL * 1000UL; // 60 seconds

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

  // Use clientId unique if you want; here we keep it simple.
  String clientId = String("esp01-") + String((uint32_t)millis()); // basic unique-ish id
  if (client.connect(clientId.c_str(), mqttUser, mqttPassword)) {
    Serial.println("connected");
    // You may publish an "online" retained message if desired:
    client.publish("home/air/esp01/status", "online", true);
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

  setupWifi();

  client.setServer(mqttServer, mqttPort);
}

void loop() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi disconnected. Reconnecting...");
    setupWifi();
    delay(500);
  }

  // Reconnect MQTT if needed
  if (!client.connected()) {
    if (!mqttReconnect()) {
      delay(5000); // pause before next reconnect attempt
      return;
    }
  }
  client.loop();

  // Publish at interval
  if (millis() - lastPublish >= publishInterval) {
    lastPublish = millis();

    float h = dht.readHumidity();
    float t = dht.readTemperature();
    int mqValue = 0;
    for (size_t i = 0; i < 10; i++)
    {
      mqValue += analogRead(MQPIN);;
    }
    mqValue /= 10;
    

    if (isnan(h) || isnan(t)) {
      Serial.println("Failed to read from DHT sensor");
    }

    // Build JSON payload
    String payload = "{";
    payload += "\"temperature\":" + String(t, 1) + ",";
    payload += "\"humidity\":" + String(h, 1) + ",";
    payload += "\"gas_raw\":" + String(mqValue);
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