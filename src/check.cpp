/*
  ESP32 SENSOR TEST (no WiFi)
  - Reads DHT22 on DHTPIN
  - Reads MQ analog on MQPIN (averages several samples)
  - Prints raw + voltage + simple status to Serial

  IMPORTANT:
  - If your MQ module is powered from 5V, DO NOT connect the module analog output
    directly to the ESP32 ADC pin (ESP32 ADC is not 5V tolerant). Power the MQ
    module with 3.3V or use a proper voltage divider/level-shifter on the analog
    output.
  - Change MQPIN to match the ADC-capable pin you wired.
*/

// #include <DHT.h>

// #define DHTPIN 2        // DHT data pin (change if needed)
// #define DHTTYPE DHT22

// // Change this to the ADC pin you're using. 34 is ADC1_CH6 on many ESP32 boards.
// #define MQPIN 0

// DHT dht(DHTPIN, DHTTYPE);

// unsigned long lastDhtRead = 0;
// const unsigned long dhtInterval = 2500UL; // DHT22 needs ~2s between reads

// void setup() {
//   Serial.begin(115200);
//   while (!Serial) { delay(10); } // wait for serial on some boards
//   Serial.println();
//   Serial.println("=== ESP32 SENSOR TEST (no WiFi) ===");

//   dht.begin();

//   // Optional: ensure ADC resolution is 12-bit (0..4095)
//   analogReadResolution(12);

//   // If you want you can set attenuation to read wider range (optional)
//   // analogSetPinAttenuation(MQPIN, ADC_11db); // uncomment if you understand attenuation

//   Serial.print("DHT pin: "); Serial.println(DHTPIN);
//   Serial.print("MQ analog pin: "); Serial.println(MQPIN);
//   Serial.println("Start reading...");
// }

// float readMqAverage(int samples = 10, int delayMs = 10) {
//   long sum = 0;
//   for (int i = 0; i < samples; ++i) {
//     int r = analogRead(MQPIN);
//     sum += r;
//     delay(delayMs);
//   }
//   return sum / (float)samples;
// }

// void loop() {
//   unsigned long now = millis();

//   // Read DHT every ~2.5s
//   if (now - lastDhtRead >= dhtInterval) {
//     lastDhtRead = now;

//     float humidity = dht.readHumidity();
//     float tempC = dht.readTemperature();
//     float tempF = dht.readTemperature(true);

//     Serial.println("----------------------------------");
//     Serial.print("Millis: "); Serial.println(now);

//     if (isnan(humidity) || isnan(tempC)) {
//       Serial.println("DHT22: Failed to read sensor!");
//     } else {
//       Serial.print("DHT22 Temperature: ");
//       Serial.print(tempC, 1);
//       Serial.print(" °C  (");
//       Serial.print(tempF, 1);
//       Serial.println(" °F)");

//       Serial.print("DHT22 Humidity: ");
//       Serial.print(humidity, 1);
//       Serial.println(" %");
//     }

//     // Read MQ analog (average)
//     float mqRaw = readMqAverage(12, 10); // 12 samples, 10ms apart
//     // Convert raw ADC (0..4095 at 12-bit) to voltage (assuming 3.3V reference)
//     float voltage = mqRaw * (3.3f / 4095.0f);

//     Serial.print("MQ raw (avg): ");
//     Serial.print(mqRaw, 1);
//     Serial.print("  |  Voltage: ");
//     Serial.print(voltage, 3);
//     Serial.println(" V");

//     // A very rough "relative" level (0..100) — not a calibrated ppm
//     float rel = (mqRaw / 4095.0f) * 100.0f;
//     Serial.print("MQ relative level: ");
//     Serial.print(rel, 1);
//     Serial.println(" %");

//     Serial.println();
//   }

//   // small idle delay so loop doesn't hog CPU
//   delay(50);
// }


// #include <DHT.h>

// #define DHTPIN 21        // change if your data line is on a different pin
// #define DHTTYPE DHT11   // or DHT22 if that's what you have

// DHT dht(DHTPIN, DHTTYPE);

// unsigned long lastRead = 0;
// const unsigned long READ_INTERVAL = 2000UL; // DHT requires >=1s between reads; 2s is safe
// int failCount = 0;

// void setup() {
//   Serial.begin(115200);
//   delay(100); // let serial settle
//   Serial.println();
//   Serial.println("=== DHT sensor test ===");
//   Serial.print("Pin: "); Serial.println(DHTPIN);
//   Serial.print("Type: "); Serial.println(DHTTYPE == DHT11 ? "DHT11" : "DHT22/AM2302");
//   dht.begin();
// }

// void loop() {
//   if (millis() - lastRead < READ_INTERVAL) return;
//   lastRead = millis();

//   Serial.println();
//   Serial.print("Reading... (failCount=");
//   Serial.print(failCount);
//   Serial.println(")");

//   float h = dht.readHumidity();
//   float tC = dht.readTemperature();        // Celsius
//   float tF = dht.readTemperature(true);    // Fahrenheit

//   // Check for failed read
//   if (isnan(h) || isnan(tC) || isnan(tF)) {
//     failCount++;
//     Serial.println("ERROR: Failed to read from DHT sensor (NaN).");
//     Serial.println("Hints: check wiring, power, pull-up resistor, and correct sensor type.");
//   } else {
//     failCount = 0; // reset on success
//     Serial.print("Temperature: ");
//     Serial.print(tC, 1);
//     Serial.print(" °C  (");
//     Serial.print(tF, 1);
//     Serial.println(" °F)");

//     Serial.print("Humidity: ");
//     Serial.print(h, 1);
//     Serial.println(" %");

//     // Optional: compute heat index (C)
//     float heatIndexC = dht.computeHeatIndex(tC, h, false);
//     Serial.print("Heat index (approx): ");
//     Serial.print(heatIndexC, 1);
//     Serial.println(" °C");
//   }
// }
