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
