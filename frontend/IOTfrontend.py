#!/usr/bin/env python3
"""
IoT Air Sensor Dashboard
Layout:
 - Top: MQTT connection settings
 - Middle: LEFT = current readings; RIGHT = terminal/log (short)
 - Bottom: two side-by-side graphs (left: Temp + Humidity, right: Gas)
Includes: MQTT subscribe, keyboard command publishes, SQLite persistence.
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


# -------- IP DETECTION HELPERS (linux-friendly) --------
def _get_ip_ioctl(ifname):
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        ifname_b = ifname[:15].encode('utf-8')
        packed = struct.pack('256s', ifname_b)
        res = fcntl.ioctl(s.fileno(), 0x8915, packed)  # SIOCGIFADDR
        ip = socket.inet_ntoa(res[20:24])
        s.close()
        return ip
    except Exception:
        try:
            s.close()
        except Exception:
            pass
        return None


def get_all_ipv4_addresses():
    try:
        p = subprocess.run(['ip', '-4', '-o', 'addr', 'show'],
                           capture_output=True, text=True, check=False, timeout=1.0)
        out = p.stdout or ""
        entries = re.findall(r'^\d+:\s+([^:]+)\s+inet\s+(\d+\.\d+\.\d+\.\d+)/', out, flags=re.M)
        return [(iface, ip) for iface, ip in entries if iface != 'lo']
    except Exception:
        return []


def detect_local_ip_dynamic(preferred='wlan0'):
    ip = _get_ip_ioctl(preferred)
    if ip:
        return ip
    entries = get_all_ipv4_addresses()
    if entries:
        # return preferred if present
        for iface, addr in entries:
            if iface == preferred:
                return addr
        return entries[0][1]
    # fallback: try outbound socket
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        s.connect(('8.8.8.8', 80))
        ip = s.getsockname()[0]
        s.close()
        return ip
    except Exception:
        return DEFAULT_BROKER
# --------------------------------------

def sanitize_table_name(name):
    # allow only letters, numbers, underscore
    return re.sub(r'\W+', '_', name)


class IoTFrontend:
    def __init__(self, root):
        self.root = root
        root.title("Air Sensor Dashboard")

        # app state
        self.msg_q = queue.Queue()
        self.mqtt_client = None
        self.connected = False

        self.temp_buf = deque(maxlen=MAX_POINTS)
        self.hum_buf = deque(maxlen=MAX_POINTS)
        self.gas_buf = deque(maxlen=MAX_POINTS)
        self.raw_log = []

        # DB (SQLite)
        self.db = sqlite3.connect("sensor_data.db", check_same_thread=False)
        self.db_cursor = self.db.cursor()

        raw_user = DEFAULT_USER
        self.table_name = sanitize_table_name(raw_user)

        self.db_cursor.execute(f"""
        CREATE TABLE IF NOT EXISTS {self.table_name} (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT NOT NULL,
            topic TEXT NOT NULL,
            temperature REAL,
            humidity REAL,
            gas REAL,
            payload TEXT
        )
        """)
        self.db.commit()


        # build UI and plots
        self._build_ui()
        self._build_plot()

        # bindings, loop
        self.root.bind("<Key>", self._on_keypress)
        self.root.after(POLL_MS, self._poll_queue)
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    # ---------------- UI ----------------
    def _build_ui(self):
        # main container
        self.main = ttk.Frame(self.root, padding=8)
        self.main.grid(sticky="nsew")
        # overall layout: 3 rows (top: controls, middle: data+log, bottom: plots)
        # make sure plots (row 2) get the expansion priority; middle is fixed (won't expand)
        self.root.rowconfigure(0, weight=0)
        self.root.rowconfigure(1, weight=0)  # middle row fixed (prevents terminal pushing plots down)
        self.root.rowconfigure(2, weight=1)  # plots get remaining vertical space
        self.root.columnconfigure(0, weight=1)

        # ---------- Top: Connection controls ----------
        conn = ttk.LabelFrame(self.main, text="MQTT Connection / Settings")
        conn.grid(row=0, column=0, sticky="ew", pady=(0, 8))
        conn.columnconfigure(1, weight=1)
        conn.columnconfigure(5, weight=0)

        ttk.Label(conn, text="Host").grid(row=0, column=0, sticky="w", padx=(4,4), pady=4)
        ips = [ip for _, ip in get_all_ipv4_addresses()]
        self.host_e = ttk.Combobox(conn, values=ips, width=20)
        self.host_e.set(detect_local_ip_dynamic())
        self.host_e.grid(row=0, column=1, sticky="w", padx=(0,8))

        ttk.Label(conn, text="Port").grid(row=0, column=2, sticky="w", padx=(4,4))
        self.port_e = ttk.Entry(conn, width=6)
        self.port_e.insert(0, str(DEFAULT_PORT))
        self.port_e.grid(row=0, column=3, sticky="w", padx=(0,8))

        ttk.Label(conn, text="Topic").grid(row=1, column=0, sticky="w", padx=(4,4), pady=(0,4))
        self.topic_e = ttk.Entry(conn)
        self.topic_e.insert(0, DEFAULT_TOPIC)
        self.topic_e.grid(row=1, column=1, columnspan=3, sticky="ew", padx=(0,8), pady=(0,4))

        ttk.Label(conn, text="User").grid(row=0, column=4, sticky="w", padx=(4,4))
        self.user_e = ttk.Entry(conn, width=12)
        self.user_e.insert(0, DEFAULT_USER)
        self.user_e.grid(row=0, column=5, sticky="w", padx=(0,8))

        ttk.Label(conn, text="Pass").grid(row=1, column=4, sticky="w", padx=(4,4))
        self.pass_e = ttk.Entry(conn, show="*", width=12)
        self.pass_e.insert(0, DEFAULT_PASS)
        self.pass_e.grid(row=1, column=5, sticky="w", padx=(0,8))

        self.status_lbl = ttk.Label(conn, text="Disconnected", foreground="red")
        self.status_lbl.grid(row=0, column=6, rowspan=2, padx=(8,4))

        btn_frame = ttk.Frame(conn)
        btn_frame.grid(row=2, column=0, columnspan=7, sticky="w", pady=(6,2))
        ttk.Button(btn_frame, text="Connect", command=self.toggle_connect).pack(side="left")
        ttk.Button(btn_frame, text="Export CSV", command=self.export_csv).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Clear Data", command=self.clear_data).pack(side="left")

        # ---------- Middle row: left = data display, right = terminal/log ----------
        mid = ttk.Frame(self.main)
        mid.grid(row=1, column=0, sticky="nsew", pady=(0,6))
        # set mid not to expand vertically (prevents terminal from pushing plots)
        mid.columnconfigure(0, weight=1)
        mid.columnconfigure(1, weight=1)
        mid.rowconfigure(0, weight=0)

        # Left: Current Readings (compact)
        data_frame = ttk.LabelFrame(mid, text="Current Readings")
        data_frame.grid(row=0, column=0, sticky="nsew", padx=(0,5))
        data_frame.columnconfigure(1, weight=1)

        font = 12
        ttk.Label(data_frame, text="Temperature (°C):").grid(row=0, column=0, sticky="w", padx=6, pady=6)
        self.temp_val = ttk.Label(data_frame, text="—", font=("Segoe UI", font, "bold"))
        self.temp_val.grid(row=0, column=1, sticky="w", padx=7, pady=6)

        ttk.Label(data_frame, text="Humidity (%):").grid(row=1, column=0, sticky="w", padx=6, pady=6)
        self.hum_val = ttk.Label(data_frame, text="—", font=("Segoe UI", font, "bold"))
        self.hum_val.grid(row=1, column=1, sticky="w", padx=7, pady=6)

        ttk.Label(data_frame, text="Gas (raw):").grid(row=2, column=0, sticky="w", padx=6, pady=6)
        self.gas_val = ttk.Label(data_frame, text="—", font=("Segoe UI", font, "bold"))
        self.gas_val.grid(row=2, column=1, sticky="w", padx=7, pady=6)

        # Right: Terminal / Log (short height so plots remain visible)
        log_frame = ttk.LabelFrame(mid, text="Terminal / Log (short)")
        log_frame.grid(row=0, column=1, sticky="nsew")
        log_frame.rowconfigure(0, weight=0)  # keep log compact
        log_frame.columnconfigure(0, weight=1)

        # IMPORTANT: small height (6 lines) so it doesn't push plots off screen
        self.log = tk.Text(log_frame, height=5, state="disabled", font=("Consolas", 10))
        self.log.grid(row=0, column=0, sticky="nsew")
        log_scroll = ttk.Scrollbar(log_frame, orient="vertical", command=self.log.yview)
        log_scroll.grid(row=0, column=1, sticky="ns")
        self.log['yscrollcommand'] = log_scroll.set

    # ---------------- PLOT (bottom, two side-by-side) ----------------
    def _build_plot(self):
        plots_frame = ttk.Frame(self.main)
        plots_frame.grid(row=2, column=0, sticky="nsew")
        plots_frame.rowconfigure(0, weight=1)
        plots_frame.columnconfigure(0, weight=1)
        plots_frame.columnconfigure(1, weight=1)

        # Larger figure to use more vertical space but it will be resized to available area
        self.fig = Figure(figsize=(13.3, 4.5), dpi=100)
        # left: temp & humidity. right: gas
        self.ax_left = self.fig.add_subplot(121)
        self.ax_hum = self.ax_left.twinx()  # twin axis for humidity
        self.ax_right = self.fig.add_subplot(122)

        self.canvas = FigureCanvasTkAgg(self.fig, master=plots_frame)
        self.canvas_widget = self.canvas.get_tk_widget()
        self.canvas_widget.grid(row=0, column=0, columnspan=2, sticky="nsew", padx=4, pady=4)

    def _update_plot(self):
        now = time.time()

        def series_to_xy(buf):
            if not buf:
                return [], []
            xs = [now - t for (t, v) in buf]
            ys = [v for (t, v) in buf]
            return xs, ys

        tx, ty = series_to_xy(self.temp_buf)
        hx, hy = series_to_xy(self.hum_buf)
        gx, gy = series_to_xy(self.gas_buf)

        # clear axes
        self.ax_left.cla()
        self.ax_hum.cla()
        self.ax_right.cla()

        # left: temperature (red), humidity (solid blue) on twin y
        
        self.ax_left.grid(True, alpha=0.25)
        if ty:
            self.ax_left.plot(tx, ty, color='red', linewidth=2.5, label="Temp (°C)")
        self.ax_left.set_ylabel("°C")
        self.ax_left.set_title("Temperature & Humidity")

        
        if hy:
            self.ax_hum.plot(hx, hy, color='blue', linewidth=2.2, label="Humidity (%)")
        self.ax_hum.set_ylabel("%")

        # combined legend
        lines1, labels1 = self.ax_left.get_legend_handles_labels()
        lines2, labels2 = self.ax_hum.get_legend_handles_labels()
        if lines1 or lines2:
            self.ax_left.legend(lines1 + lines2, labels1 + labels2, loc="upper right")

        # right: gas (grey)
        
        self.ax_right.grid(True, alpha=0.25)
        if gy:
            self.ax_right.plot(gx, gy, color='grey', linewidth=2.0, label="Gas (raw)")
        self.ax_right.set_title("Gas (raw)")
        self.ax_right.set_xlabel("Seconds ago")
        self.ax_right.set_ylabel("Value")
        if gy:
            self.ax_right.legend(loc="upper right")

        self.fig.subplots_adjust(left=0.06, right=0.97, top=0.92, bottom=0.12, wspace=0.25)

        self.canvas.draw_idle()

    # ---------------- MQTT ----------------
    def toggle_connect(self):
        if self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        host = self.host_e.get().strip()
        try:
            port = int(self.port_e.get().strip())
        except Exception:
            messagebox.showerror("Bad port", "Port must be an integer")
            return
        topic = self.topic_e.get().strip()
        if not host or not topic:
            messagebox.showerror("Missing settings", "Host and Topic required")
            return

        try:
            # clean up existing client if present
            if self.mqtt_client:
                try:
                    self.mqtt_client.loop_stop()
                    self.mqtt_client.disconnect()
                except:
                    pass
                self.mqtt_client = None
                self.connected = False

            client_id = f"iot-frontend-{int(time.time())}"
            client = mqtt.Client(client_id=client_id, clean_session=True)
            user = self.user_e.get().strip()
            pwd = self.pass_e.get()
            if user:
                client.username_pw_set(user, pwd)

            client.on_connect = self._on_connect
            client.on_disconnect = self._on_disconnect
            client.on_message = self._on_message

            client.connect(host, port, keepalive=60)
            client.loop_start()

            self.mqtt_client = client
            self.status_lbl.config(text="Connecting...", foreground="orange")
            self._log("SYS", f"Connect issued to {host}:{port}")
        except Exception as e:
            messagebox.showerror("Connect failed", str(e))
            self._log("SYS", f"Connect failed: {e}")

    def _disconnect(self):
        try:
            if self.mqtt_client:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
        except:
            pass
        self.mqtt_client = None
        self.connected = False
        self.status_lbl.config(text="Disconnected", foreground="red")
        self._log("SYS", "Disconnected")

    def _on_connect(self, client, userdata, flags, rc):
        try:
            client.subscribe(self.topic_e.get().strip())
            self._log("SYS", f"Subscribed to {self.topic_e.get().strip()}")
            self.connected = True
            self.status_lbl.config(text="Connected", foreground="green")
        except Exception as e:
            self._log("SYS", f"Subscribe failed: {e}")

    def _on_disconnect(self, client, userdata, rc):
        self._log("SYS", "MQTT disconnected")
        self.connected = False
        self.status_lbl.config(text="Disconnected", foreground="red")

    def _on_message(self, client, userdata, msg):
        try:
            payload = msg.payload.decode("utf-8", errors="replace")
        except Exception:
            payload = str(msg.payload)
        self.msg_q.put((time.time(), msg.topic, payload))

    # ---------------- DATA POLLING & DB ----------------
    def _poll_queue(self):
        updated = False
        while not self.msg_q.empty():
            ts, topic, payload = self.msg_q.get()
            self._log(topic, payload)

            obj = None
            try:
                obj = json.loads(payload)
            except Exception:
                # skip non-json payloads for plotting/storing
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
                self.gas_val.config(text=str(int(round(g))))

            # self.last_update.config(text=time.strftime("%F %T", time.localtime(ts)))

            # write to DB
            try:
                self.db_cursor.execute(
                    f"""
                    INSERT INTO {self.table_name}
                    (timestamp, topic, temperature, humidity, gas, payload)
                    VALUES (?, ?, ?, ?, ?, ?)
                    """,
                    (
                        time.strftime("%F %T", time.localtime(ts)),
                        topic,
                        t,
                        h,
                        g,
                        payload
                    )
                )

                self.db.commit()
            except Exception as e:
                self._log("DB", f"DB write error: {e}")

            updated = True

        if updated:
            self._update_plot()

        self.root.after(POLL_MS, self._poll_queue)

    # ---------------- KEYS ----------------
    def _on_keypress(self, e):
        if not self.connected or not self.mqtt_client:
            return
        ch = getattr(e, 'char', '')
        if ch == 't':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"INC:{TEMP_STEP}")
        elif ch == 'T':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"DEC:{TEMP_STEP}")
        elif ch == 'h':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"HUM_INC:{HUM_STEP}")
        elif ch == 'H':
            self.mqtt_client.publish(DEFAULT_CMD_TOPIC, f"HUM_DEC:{HUM_STEP}")

    # ---------------- UTIL ----------------
    def export_csv(self):
        fn = filedialog.asksaveasfilename(defaultextension=".csv")
        if not fn:
            return
        try:
            rows = self.db_cursor.execute(
                f"SELECT timestamp, topic, temperature, humidity, gas, payload FROM {self.table_name}"
            ).fetchall()
            with open(fn, "w", newline="") as f:
                w = csv.writer(f)
                w.writerow(["timestamp", "topic", "temperature", "humidity", "gas_raw", "payload"])
                w.writerows(rows)
            messagebox.showinfo("Exported", f"Saved to {fn}")
        except Exception as e:
            messagebox.showerror("Export failed", str(e))

    def clear_data(self):
        self.temp_buf.clear()
        self.hum_buf.clear()
        self.gas_buf.clear()
        self._update_plot()

    def _log(self, topic, msg):
        timestr = time.strftime("%H:%M:%S", time.localtime(time.time()))
        line = f"[{timestr}] {topic}: {msg}\n"
        try:
            self.log.configure(state="normal")
            self.log.insert("end", line)
            # keep log short: ensure it doesn't grow visually beyond the allocated small height
            self.log.yview_moveto(1.0)
            self.log.configure(state="disabled")
        except Exception:
            pass

    def _on_close(self):
        try:
            self.db.close()
        except:
            pass
        try:
            if self.mqtt_client:
                self.mqtt_client.loop_stop()
                self.mqtt_client.disconnect()
        except:
            pass
        self.root.destroy()


# ---------------- RUN ----------------
if __name__ == "__main__":
    root = tk.Tk()
    # start with a sensible window size that fits most laptop screens
    root.geometry("1150x700")
    app = IoTFrontend(root)
    root.mainloop()
