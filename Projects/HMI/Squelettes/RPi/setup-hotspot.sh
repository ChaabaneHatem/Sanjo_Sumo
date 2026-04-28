#!/bin/bash
# Script d'installation a executer UNE SEULE FOIS sur le RPi.
# Configure le hotspot WiFi et installe le service watchdog.

HOTSPOT_NAME="Hotspot"
SSID="RPi-Combat"
PASSWORD="combat123"
INTERFACE="wlan0"

echo "=== Configuration du hotspot WiFi permanent ==="

# 1. Creer la connexion hotspot seulement si elle n'existe pas encore
if nmcli con show "${HOTSPOT_NAME}" &>/dev/null; then
    echo "Connexion '${HOTSPOT_NAME}' existe deja — mise a jour des parametres"
    nmcli con modify "${HOTSPOT_NAME}" \
        wifi-sec.key-mgmt wpa-psk \
        wifi-sec.psk "${PASSWORD}" \
        connection.autoconnect no
else
    nmcli con add type wifi ifname "${INTERFACE}" con-name "${HOTSPOT_NAME}" \
        autoconnect no \
        ssid "${SSID}" \
        -- \
        wifi.mode ap \
        wifi.band bg \
        wifi.channel 6 \
        wifi-sec.key-mgmt wpa-psk \
        wifi-sec.psk "${PASSWORD}" \
        ipv4.method shared \
        ipv4.addresses "10.42.0.1/24"
    echo "Connexion '${HOTSPOT_NAME}' creee (SSID=${SSID})"
fi
echo "Hotspot configure (le watchdog l'activera au demarrage)"

# 3. Installer le script watchdog (seulement si pas deja en place)
SCRIPT_SRC="$(cd "$(dirname "$0")" && pwd)/hotspot-watchdog.sh"
if [ "$SCRIPT_SRC" != "/home/pi/hotspot-watchdog.sh" ]; then
    cp "$SCRIPT_SRC" /home/pi/hotspot-watchdog.sh
fi
chmod +x /home/pi/hotspot-watchdog.sh

# 4. Installer le service systemd (enable seulement — demarre au prochain boot)
cp "$(cd "$(dirname "$0")" && pwd)/hotspot-watchdog.service" /etc/systemd/system/hotspot-watchdog.service
systemctl daemon-reload
systemctl enable hotspot-watchdog.service

echo ""
echo "=== Installation terminee ==="
echo "SSID     : ${SSID}"
echo "Mot passe: ${PASSWORD}"
echo "IP RPi   : 10.42.0.1 (apres redemarrage)"
echo ""
echo "IMPORTANT : wlan0 ne peut pas etre sur sanjo ET hotspot en meme temps."
echo "Au redemarrage, le hotspot prendra la priorite sur sanjo."
echo "Pour SSH apres reboot : connecte-toi au reseau ${SSID} puis ssh pi@10.42.0.1"
echo ""
echo "Redemarrer maintenant ? (o/n)"
read -r rep
if [ "$rep" = "o" ]; then sudo reboot; fi
