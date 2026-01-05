#include <Arduino.h>
#define MQPIN 12

unsigned long lastDhtRead = 0;
const unsigned long dhtInterval = 2500UL; // DHT22 needs ~2s between reads

void setup() {
  Serial.begin(115200);
  while (!Serial) { delay(10); } // wait for serial on some boards
  Serial.println();
  Serial.println("=== ESP32 SENSOR TEST (no WiFi) ===");

  analogReadResolution(12);

  Serial.print("MQ analog pin: "); Serial.println(MQPIN);
  Serial.println("Start reading...");
}

float readMqAverage(int samples = 10, int delayMs = 10) {
  long sum = 0;
  for (int i = 0; i < samples; ++i) {
    int r = analogRead(MQPIN);
    sum += r;
    delay(delayMs);
  }
  return sum / (float)samples;
}

void loop() {
  unsigned long now = millis();

  if (now - lastDhtRead >= dhtInterval) {
    lastDhtRead = now;


    Serial.println("----------------------------------");
    Serial.print("Millis: "); Serial.println(now);

    float mqRaw = readMqAverage(12, 10); // 12 samples, 10ms apart
    float voltage = mqRaw * (3.3f / 4095.0f);

    Serial.print("MQ raw (avg): ");
    Serial.print(mqRaw, 1);
    Serial.print("  |  Voltage: ");
    Serial.print(voltage, 3);
    Serial.println(" V");

    float rel = (mqRaw / 4095.0f) * 100.0f;
    Serial.print("MQ relative level: ");
    Serial.print(rel, 1);
    Serial.println(" %");

    Serial.println();
  }

  delay(50);
}
