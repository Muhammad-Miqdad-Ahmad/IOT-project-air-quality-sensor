# ESP32 Air Sensor (MQ + DHT) → Mosquitto Broker → Python Frontend — **Part 1 of 2**

> **Short:** ESP32 reads a DHT11 (temperature + humidity) and an MQ-series gas sensor, publishes JSON to a Mosquitto MQTT broker (I ran the broker on both Ubuntu and Arch Linux). A Python/Tkinter frontend subscribes, plots values, and can export CSV.
> This is **part 1** (repo layout, hardware, firmware & PlatformIO details, payload format, and important gotchas). I’ll deliver part 2 (broker setup, frontend, testing, troubleshooting, security, improvements, license & credits) in the next message.

---

# Repo layout (suggested)

```
README-part1.md
README-part2.md
esp/                      # PlatformIO project for the ESP firmware
frontend/                 # Python frontend + helper scripts
frontend/requirements.txt
frontend/setup_and_run.py # helper that creates venv and runs frontend
scripts/add_mqtt_device.sh # helper for Mosquitto user + ACL (optional)
LICENSE
```

Place the PlatformIO project under `esp/` and the frontend under `frontend/`. Split README into two files (or keep them as README.md with sections) — here they are split per your request.

---

# Hardware

* **ESP** — ESP32 (recommended). ESP8266 could work but requires code/pin changes.
* **DHT sensor** — DHT11 in the code; DHT22 recommended if you want better accuracy.
* **MQ-series gas sensor** — e.g., MQ-2 / MQ-135 (analog output).
* USB cable for power + serial.

**Wiring (basic)**

* `DHT` data → `DHTPIN` (GPIO2 by default in the sketch).
* `MQ` analog output → `MQPIN` (default in sketch was `3` — **change this** to an actual ADC-capable pin on ESP32).
* Power and GND — follow module voltage requirements. Many MQ modules use a 5V heater — read module docs. Use a level shifter or confirm module output suits 3.3V ADC inputs.

**Important pin note (ESP32)**
Do **not** use ADC2 pins when Wi-Fi is used — ADC2 is shared with Wi-Fi. Prefer ADC1 pins (GPIO32–GPIO39) for stable analog reads. Also avoid pins that affect flash/boot unless you know what you’re doing.

**Recommended example pins (ESP32)**

* `DHTPIN = 2` (GPIO2)
* `MQPIN = 34` (GPIO34, ADC1) — change the sketch accordingly.

---

# Firmware (ESP sketch) — PlatformIO project

This firmware is set up as a **PlatformIO** project. Use VSCode + PlatformIO extension or the `platformio` CLI.

## PlatformIO quick start

1. Install PlatformIO (VSCode + PlatformIO extension) or install the `platformio` CLI.
2. Open the `esp/` folder as a PlatformIO project (or run commands from the `esp/` folder).

Common commands (run from the project root or `esp/`):

```bash
# build
platformio run

# build + upload (explicit environment)
platformio run -e esp32dev -t upload

# serial monitor
platformio device monitor -b 115200
```

## Example `platformio.ini` (put in `esp/platformio.ini`)

```ini
[env:esp32dev]
platform = espressif32
board = esp32dev
framework = arduino
monitor_speed = 115200

lib_deps =
  adafruit/Adafruit Unified Sensor@^1.1.4
  adafruit/DHT sensor library@^1.4.3
  knolleary/PubSubClient@^2.8

; upload_port = /dev/ttyUSB0  ; set if you want to force a port
```

Notes:

* If a library name cannot be resolved by PlatformIO, use the Library Manager to add it or specify the numeric ID.
* Omit `upload_port` to let PlatformIO autodetect, or set it explicitly if multiple serial devices exist.

---

# What the sketch does

