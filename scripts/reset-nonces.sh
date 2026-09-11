#!/usr/bin/env bash
# Borra los DevNonce que ChirpStack tiene registrados, para que un nodo que se
# reinició pueda volver a hacer join.
#
#   ./scripts/reset-nonces.sh              # todos los dispositivos
#   ./scripts/reset-nonces.sh <devEUI>     # solo uno
#
# CUÁNDO HACE FALTA
# -----------------
# En el uso normal, NUNCA: nodo_lorawan.ino guarda sus nonces en la NVS del
# ESP32, así que sobrevive a los reinicios por su cuenta.
#
# ChirpStack guarda los DevNonce ya usados para rechazar joins repetidos (es la
# protección anti-replay del protocolo). Un nodo que arranque con el contador en
# cero manda un nonce que ChirpStack ya vio y el join se rechaza sin que el nodo
# se entere — desde el monitor serie se ve igual que si no hubiera cobertura.
#
# Casos donde sí se necesita:
#   - un firmware de terceros o de un alumno que no persista nonces
#   - después de borrar la NVS de la placa (Erase Flash, o cambiar de partición)
#   - al reutilizar un DevEUI en un dispositivo distinto

set -euo pipefail

CONTENEDOR=lorawan-postgres
DEV_EUI="${1:-}"

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
  echo "El contenedor $CONTENEDOR no está corriendo." >&2
  exit 1
fi

if [ -n "$DEV_EUI" ]; then
  FILTRO="where dev_eui = decode('${DEV_EUI}', 'hex')"
  echo "Limpiando nonces de $DEV_EUI..."
else
  FILTRO=""
  echo "Limpiando nonces de TODOS los dispositivos..."
fi

docker exec "$CONTENEDOR" psql -U chirpstack -d chirpstack -c \
  "update device_keys set dev_nonces = '{}'::jsonb, join_nonce = 0 ${FILTRO};"

echo
echo "Listo. Estado actual:"
docker exec "$CONTENEDOR" psql -U chirpstack -d chirpstack -c \
  "select encode(dev_eui,'hex') as dev_eui, dev_nonces, join_nonce from device_keys;"
