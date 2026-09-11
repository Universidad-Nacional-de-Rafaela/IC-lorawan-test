#!/usr/bin/env bash
# Registra cada uplink con su calidad de señal, para pruebas de alcance.
#
#   ./scripts/registrar-alcance.sh [archivo.csv]
#
# Escribe una línea por uplink con hora, fCnt, data rate, frecuencia, RSSI y SNR.
# Dejarlo corriendo mientras se mueve el nodo; Ctrl-C para terminar.
#
# CÓMO LEER LOS NÚMEROS
#   RSSI: potencia recibida en dBm. Más cerca de 0 es mejor. Pegado al gateway
#         da unos -25; por debajo de -120 ya estás en el límite.
#   SNR : relación señal/ruido en dB. LoRa recupera hasta unos -20 dB, que es
#         justamente su gracia: sigue decodificando bajo el piso de ruido.
#   DR  : data rate. Si el ADR lo baja (5 -> 0) es que la señal se degrada: el
#         servidor pide transmitir más lento para llegar más lejos.

set -euo pipefail

SALIDA="${1:-alcance-$(date +%Y%m%d-%H%M).csv}"
CONTENEDOR=lorawan-mosquitto

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
  echo "El contenedor $CONTENEDOR no está corriendo." >&2
  exit 1
fi

echo "hora,fcnt,dr,frecuencia_hz,rssi_dbm,snr_db,contador" > "$SALIDA"
echo "Registrando en $SALIDA — Ctrl-C para terminar."
echo

docker exec "$CONTENEDOR" mosquitto_sub -h localhost -t 'application/+/device/+/event/up' \
  | python3 -u -c '
import sys, json, datetime

for linea in sys.stdin:
    linea = linea.strip()
    if not linea:
        continue
    try:
        d = json.loads(linea)
    except ValueError:
        continue

    rx = (d.get("rxInfo") or [{}])[0]
    fila = [
        datetime.datetime.now().strftime("%H:%M:%S"),
        d.get("fCnt"),
        d.get("dr"),
        (d.get("txInfo") or {}).get("frequency"),
        rx.get("rssi"),
        rx.get("snr"),
        (d.get("object") or {}).get("contador"),
    ]
    fila = ["" if v is None else v for v in fila]
    print(",".join(str(v) for v in fila), flush=True)
' | tee -a "$SALIDA"