* Connects to Wi-Fi using `ssid` and `wifiPassword`.
* Connects to the MQTT broker at `mqttServer:mqttPort` with `mqttUser` / `mqttPassword`.
* Publishes JSON payloads to `mqttTopic` at `publishInterval`.
* Publishes an `"online"` retained status to `home/air/<device>/status` on successful connect.

**Published topic (example)**
`home/air/esp01/data` — change as needed.

---

# JSON payload format

Example payload the ESP publishes:

```json
{
  "temperature": 23.4,
  "humidity": 48.2,
  "gas_raw": 286
}
```

* `temperature`: °C (float with 1 decimal in the example sketch).
* `humidity`: % (float with 1 decimal).
* `gas_raw`: raw ADC integer (0–4095 on many ESP32 ADCs; depends on ADC resolution and voltage).

---

# Important code notes & gotchas

* **MQ analog pin:** The sample sketch used `MQPIN = 3` — that is not a good default for ESP32. Change `MQPIN` to an ADC1 pin (e.g., 34) to avoid serial/Wi-Fi conflicts.
* **Averaging:** The sketch averages 10 samples for `analogRead()` to stabilize the gas reading — keep or increase sample count for smoother values.
* **DHT failures:** `DHT.readTemperature()` and `DHT.readHumidity()` may return `NaN`. The sketch logs and skips bad readings. Avoid reading DHTs faster than once every 2 seconds.
* **publishInterval discrepancy:** In the code you pasted, `publishInterval = 10UL * 1000UL` (10 seconds) but a comment said 60s — confirm the interval you want and update the comment or value.
* **MQTT client IDs:** The sketch forms a client id using `millis()` which is okay for uniqueness during dev; for production, consider a deterministic unique ID per device (MAC-based or a flashed ID).
* **Retained status:** The sketch publishes a retained `online` message (`home/air/esp01/status`) — this is useful so new subscribers know the device last known state. If you prefer not to use retained messages, change the publish flag.
* **client.state():** When `client.connect()` fails, the sketch prints `client.state()` — use that return code to debug auth failures or protocol issues.
* **Power & noisy ADC:** MQ sensors (and their heaters) draw current and generate noise. Ensure stable power supply (capable of heater current) and consider filtering the analog input (hardware RC filter) or using shielded wiring for reliable readings.
* **ADC calibration:** Raw ADC values are not PPM. Convert to real gas concentrations via calibration curves if you need meaningful units.

# ESP32 Air Sensor (MQ + DHT) → Mosquitto Broker → Python Frontend — **Part 2 of 2** (Markdown)

> **This is Part 2** — continues from Part 1. Contains broker setup (Ubuntu/Arch), `add_mqtt_device.sh`, Python frontend install & run, testing, troubleshooting, security, improvements, license & contribution, and credits.

---

# MQTT broker setup (Mosquitto)

> Replace example IPs / usernames / passwords with secure values for production.

## Ubuntu (apt)

```bash
sudo apt update
sudo apt install -y mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto
```

Create a password file and add a user (example username `esp01`):

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp01
# enter password when prompted (example: pass)
```

Create `/etc/mosquitto/conf.d/esp.conf`:

```
listener 1883 0.0.0.0
allow_anonymous false
password_file /etc/mosquitto/passwd
acl_file /etc/mosquitto/acl
```

Restart and open firewall (if using `ufw`):

```bash
sudo systemctl restart mosquitto
sudo ufw allow 1883/tcp
```

Test from a client machine:

```bash
mosquitto_sub -h <broker-ip> -t home/air/esp01/data -u esp01 -P pass -v
```

## Arch Linux (pacman)

```bash
sudo pacman -Syu
sudo pacman -S mosquitto mosquitto-clients
sudo systemctl enable --now mosquitto.service
```

Add passwd and ACL as above (`/etc/mosquitto/passwd`, `/etc/mosquitto/acl`), then restart:

```bash
sudo mosquitto_passwd -c /etc/mosquitto/passwd esp01
sudo systemctl restart mosquitto
# If using firewalld:
sudo firewall-cmd --add-port=1883/tcp --permanent
sudo firewall-cmd --reload
```

### Logs & debugging

```bash
sudo journalctl -u mosquitto -f
```

Check that Mosquitto is listening:

```bash
ss -ltnp | grep 1883
```

---

# Helper script: `add_mqtt_device.sh`

This helper adds a Mosquitto user and ACL entry for `home/air/<device>/#`.

