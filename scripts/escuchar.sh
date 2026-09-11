#!/usr/bin/env bash
# Escucha el tráfico MQTT del banco LoRaWAN en vivo.
#
#   ./scripts/escuchar.sh          # todo
#   ./scripts/escuchar.sh gateway  # solo eventos del gateway (stats, up)
#   ./scripts/escuchar.sh app      # solo uplinks ya descifrados de las aplicaciones
#
# Usa el mosquitto_sub de adentro del contenedor, así no hace falta instalar
# mosquitto-clients en la notebook.

set -euo pipefail

CONTENEDOR=lorawan-mosquitto

case "${1:-todo}" in
  gateway) TOPICO='+/gateway/#' ;;
  app)     TOPICO='application/#' ;;
  todo)    TOPICO='#' ;;
  *)       echo "Uso: $0 [todo|gateway|app]" >&2; exit 1 ;;
esac

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
  echo "El contenedor $CONTENEDOR no está corriendo. Levantá el stack con:" >&2
  echo "  docker compose up -d" >&2
  exit 1
fi

echo "Suscrito a '$TOPICO'. Ctrl-C para salir."
echo
exec docker exec -it "$CONTENEDOR" mosquitto_sub -h localhost -t "$TOPICO" -v
