#!/bin/bash
# Watchdog : verifie que le hotspot (partage de connexion) est actif
# et le relance automatiquement s'il est eteint.
# Sous-reseau attendu : 10.42.0.0/24 (NetworkManager "shared")

HOTSPOT_NAME="Hotspot"
INTERFACE="wlan0"
CHECK_INTERVAL=10   # secondes entre chaque verification

log() {
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] $1"
}

hotspot_est_actif() {
    nmcli -t -f NAME,DEVICE con show --active \
        | grep -q "^${HOTSPOT_NAME}:${INTERFACE}$"
}

activer_hotspot() {
    log "Hotspot inactif — activation en cours..."
    nmcli con up "${HOTSPOT_NAME}" ifname "${INTERFACE}"
    sleep 3
    if hotspot_est_actif; then
        log "Hotspot actif sur ${INTERFACE} (10.42.0.1/24)"
    else
        log "ERREUR : impossible d'activer le hotspot."
    fi
}

# --- Boucle principale ---
log "Watchdog hotspot demarre (interface=${INTERFACE}, ssid=${HOTSPOT_NAME})"

while true; do
    if ! hotspot_est_actif; then
        activer_hotspot
    fi
    sleep "${CHECK_INTERVAL}"
done