**Script (naive append version)**

```bash
#!/usr/bin/env bash
set -euo pipefail

if [ $# -ne 1 ]; then
  echo "Usage: $0 <device_id>"
  exit 2
fi

DEVICE="$1"
PASSWD_FILE="/etc/mosquitto/passwd"
ACL_FILE="/etc/mosquitto/acl"

# Add user (interactive password prompt)
sudo mosquitto_passwd "$PASSWD_FILE" "$DEVICE"

# Append ACL block
sudo bash -c "cat >> $ACL_FILE <<EOF

user $DEVICE
topic write home/air/$DEVICE/#
topic read  home/air/$DEVICE/#
EOF"

# Fix ownership & perms
sudo chown root:mosquitto $PASSWD_FILE $ACL_FILE
sudo chmod 640 $PASSWD_FILE $ACL_FILE

# Restart mosquitto
sudo systemctl restart mosquitto
echo "Added $DEVICE and ACL updated. Mosquitto restarted."
```

**Usage**

```bash
chmod +x add_mqtt_device.sh
./add_mqtt_device.sh esp01
```

**Idempotency note**
The script appends ACL blocks naively. If you run it twice for the same device you'll create duplicate ACL entries. If you want an idempotent variant, replace the append step with logic that checks for an existing `user <device>` line and edits or skips if present. Example (simple guard):

```bash
if sudo grep -q "^user $DEVICE\$" "$ACL_FILE"; then
  echo "ACL entry already exists for $DEVICE, skipping ACL append"
else
  # append block ...
fi
```

---

# Frontend (Python / Tkinter)

The frontend (in `frontend/`) subscribes to the MQTT topic, shows current values, plots recent values and exports CSV.

## Requirements

* Python 3.8+
* `paho-mqtt`
* `matplotlib`
* `tkinter` (system package, usually included with Python; on some Linux distros install `python3-tk`)

`frontend/requirements.txt`:

```
paho-mqtt
matplotlib
```

## Install & Run — Recommended (venv)

```bash
cd frontend
python3 -m venv .venv
source .venv/bin/activate
pip install --upgrade pip
pip install -r requirements.txt
python iot_frontend.py
```

Or run your helper script (example `setup_and_run.py`):

```bash
python3 setup_and_run.py
```

## Using the GUI

1. Enter broker host (IP), port (1883), topic (e.g. `home/air/esp01/data`), username (`esp01`) and password.
2. Click **Connect**.
3. The window displays:

   * Current Temperature, Humidity, Gas (raw)
   * Scrolling log with timestamps
   * Plot of recent values (Temp / Humidity / Gas)
4. Use **Export CSV** to save the raw log (timestamp, topic, payload, parsed fields).

---

# Testing & verification

1. Start Mosquitto on the broker machine:

   ```bash
   sudo systemctl start mosquitto
   ```

2. Ensure the broker is reachable from the ESP's network (ping the broker IP from the same subnet).

3. Update the ESP sketch:

   * Set `mqttServer` to broker IP (e.g., `192.168.100.122`)
   * Set `mqttUser` / `mqttPassword`
   * Ensure `MQPIN` is an ADC-capable pin (e.g., 34 on ESP32)

4. Upload firmware via PlatformIO and open serial monitor:

   ```bash
   platformio device monitor -b 115200
   ```

   Check output: Wi-Fi connected, IP assigned, MQTT connected, and periodic publish logs.

