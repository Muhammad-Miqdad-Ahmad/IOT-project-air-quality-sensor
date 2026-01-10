#!/usr/bin/env python3
"""
IoT Air Sensor Dashboard
- MQTT subscriber
- Keyboard command publisher
- 3 separate live graphs
- SQLite database storage
"""

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import time, json, queue, csv, re, math
from collections import deque
import socket, subprocess, struct, fcntl
import sqlite3

import paho.mqtt.client as mqtt
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg

# ---------------- CONFIG ----------------
DEFAULT_BROKER = "172.16.18.157"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "home/air/esp01/data"
DEFAULT_CMD_TOPIC = "home/air/esp01/cmd"
DEFAULT_USER = "esp01"
DEFAULT_PASS = "pass"

MAX_POINTS = 600
POLL_MS = 200

TEMP_STEP = 0.5
HUM_STEP = 1.0
# --------------------------------------


# -------- IP DETECTION HELPERS --------
def _get_ip_ioctl(ifname):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    try:
        packed = struct.pack('256s', ifname[:15].encode())
        res = fcntl.ioctl(s.fileno(), 0x8915, packed)
        return socket.inet_ntoa(res[20:24])
    except:
        return None
    finally:
        s.close()


def get_all_ipv4_addresses():
    try:
        p = subprocess.run(
            ['ip', '-4', '-o', 'addr', 'show'],
            capture_output=True, text=True, timeout=1
        )
        return [(i, ip) for i, ip in
                re.findall(r'\d+:\s+(\S+).*inet (\d+\.\d+\.\d+\.\d+)/', p.stdout)
                if i != 'lo']
    except:
        return []


def detect_local_ip_dynamic(preferred='wlan0'):
    ip = _get_ip_ioctl(preferred)
    if ip:
        return ip
    entries = get_all_ipv4_addresses()
    return entries[0][1] if entries else DEFAULT_BROKER
# --------------------------------------


