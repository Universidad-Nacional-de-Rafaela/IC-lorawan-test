#!/usr/bin/env bash
# Genera el credenciales.h de un nodo leyendo las claves directo de ChirpStack.
#
#   ./scripts/generar-credenciales.sh                 # lista los dispositivos
#   ./scripts/generar-credenciales.sh <devEUI>        # imprime el header
#   ./scripts/generar-credenciales.sh <devEUI> ruta.h # lo escribe a un archivo
#
# Evita transcribir a mano un DevEUI de 16 dígitos y una clave de 32: es la
# fuente de error más común al dar de alta un nodo, y el síntoma es un join que
# no cierra sin decir por qué.
#
# OJO CON LA COLUMNA
# ------------------
# En LoRaWAN 1.0.x, el campo que la web de ChirpStack muestra como "Application
# key" se guarda en la columna `nwk_key`, NO en `app_key`. Verificado byte a byte
# contra un nodo funcionando. Leer `app_key` por el nombre obvio da la clave
# equivocada. Los EUI van en MSB, el mismo orden que muestra la web.

set -euo pipefail

CONTENEDOR=lorawan-postgres
DEV_EUI="${1:-}"
SALIDA="${2:-}"

psql_q() {
  docker exec "$CONTENEDOR" psql -U chirpstack -d chirpstack -At -c "$1"
}

if ! docker ps --format '{{.Names}}' | grep -qx "$CONTENEDOR"; then
  echo "El contenedor $CONTENEDOR no está corriendo. Levantá el stack con: docker compose up -d" >&2
  exit 1
fi

if [ -z "$DEV_EUI" ]; then
  echo "Dispositivos dados de alta:"
  echo
  docker exec "$CONTENEDOR" psql -U chirpstack -d chirpstack -c \
    "select encode(d.dev_eui,'hex') as dev_eui, d.name,
            (k.dev_eui is not null) as tiene_claves
       from device d left join device_keys k using (dev_eui)
      order by d.name;"
  echo "Usá:  $0 <devEUI>"
  exit 0
fi

DEV_EUI=$(echo "$DEV_EUI" | tr 'A-Z' 'a-z' | tr -d ' :')

EXISTE=$(psql_q "select count(*) from device where dev_eui = decode('${DEV_EUI}','hex');")
if [ "$EXISTE" != "1" ]; then
  echo "No existe un dispositivo con DevEUI ${DEV_EUI}. Corré '$0' sin argumentos para verlos." >&2
  exit 1
fi

TIENE_CLAVES=$(psql_q "select count(*) from device_keys where dev_eui = decode('${DEV_EUI}','hex');")
if [ "$TIENE_CLAVES" != "1" ]; then
  echo "El dispositivo existe pero no tiene claves OTAA cargadas." >&2
  echo "Generalas en la web: Applications -> [app] -> [device] -> pestaña 'OTAA keys'." >&2
  exit 1
fi

NOMBRE=$(psql_q "select name from device where dev_eui = decode('${DEV_EUI}','hex');")
JOIN_EUI=$(psql_q "select encode(join_eui,'hex') from device where dev_eui = decode('${DEV_EUI}','hex');")
APP_KEY=$(psql_q "select encode(nwk_key,'hex') from device_keys where dev_eui = decode('${DEV_EUI}','hex');")

# 32 hex -> "0xAA, 0xBB, ..." en dos filas de ocho
bytes=$(echo "$APP_KEY" | sed -E 's/(..)/0x\1, /g' | sed 's/, $//')
fila1=$(echo "$bytes" | cut -d' ' -f1-16)
fila2=$(echo "$bytes" | cut -d' ' -f17-)

generar() {
cat <<EOF
/*
 * Credenciales OTAA — generado por scripts/generar-credenciales.sh
 * Dispositivo: ${NOMBRE}
 * Fecha: $(date +%Y-%m-%d)
 *
 * NO versionar este archivo: está en .gitignore.
 */

#ifndef CREDENCIALES_H
#define CREDENCIALES_H

static const uint64_t JOIN_EUI = 0x${JOIN_EUI};
static const uint64_t DEV_EUI  = 0x${DEV_EUI};

// La "Application key" de la web de ChirpStack (columna nwk_key en 1.0.x).
static const uint8_t APP_KEY[] = {
  ${fila1}
  ${fila2}
};

// Sin NWK_KEY: en LoRaWAN 1.0.x el sketch pasa nullptr en ese argumento de
// beginOTAA(). Un puntero no nulo pondría a RadioLib en modo 1.1 y el join
// fallaría. Ver el comentario en nodo_lorawan.ino.

#endif  // CREDENCIALES_H
EOF
}

if [ -n "$SALIDA" ]; then
  generar > "$SALIDA"
  echo "Escrito en $SALIDA  (dispositivo: ${NOMBRE})"
else
  generar
fi
