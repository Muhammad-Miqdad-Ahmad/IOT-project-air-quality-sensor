# ESP32 Air Sensor (MQ + DHT) → Mosquitto Broker → Python Frontend

> **Short:** ESP32 reads a DHT11 (temp + humidity) and an MQ-series gas sensor, publishes JSON to a Mosquitto MQTT broker (I ran the broker on both Ubuntu and Arch Linux). A small Python/Tkinter frontend subscribes and plots the values and allows CSV export.

---

## Repository layout (suggested)

```
README.md
esp/                      # ESP sketch (Arduino) — main.ino or esp_main.ino
frontend/                 # Python frontend + helper scripts
frontend/requirements.txt
frontend/setup_and_run.py # helper that creates venv and runs frontend
frontend/iot_frontend.py  # Tkinter + matplotlib frontend
LICENSE
```

> The repo includes the ESP sketch (Arduino/PlatformIO), a Python frontend, and a helper script that creates a venv and runs the frontend.

---

## Hardware

- **ESP** — ESP32 (recommended) or ESP8266 (you'll need to adapt pins and `analogRead` behavior).
- **DHT sensor** — DHT11 (in code). DHT22 is a drop-in improvement (better accuracy).
- **MQ-series gas sensor** — e.g., MQ-2 / MQ-135 (analog output).
- USB cable for power + serial.
- Linux machine running Mosquitto (this repo contains notes for Ubuntu and Arch setups).

**Wiring (basic)**

- `DHT`: data pin → `DHTPIN` (GPIO 2 in sketch). Add a 10K pull-up if required by your module.
- `MQ sensor`: analog output → `MQPIN` (default in sketch is 3). IMPORTANT: on ESP32 many boards **do not** expose ADC on GPIO3; **use an ADC-capable pin** such as `GPIO34` or `GPIO35` (ADC1 channel) to avoid conflicts with serial and Wi‑Fi. If you keep `MQPIN=3` you may get garbage or no readings.
- Ground (GND) and Vcc: match sensor module voltage (often 5V for MQ modules; ESP32 uses 3.3V logic — check your module and use a voltage divider or level shifting if needed). Many MQ modules work with 5V heater but provide safe analog outputs to 3.3V microcontrollers — check the module datasheet!

**Pin example for ESP32 (recommended)**
- `DHTPIN = 2` (GPIO2)
- `MQPIN = 34` (change in code if needed)

---

## Firmware (ESP sketch) — key points

The included sketch (shown in `esp/`) does the following:

- Connects to WiFi using `ssid` and `wifiPassword` variables.
- Connects to the configured MQTT broker at `mqttServer:mqttPort` using `mqttUser`/`mqttPassword`.
- Publishes JSON payloads to `mqttTopic` every `publishInterval` (default 10 s in the pasted code, the comment says 60s but code sets 10 * 1000 = 10s — verify and adjust as needed).
- Publishes an "online" retained status message to `home/air/esp01/status` on connect.

**JSON payload format**

```json
{
  "temperature": 23.4,
  "humidity": 48.2,
  "gas_raw": 286
}
```

**Important code notes & gotchas**
- `MQPIN` in the sketch is read via `analogRead()`; take the average of 10 samples for stability (implemented).
- `DHT.readTemperature()` and `DHT.readHumidity()` can return `NaN` when the sensor fails; the sketch logs this and leaves the value out.
- `client.connect(...)` returns an MQTT return code — the sketch prints `client.state()` when failing. Inspect that value to understand the failure reason (bad credentials, unreachable broker, etc.).
- If you are using an ESP32: avoid ADC2 pins when WiFi is used (ADC2 shares resources with WiFi); prefer ADC1 pins (GPIO32–GPIO39).
- The sketch currently publishes the online status with `retain=true`. Keep this if you want new clients to see the node is online.

---

## MQTT broker setup

This project uses Mosquitto. Below are concise instructions for **Ubuntu** and **Arch Linux**.

> Replace example IPs / usernames / passwords with secure values for production.

### Ubuntu (apt)

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Create a password file and add a user (example username `esp01`):

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp01
# enter the password (e.g. 'pass')
```

Create or edit a Mosquitto config fragment `/etc/mosquitto/conf.d/esp.conf`:

```
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd
# optional: bind_address <your-ip>
```

Restart Mosquitto:

```bash
sudo systemctl restart mosquitto
```

Open firewall (if running `ufw`):

```bash
sudo ufw allow 1883/tcp
```

Test (from any machine on the same network):

```bash
mosquitto_sub -h <broker-ip> -t home/air/esp01/data -u esp01 -P pass -v
```

### Arch Linux (pacman)

```bash
sudo pacman -Syu
sudo pacman -S mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto.service
```

Password file and config are the same path (`/etc/mosquitto/`). Create `/etc/mosquitto/conf.d/esp.conf` with the same content as the Ubuntu example and restart the service:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp01
sudo systemctl restart mosquitto
```

If you use `firewalld` on Arch, allow port 1883:

```bash
sudo firewall-cmd --add-port=1883/tcp --permanent
sudo firewall-cmd --reload
```

**Debugging tips**
- Use `mosquitto_sub`/`mosquitto_pub` from a client machine to test the broker manually.
- Check logs: `sudo journalctl -u mosquitto -f` for realtime logs.

---

## Frontend (Python/Tkinter)

The `iot_frontend.py` subscribes to your `DEFAULT_TOPIC` (defaults to `home/air/esp01/data`) and displays live values, keeps a history buffer, plots recent values, and can export the raw log to CSV.

### Requirements

- Python 3.8+
- `paho-mqtt`
- `matplotlib`

`frontend/requirements.txt` contains:

```
paho-mqtt
matplotlib
```

### Install & Run (manual steps)

```bash
cd frontend
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
python iot_frontend.py
```

Or run the included helper script (if present) which will create the virtual env and run the frontend for you — check the script name and run it with Python3:

```bash
python3 setup_and_run.py
# or
./setup_and_run.py
```

### Using the GUI
- Enter broker IP, port, topic, username, password, then `Connect`.
- The GUI shows current readings, a scrolling log, and a plot of recent values.
- Use `Export CSV` to save messages (timestamp, topic, payload, parsed fields).

---

## Example: Testing & verification

1. Start Mosquitto on the Linux host: `sudo systemctl start mosquitto`.
2. Point the ESP sketch `mqttServer` variable to the broker IP and upload the sketch.
3. Open the serial monitor (115200 baud) to observe WiFi and MQTT connection logs.
4. From any client machine, subscribe to the topic to confirm messages:

```bash
mosquitto_sub -h 192.168.100.122 -t home/air/esp01/data -u esp01 -P pass -v
```

You should see JSON payloads like the example above.

---

## Troubleshooting — common problems and fixes

- **No MQTT connection / `client.state()` non-zero**: check broker IP/port, username/password, firewall, broker logs (`journalctl -u mosquitto -f`).
- **DHT returns `NaN`**: check wiring, sensor Vcc, long wires, and sensor age; try read intervals >2s.
- **MQ reading is 0 or noisy**: ensure the analog pin is ADC-capable; change `MQPIN` to a known ADC pin (e.g., 34) on ESP32 and reflash.
- **WiFi not connecting**: verify SSID/password, and check for captive portals; serial prints will help.
- **ESP repeatedly reconnects**: might be an unstable WiFi signal, insufficient power, or blocking code in loop — keep `loop()` lightweight and ensure `client.loop()` is called frequently (it is in the sketch).

---

## Security & production notes

- Do **not** use plaintext passwords for production. Use TLS and proper authentication where possible — Mosquitto supports TLS listeners (`listener 8883`) and required certificates.
- If your broker is reachable from the internet, lock it down with firewall rules and strong credentials.
- Consider using unique client IDs and short keepalive values only when necessary.

---

## Possible improvements / TODOs

- Add TLS (MQTT over TLS port 8883) and update the ESP to use secure connections.
- Convert the MQ raw ADC values to PPM using calibration curves for meaningful gas concentrations.
- Add persistent logging on the broker side (e.g., InfluxDB + Grafana) for long-term storage and dashboards.
- Add OTA updates for the ESP sketch.
- Improve frontend with asyncio and web UI for remote access.

---

## License

Choose a license (MIT recommended for small projects). Example `LICENSE` file can be added.

---

## Contribution

PRs and issues welcome. If you open an issue, include firmware serial logs and frontend logs where possible.

---

## Credits

- DHT library (Adafruit/Arduino DHT)
- PubSubClient (for MQTT)
- paho-mqtt and matplotlib for frontend



*README generated and included in repo.*

