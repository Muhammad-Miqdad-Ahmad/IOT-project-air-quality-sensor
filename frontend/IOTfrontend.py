# iot_frontend.py
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import time, json, queue, threading
from collections import deque
import paho.mqtt.client as mqtt
from matplotlib.figure import Figure
from matplotlib.backends.backend_tkagg import FigureCanvasTkAgg
import csv

# ---------- CONFIG DEFAULTS ----------
DEFAULT_BROKER = "172.16.18.157"
DEFAULT_PORT = 1883
DEFAULT_TOPIC = "home/air/esp01/data"
DEFAULT_USER = "esp01"
DEFAULT_PASS = "pass"
MAX_POINTS = 600   # keep last N points for plotting (~600)
POLL_MS = 200      # GUI poll interval
# --------------------------------------

class IoTFrontend:
    def __init__(self, root):
        self.root = root
        root.title("Air Sensor Dashboard")
        self.msg_q = queue.Queue()
        self.mqtt_client = None
        self.connected = False

        # data buffers: deque of (ts, value)
        self.temp_buf = deque(maxlen=MAX_POINTS)
        self.hum_buf = deque(maxlen=MAX_POINTS)
        self.gas_buf = deque(maxlen=MAX_POINTS)
        self.raw_log = []  # list of (ts, topic, payload_str)

        self._build_ui()
        self._build_plot()

        self.root.protocol("WM_DELETE_WINDOW", self._on_close)
        self.root.after(POLL_MS, self._poll_queue)

    def _build_ui(self):
        frm = ttk.Frame(self.root, padding=8)
        frm.grid(sticky="nsew")
        self.root.columnconfigure(0, weight=1)
        self.root.rowconfigure(0, weight=1)

        # Connection frame
        conn = ttk.LabelFrame(frm, text="Broker / Subscription")
        conn.grid(row=0, column=0, sticky="ew", padx=4, pady=4)
        conn.columnconfigure(1, weight=1)

        ttk.Label(conn, text="Host:").grid(row=0, column=0, sticky="w")
        self.host_e = ttk.Entry(conn)
        self.host_e.insert(0, DEFAULT_BROKER)
        self.host_e.grid(row=0, column=1, sticky="ew")

        ttk.Label(conn, text="Port:").grid(row=0, column=2, sticky="w")
        self.port_e = ttk.Entry(conn, width=6)
        self.port_e.insert(0, str(DEFAULT_PORT))
        self.port_e.grid(row=0, column=3, sticky="w")

        ttk.Label(conn, text="Topic:").grid(row=1, column=0, sticky="w")
        self.topic_e = ttk.Entry(conn)
        self.topic_e.insert(0, DEFAULT_TOPIC)
        self.topic_e.grid(row=1, column=1, columnspan=3, sticky="ew")

        ttk.Label(conn, text="User:").grid(row=2, column=0, sticky="w")
        self.user_e = ttk.Entry(conn)
        self.user_e.insert(0, DEFAULT_USER)
        self.user_e.grid(row=2, column=1, sticky="ew")

        ttk.Label(conn, text="Pass:").grid(row=2, column=2, sticky="w")
        self.pass_e = ttk.Entry(conn, show="*")
        self.pass_e.insert(0, DEFAULT_PASS)
        self.pass_e.grid(row=2, column=3, sticky="w")

        btn_frame = ttk.Frame(conn)
        btn_frame.grid(row=3, column=0, columnspan=4, sticky="ew", pady=(6,0))
        self.connect_btn = ttk.Button(btn_frame, text="Connect", command=self.toggle_connect)
        self.connect_btn.pack(side="left")
        ttk.Button(btn_frame, text="Export CSV", command=self.export_csv).pack(side="left", padx=6)
        ttk.Button(btn_frame, text="Clear Data", command=self.clear_data).pack(side="left")

        self.status_label = ttk.Label(conn, text="Disconnected", foreground="red")
        self.status_label.grid(row=0, column=4, rowspan=2, sticky="e", padx=6)

        # Readouts frame (big numbers)
        readouts = ttk.LabelFrame(frm, text="Current Readings")
        readouts.grid(row=1, column=0, sticky="ew", padx=4, pady=4)
        readouts.columnconfigure(1, weight=1)

        ttk.Label(readouts, text="Temperature (°C):").grid(row=0, column=0, sticky="w", padx=6, pady=4)
        self.temp_val = ttk.Label(readouts, text="—", font=("TkDefaultFont", 24))
        self.temp_val.grid(row=0, column=1, sticky="w", padx=6)

        ttk.Label(readouts, text="Humidity (%):").grid(row=1, column=0, sticky="w", padx=6, pady=4)
        self.hum_val = ttk.Label(readouts, text="—", font=("TkDefaultFont", 24))
        self.hum_val.grid(row=1, column=1, sticky="w", padx=6)

        ttk.Label(readouts, text="Gas (raw):").grid(row=2, column=0, sticky="w", padx=6, pady=4)
        self.gas_val = ttk.Label(readouts, text="—", font=("TkDefaultFont", 24))
        self.gas_val.grid(row=2, column=1, sticky="w", padx=6)

        # Log (compact)
        self.log = tk.Text(frm, height=6, state="disabled", font=("Consolas", 10))
        self.log.grid(row=2, column=0, sticky="nsew", padx=4, pady=(4,0))
        frm.rowconfigure(2, weight=0)

    def _build_plot(self):
        # Plot area below
        self.fig = Figure(figsize=(8,3), dpi=100)
        self.ax = self.fig.add_subplot(111)
        self.ax.set_title("Last values (seconds)")
        self.ax.set_xlabel("Time (s ago)")
        self.ax.set_ylabel("Value")
        self.canvas = FigureCanvasTkAgg(self.fig, master=self.root)
        self.canvas.get_tk_widget().grid(row=3, column=0, sticky="nsew", padx=8, pady=8)
        self.root.rowconfigure(3, weight=1)

    def toggle_connect(self):
        if self.mqtt_client and self.connected:
            self._disconnect()
        else:
            self._connect()

    def _connect(self):
        host = self.host_e.get().strip()
        try:
            port = int(self.port_e.get().strip())
        except:
            messagebox.showerror("Bad port", "Port must be integer")
            return
        topic = self.topic_e.get().strip()
        if not host or not topic:
            messagebox.showerror("Missing settings", "Host and Topic required")
            return

        client = mqtt.Client()
        user = self.user_e.get().strip()
        pwd = self.pass_e.get()
        if user:
            client.username_pw_set(user, pwd)

        client.on_connect = self._on_connect
        client.on_disconnect = self._on_disconnect
        client.on_message = self._on_message
        try:
            client.connect(host, port, keepalive=60)
        except Exception as e:
            messagebox.showerror("Connect failed", f"{e}")
            return
        client.loop_start()
        self.mqtt_client = client
        self.status_label.config(text="Connecting...", foreground="orange")
        self.connect_btn.config(text="Disconnect")

    def _disconnect(self):
        if self.mqtt_client:
            try:
                self.mqtt_client.disconnect()
                self.mqtt_client.loop_stop()
            except:
                pass
        self.mqtt_client = None
        self.connected = False
        self.status_label.config(text="Disconnected", foreground="red")
        self.connect_btn.config(text="Connect")

    # MQTT callbacks
    def _on_connect(self, client, userdata, flags, rc):
        topic = self.topic_e.get().strip()
        try:
            client.subscribe(topic)
            self.msg_q.put(("__SYS__", f"SUBSCRIBED {topic}", time.time()))
        except Exception as e:
            self.msg_q.put(("__SYS__", f"SUBSCRIBE FAILED {e}", time.time()))
        self.msg_q.put(("__SYS__", "CONNECTED", time.time()))
        self.connected = True

    def _on_disconnect(self, client, userdata, rc):
        self.msg_q.put(("__SYS__", "DISCONNECTED", time.time()))
        self.connected = False

    def _on_message(self, client, userdata, msg):
        ts = time.time()
        try:
            payload = msg.payload.decode('utf-8', errors='replace')
        except:
            payload = str(msg.payload)
        self.msg_q.put((msg.topic, payload, ts))

    def _poll_queue(self):
        updated = False
        while True:
            try:
                topic, payload, ts = self.msg_q.get_nowait()
            except queue.Empty:
                break
            if topic == "__SYS__":
                self._append_log(ts, "SYS", payload)
                if payload == "CONNECTED":
                    self.status_label.config(text="Connected", foreground="green")
                elif payload == "DISCONNECTED":
                    self.status_label.config(text="Disconnected", foreground="red")
                updated = True
                continue

            # normal message
            self._append_log(ts, topic, payload)
            self.raw_log.append((ts, topic, payload))
            # parse JSON
            try:
                obj = json.loads(payload)
            except:
                obj = None

            if isinstance(obj, dict):
                # update display values and buffers
                if "temperature" in obj:
                    try:
                        t = float(obj["temperature"])
                        self.temp_val.config(text=f"{t:.1f} °C")
                        self.temp_buf.append((ts, t))
                    except:
                        pass
                if "humidity" in obj:
                    try:
                        h = float(obj["humidity"])
                        self.hum_val.config(text=f"{h:.1f} %")
                        self.hum_buf.append((ts, h))
                    except:
                        pass
                if "gas_raw" in obj:
                    try:
                        g = float(obj["gas_raw"])
                        self.gas_val.config(text=f"{g:.0f}")
                        self.gas_buf.append((ts, g))
                    except:
                        pass
            else:
                # not JSON -- ignore for plotting but keep in log
                pass
            updated = True

        if updated:
            self._update_plot()
        self.root.after(POLL_MS, self._poll_queue)

    def _append_log(self, ts, topic, payload):
        timestr = time.strftime("%H:%M:%S", time.localtime(ts))
        line = f"[{timestr}] {topic}: {payload}\n"
        self.log.configure(state="normal")
        self.log.insert("end", line)
        self.log.yview_moveto(1.0)
        self.log.configure(state="disabled")

    def _update_plot(self):
        # We'll plot time relative to now (seconds ago)
        now = time.time()
        # prepare series
        def series_to_xy(buf):
            if not buf:
                return [], []
            xs = [now - t for (t, v) in buf]  # seconds ago
            ys = [v for (t, v) in buf]
            return xs, ys

        tx, ty = series_to_xy(self.temp_buf)
        hx, hy = series_to_xy(self.hum_buf)
        gx, gy = series_to_xy(self.gas_buf)

        self.ax.cla()
        self.ax.invert_xaxis()  # show newest at right? invert to show 0 on right
        if ty:
            self.ax.plot(tx, ty, label="Temp (°C)")
        if hy:
            self.ax.plot(hx, hy, label="Humidity (%)")
        if gy:
            self.ax.plot(gx, gy, label="Gas (raw)")
        self.ax.set_xlabel("Seconds ago")
        self.ax.set_title("Recent sensor history")
        self.ax.legend(loc="upper right")
        # autoscale y
        self.ax.relim()
        self.ax.autoscale_view()
        self.canvas.draw_idle()

    def export_csv(self):
        if not self.raw_log:
            messagebox.showinfo("No data", "No messages to export")
            return
        fn = filedialog.asksaveasfilename(defaultextension=".csv", filetypes=[("CSV file", "*.csv")])
        if not fn:
            return
        try:
            with open(fn, "w", newline="") as f:
                w = csv.writer(f)
                header = ["timestamp", "topic", "payload", "temperature", "humidity", "gas_raw"]
                w.writerow(header)
                for ts, topic, payload in self.raw_log:
                    temp = hum = gas = ""
                    try:
                        obj = json.loads(payload)
                        temp = obj.get("temperature", "")
                        hum = obj.get("humidity", "")
                        gas = obj.get("gas_raw", "")
                    except:
                        pass
                    w.writerow([time.strftime("%Y-%m-%d %H:%M:%S", time.localtime(ts)), topic, payload, temp, hum, gas])
            messagebox.showinfo("Exported", f"Saved to {fn}")
        except Exception as e:
            messagebox.showerror("Export failed", str(e))

    def clear_data(self):
        self.temp_buf.clear()
        self.hum_buf.clear()
        self.gas_buf.clear()
        self.raw_log.clear()
        self.log.configure(state="normal")
        self.log.delete("1.0", "end")
        self.log.configure(state="disabled")
        self.temp_val.config(text="—")
        self.hum_val.config(text="—")
        self.gas_val.config(text="—")
        self._update_plot()

    def _on_close(self):
        self._disconnect()
        self.root.destroy()

if __name__ == "__main__":
    root = tk.Tk()
    app = IoTFrontend(root)
    root.geometry("900x700")
    root.mainloop()
