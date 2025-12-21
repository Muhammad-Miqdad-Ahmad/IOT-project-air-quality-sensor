#include <DHT.h>
#include <WiFi.h>
#include <PubSubClient.h>

#define MQPIN 0
#define DHTPIN 4
#define DHTTYPE DHT22

const char *ssid = "Cyber Surge";
const char *mqttServer = "192.168.1.100"; // Local broker IP or cloud address
const char *password = "cout<<INTERNETpassword";

WiFiClient espClient;
PubSubClient client(espClient);
DHT dht(DHTPIN, DHTTYPE);

void setup()
{
  dht.begin();
  Serial.begin(115200);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
  }
  client.setServer(mqttServer, 1883);
}

void loop()
{
  if (!client.connected())
  {
    client.connect("esp32Client");
  }
  client.loop();

  float h = dht.readHumidity();
  float t = dht.readTemperature();
  int mqValue = analogRead(MQPIN);

  if (!isnan(h) && !isnan(t))
  {
    String payload = "{";
    payload += "\"temperature\":" + String(t, 1) + ",";
    payload += "\"humidity\":" + String(h, 1) + ",";
    payload += "\"gas_raw\":" + String(mqValue);
    payload += "}";
    client.publish("home/air", (char *)payload.c_str());
  }

  delay(60000);
}