5. Subscribe manually from another machine:

   ```bash
   mosquitto_sub -h 192.168.100.122 -t "home/air/esp01/data" -u esp01 -P pass -v
   ```

   You should see JSON payloads like:

   ```
   home/air/esp01/data {"temperature":23.4,"humidity":48.2,"gas_raw":286}
   ```

6. Connect the GUI to the broker and confirm values appear and plot updates.

---

# Troubleshooting — common problems & fixes

* **WiFi not connecting**

  * Confirm SSID/password.
  * Check signal and power (weak USB power causes instability).
  * See serial logs for repeated reconnects.

* **MQTT connect fails (`client.state()` non-zero)**

  * `4` or `5` often indicate auth issues — verify username/password and ACLs.
  * Confirm broker IP & port, and that broker is listening on 0.0.0.0 if connecting remotely.
  * Check `journalctl -u mosquitto -f` for server-side errors.

* **MQ sensor reading 0 or noisy**

  * Use ADC1 pins on ESP32 (GPIO32–39). Avoid ADC2.
  * Ensure stable power (MQ heater draws current).
  * Add small RC filter on analog line or increase sample averaging.

* **DHT returns `NaN`**

  * DHT modules are sensitive; check wiring, pull-up, and minimum read interval (>2s).
  * Try replacing sensor if old.

* **Frontend cannot connect**

  * Check host/port, username/password.
  * Ensure `python3-tk` installed (on Linux: `sudo apt install python3-tk`).
  * If GUI shows `CONNECT FAILED rc=<n>`, inspect what the `mosquitto` logs say.

---

# Security & production notes

* **Do not** use plaintext credentials for public or internet-exposed brokers.
* Consider TLS (MQTT over TLS on 8883) and certificate-based auth.
* Use strong, unique passwords per device.
* Lock down the broker firewall to allowed IPs only.
* Rotate credentials and monitor logs for suspicious activity.
* For large fleets, use a more robust identity and provisioning process (e.g., certificate-per-device).

---

# Improvements / TODOs

* Add MQTT over TLS (broker and ESP). PlatformIO/Esp32 support `WiFiClientSecure` + PubSubClient or use `MQTTClient` supporting TLS.
* Convert `gas_raw` ADC to PPM via calibration curves for targeted gases.
* Add OTA updates for ESP (PlatformIO + ArduinoOTA or native ESP OTA).
* Use persistent storage + time-series DB (InfluxDB) + Grafana dashboards.
* Harden `add_mqtt_device.sh` to be idempotent and support bulk device registration.
* Add unit tests / linter (shellcheck for scripts, flake8 for Python).
* Add CI to build PlatformIO firmware automatically (example: GitHub Actions).

---

# License

Pick a license. For small projects MIT is recommended.

**MIT (example)** — create `LICENSE` file with MIT text.

---

# Contribution

* PRs and issues welcome.
* When opening an issue include: serial logs from the ESP, mosquitto logs (`journalctl -u mosquitto`), frontend logs and screenshots if relevant.
* For code changes, follow style: PlatformIO project under `esp/`, Python under `frontend/`.

---

# Credits

* DHT library (Adafruit / Arduino DHT)
* PubSubClient (by Nick O'Leary / knolleary)
* paho-mqtt (Python)
* matplotlib (Python plotting)
* Tkinter (GUI)

---

# Quick reference (commands)

Build & upload via PlatformIO:

```bash
cd esp
platformio run -e esp32dev -t upload
platformio device monitor -b 115200
```

Start frontend (venv):

```bash
cd frontend
python3 -m venv .venv
source .venv/bin/activate
pip install -r requirements.txt
python iot_frontend.py
```

Add device helper:

```bash
chmod +x scripts/add_mqtt_device.sh
./scripts/add_mqtt_device.sh esp01
```

Test subscribe (client):

```bash
mosquitto_sub -h <broker-ip> -t "home/air/esp01/#" -u esp01 -P <password> -v
```