class IoTFrontend:
    def __init__(self, root):
        self.root = root
        root.title("Air Sensor Dashboard")

        self.msg_q = queue.Queue()
        self.mqtt_client = None
        self.connected = False

        self.temp_buf = deque(maxlen=MAX_POINTS)
        self.hum_buf = deque(maxlen=MAX_POINTS)
        self.gas_buf = deque(maxlen=MAX_POINTS)
        self.raw_log = []

        # ---------- DATABASE ----------
        self.db = sqlite3.connect("sensor_data.db", check_same_thread=False)
        self.db_cursor = self.db.cursor()
        self.db_cursor.execute("""
        CREATE TABLE IF NOT EXISTS readings (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT,
            topic TEXT,
            temperature REAL,
            humidity REAL,
            gas_raw REAL,
            payload TEXT
        )
        """)
        self.db.commit()

        self._build_ui()
        self._build_plot()

        self.root.bind("<Key>", self._on_keypress)
        self.root.after(POLL_MS, self._poll_queue)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------- UI ----------------
    def _build_ui(self):
        frame = ttk.Frame(self.root, padding=8)
        frame.grid(sticky="nsew")

        # ---- Connection ----
        conn = ttk.LabelFrame(frame, text="MQTT Connection")
        conn.grid(row=0, column=0, sticky="ew")

        ttk.Label(conn, text="Host").grid(row=0, column=0)
        ips = [ip for _, ip in get_all_ipv4_addresses()]
        self.host_e = ttk.Combobox(conn, values=ips, width=18)
        self.host_e.set(detect_local_ip_dynamic())
        self.host_e.grid(row=0, column=1)

        ttk.Label(conn, text="Port").grid(row=0, column=2)
        self.port_e = ttk.Entry(conn, width=6)
        self.port_e.insert(0, DEFAULT_PORT)
        self.port_e.grid(row=0, column=3)

        ttk.Label(conn, text="Topic").grid(row=1, column=0)
        self.topic_e = ttk.Entry(conn, width=40)
        self.topic_e.insert(0, DEFAULT_TOPIC)
        self.topic_e.grid(row=1, column=1, columnspan=3, sticky="ew")

        ttk.Label(conn, text="User").grid(row=2, column=0)
        self.user_e = ttk.Entry(conn)
        self.user_e.insert(0, DEFAULT_USER)
        self.user_e.grid(row=2, column=1)

        ttk.Label(conn, text="Pass").grid(row=2, column=2)
        self.pass_e = ttk.Entry(conn, show="*")
        self.pass_e.insert(0, DEFAULT_PASS)
        self.pass_e.grid(row=2, column=3)

        self.status = ttk.Label(conn, text="Disconnected", foreground="red")
        self.status.grid(row=0, column=4, rowspan=2, padx=10)

        ttk.Button(conn, text="Connect", command=self.toggle_connect).grid(row=3, column=0)
        ttk.Button(conn, text="Export CSV", command=self.export_csv).grid(row=3, column=1)
        ttk.Button(conn, text="Clear", command=self.clear_data).grid(row=3, column=2)

        # ---- Readings ----
        read = ttk.LabelFrame(frame, text="Current Readings")
        read.grid(row=1, column=0, sticky="ew")

        self.temp_val = ttk.Label(read, text="—", font=("Arial", 22))
        self.hum_val = ttk.Label(read, text="—", font=("Arial", 22))
        self.gas_val = ttk.Label(read, text="—", font=("Arial", 22))

        ttk.Label(read, text="Temp (°C)").grid(row=0, column=0)
        ttk.Label(read, text="Humidity (%)").grid(row=1, column=0)
        ttk.Label(read, text="Gas").grid(row=2, column=0)

        self.temp_val.grid(row=0, column=1)
        self.hum_val.grid(row=1, column=1)
        self.gas_val.grid(row=2, column=1)

        # ---- Log ----
        self.log = tk.Text(frame, height=8, state="disabled")
        self.log.grid(row=2, column=0, sticky="ew")

    # ---------------- PLOT ----------------
    def _build_plot(self):
        self.fig = Figure(figsize=(9, 6), dpi=100)

        self.ax_t = self.fig.add_subplot(311)
        self.ax_h = self.fig.add_subplot(312)
        self.ax_g = self.fig.add_subplot(313)

        self.canvas = FigureCanvasTkAgg(self.fig, self.root)
        self.canvas.get_tk_widget().grid(row=3, column=0, sticky="nsew")

    def _update_plot(self):
        now = time.time()

        def xy(buf):
            return [now - t for t, _ in buf], [v for _, v in buf]

        for ax in (self.ax_t, self.ax_h, self.ax_g):
            ax.cla()
            ax.invert_xaxis()
            ax.grid(True, alpha=0.3)

        if self.temp_buf:
            x, y = xy(self.temp_buf)
            self.ax_t.plot(x, y)
        self.ax_t.set_title("Temperature (°C)")

        if self.hum_buf:
            x, y = xy(self.hum_buf)
            self.ax_h.plot(x, y)
        self.ax_h.set_title("Humidity (%)")

        if self.gas_buf:
            x, y = xy(self.gas_buf)
            self.ax_g.plot(x, y)
        self.ax_g.set_title("Gas Sensor")

        self.fig.tight_layout()
        self.canvas.draw_idle()

    # ---------------- MQTT ----------------
    def toggle_connect(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        self.mqtt_client = mqtt.Client()
        self.mqtt_client.username_pw_set(self.user_e.get(), self.pass_e.get())
        self.mqtt_client.on_connect = self._on_connect
        self.mqtt_client.on_message = self._on_message
        self.mqtt_client.connect(self.host_e.get(), int(self.port_e.get()), 60)
        self.mqtt_client.loop_start()

    def _disconnect(self):
        self.mqtt_client.loop_stop()
        self.mqtt_client.disconnect()
        self.connected = False
        self.status.config(text="Disconnected", foreground="red")

    def _on_connect(self, client, userdata, flags, rc):
        client.subscribe(self.topic_e.get())
        self.connected = True
        self.status.config(text="Connected", foreground="green")

    def _on_message(self, client, userdata, msg):
        self.msg_q.put((time.time(), msg.topic, msg.payload.decode()))

    # ---------------- DATA ----------------
    def _poll_queue(self):
        updated = False
        while not self.msg_q.empty():
            ts, topic, payload = self.msg_q.get()
            self._log(topic, payload)

            try:
                obj = json.loads(payload)
            except:
                continue

            t = obj.get("temperature")
            h = obj.get("humidity")
            g = obj.get("gas_raw")

            if isinstance(t, (int, float)):
                self.temp_buf.append((ts, t))
                self.temp_val.config(text=f"{t:.1f}")
            if isinstance(h, (int, float)):
                self.hum_buf.append((ts, h))
                self.hum_val.config(text=f"{h:.1f}")
            if isinstance(g, (int, float)):
                self.gas_buf.append((ts, g))
                self.gas_val.config(text=str(int(g)))

            self.db_cursor.execute(
                "INSERT INTO readings VALUES (NULL,?,?,?,?,?,?)",
                (time.strftime("%F %T", time.localtime(ts)), topic, t, h, g, payload)
            )
            self.db.commit()

            updated = True

        if updated:
            self._update_plot()

        self.root.after(POLL_MS, self._poll_queue)

    # ---------------- KEYS ----------------
    def _on_keypress(self, e):
        if not self.connected:
            return

        if e.char == 't':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"INC:{TEMP_STEP}")
        elif e.char == 'T':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"DEC:{TEMP_STEP}")
        elif e.char == 'h':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"HUM_INC:{HUM_STEP}")
        elif e.char == 'H':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"HUM_DEC:{HUM_STEP}")

    # ---------------- UTIL ----------------
    def export_csv(self):
        fn = filedialog.asksaveasfilename(defaultextension=".csv")
        if not fn:
            return
        rows = self.db_cursor.execute("SELECT * FROM readings").fetchall()
        with open(fn, "w", newline="") as f:
            csv.writer(f).writerows(rows)

    def clear_data(self):
        self.temp_buf.clear()
        self.hum_buf.clear()
        self.gas_buf.clear()
        self._update_plot()

    def _log(self, topic, msg):
        self.log.config(state="normal")
        self.log.insert("end", f"[{topic}] {msg}\n")
        self.log.yview_moveto(1)
        self.log.config(state="disabled")

    def _on_close(self):
        try:
            self.db.close()
            self.mqtt_client.loop_stop()
            self.mqtt_client.disconnect()
        except:
            pass
        self.root.destroy()


# ---------------- RUN ----------------
if __name__ == "__main__":
    root = tk.Tk()
    root.geometry("950x750")
    app = IoTFrontend(root)
    root.mainloop()
