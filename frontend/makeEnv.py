#!/usr/bin/env python3
import os
import sys
import subprocess
import platform

PROJECT_DIR = os.path.dirname(os.path.abspath(__file__))
ENV_NAME = "IOTfrontend"
ENV_DIR = os.path.join(PROJECT_DIR, ENV_NAME)

def env_python():
    if platform.system() == "Windows":
        return os.path.join(ENV_DIR, "Scripts", "python.exe")
    else:
        return os.path.join(ENV_DIR, "bin", "python")

def run(cmd):
    print(">", " ".join(cmd))
    subprocess.check_call(cmd)

def main():
    # 1) Create env if missing
    if not os.path.exists(env_python()):
        print(f"Creating virtual environment '{ENV_NAME}'...")
        run([sys.executable, "-m", "venv", ENV_DIR])

    py = env_python()

    # 2) Upgrade pip
    print("Upgrading pip...")
    run([py, "-m", "pip", "install", "--upgrade", "pip"])

    # 3) Install requirements
    print("Installing requirements...")
    run([py, "-m", "pip", "install", "-r", "requirements.txt"])

    # 4) Run frontend
    print("Starting IoT frontend...")
    run([py, "iot_frontend.py"])

if __name__ == "__main__":
    main()
