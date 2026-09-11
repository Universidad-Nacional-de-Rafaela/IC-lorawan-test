# Banco LoRaWAN — UNRaf

Stack completo para levantar una red LoRaWAN privada: gateway **Milesight UG**, network server
**ChirpStack v4** en Docker y un nodo **XIAO ESP32S3 + Wio-SX1262**.

```
XIAO + SX1262  ──LoRaWAN AU915──>  Milesight UG  ──Semtech UDP:1700──>  ChirpStack  ──MQTT──>  tu código
```

El gateway es deliberadamente tonto: escucha radio y reenvía por IP, nada más. No sabe qué
dispositivos existen ni puede descifrar nada. **ChirpStack** es el que descifra (AES), deduplica los
paquetes que llegan por varios gateways, lleva los contadores de frame anti-replay, resuelve el
handshake de join y decide data rate y potencia de cada nodo (ADR). Su salida es MQTT, así que del
lado de tu código un nodo LoRaWAN se ve igual que cualquier otro: un JSON en un tópico.

## Datos del banco

| | |
|---|---|
| Gateway | Milesight UG, EUI `24E124FFFEFAFC09`, MAC `24:E1:24:FA:FC:09` |
| Plan de bandas | AU915 sub-banda 2 (canales 8-15 + 65) — Argentina |
| Red del cable | notebook `192.168.23.100` ↔ gateway `192.168.23.150` |
| Web de ChirpStack | http://localhost:8080 (`admin` / `admin`) |

## Arranque rápido

```bash
cp .env.example .env
# generar un secreto propio:  openssl rand -base64 32
docker compose up -d
./scripts/escuchar.sh          # ver el tráfico MQTT en vivo
```

## Los tres cuidados que hacen fallar esto en silencio

Casi todos los problemas de un setup LoRaWAN no dan error: los paquetes simplemente se pierden y
no hay nada en los logs. Estos son los tres que importan:

**1. La región tiene que coincidir en las tres puntas.** El packet forwarder del gateway, el
`enabled_regions` de `chirpstack.toml` y el prefijo de los tópicos MQTT del gateway-bridge. Si el
bridge publica en `eu868/gateway/...` (el default del ejemplo oficial) y ChirpStack escucha en
`au915_1/gateway/...`, los paquetes llegan y se descartan sin una sola línea de log. Acá el prefijo
sale de `CHIRPSTACK_REGION` en el `.env`, justamente para que no se puedan desincronizar.

**2. La sub-banda se numera distinto en cada lado.** RadioLib la numera desde 1, ChirpStack desde 0.
La sub-banda 2 de Argentina es `SUBBANDA = 2` en el firmware y `au915_1` en ChirpStack. Son la
misma; no es un error de tipeo.

**3. El network server embebido del Milesight compite con éste.** Los UG traen ChirpStack adentro.
Hay que apagarlo (*Network Server → General → Enable: OFF*) y dejar el gateway en modo packet
forwarder puro.

## Estructura

```
docker-compose.yml                     5 servicios (ver abajo)
.env.example                           región, bind del UDP, credenciales
configuration/
  chirpstack/chirpstack.toml           enabled_regions = ["au915_1"]
  chirpstack/region_au915_1.toml       definición de la banda (oficial, sin tocar)
  chirpstack-gateway-bridge/           config del bridge Semtech UDP
  mosquitto/config/mosquitto.conf      broker sin auth, solo red interna
  postgresql/initdb/                   crea las extensiones pg_trgm y hstore
  codec-contador.js                    decodificador para pegar en ChirpStack
scripts/escuchar.sh                    suscriptor MQTT
firmware/nodo_lorawan/                 sketch Arduino del nodo
```

| Servicio | Imagen | Expuesto en | Rol |
|---|---|---|---|
| `chirpstack` | `chirpstack/chirpstack:4` | `127.0.0.1:8080` | Network server + web |
| `chirpstack-gateway-bridge` | `.../chirpstack-gateway-bridge:4` | `${UDP_BIND_ADDR}:1700/udp` | Semtech UDP ↔ MQTT |
| `mosquitto` | `eclipse-mosquitto:2` | `127.0.0.1:1883` | Broker |
| `postgres` | `postgres:15-alpine` | solo red interna | Estado de ChirpStack |
| `redis` | `redis:7-alpine` | solo red interna | Sesiones y downlinks |

Nada se publica en `0.0.0.0` salvo el puerto 1700 mientras `UDP_BIND_ADDR` siga en su valor
inicial. Una vez configurada la red del cable conviene pasarlo a `192.168.23.100` para que el
puerto no quede expuesto en el WiFi de la facultad.

Este stack tiene **su propio Mosquitto**: no toca el `aura-mosquitto` de `aura-app`, ni su red
`aura-network`. Se puede levantar y destruir sin afectar a AURA.

## Verificación, en orden

Cada paso valida el anterior. Si uno falla, los siguientes no tienen sentido.

1. **Stack arriba** — `docker compose ps`: `postgres` y `redis` en *healthy*, el resto *Up*.
2. **Región correcta** — `docker compose logs chirpstack | grep region`: debe decir
   `common_name=AU915 region_id=au915_1`.
3. **Cable** — `ping 192.168.23.150` responde, y `ip route | head -1` sigue saliendo por WiFi
   (si la ruta por defecto se fue al cable, falta `ipv4.never-default`).
4. **Gateway conectado** — en la web, *Gateways* → el EUI en verde con *Last seen* actualizándose.
   En paralelo, `docker compose logs -f chirpstack-gateway-bridge` muestra los `PULL_DATA`.
   **Éste es el hito que valida gateway + red + stack.**
5. **Tráfico en crudo** — `./scripts/escuchar.sh gateway` muestra `event/stats` cada 30 s, aun sin
   ningún nodo encendido.
6. **Join del nodo** — al encender la XIAO, en *Device → LoRaWAN frames* aparece el `JoinRequest`
   y detrás el `JoinAccept`. Si está el request pero no el accept, las claves OTAA no coinciden.
7. **Cadena completa** — `./scripts/escuchar.sh app` imprime el contador incrementándose cada 60 s.

## Pendiente: integración con AURA

ChirpStack publica en `application/<id>/device/<devEUI>/event/up`. El contrato de AURA
(`../aura/aura-app/docs/CONTRATO_MQTT.md` v1.1) exige `devices/<uuid>/data`, con el identificador
**en el tópico** y como **UUID**, no como DevEUI.

Cerrar esa brecha necesita un servicio puente y una decisión de mapeo DevEUI→UUID — el mismo
problema que `nodo_gateway.ino` resuelve hoy con una tabla hardcodeada. Es una decisión de
arquitectura que merece su propio ADR antes de escribir código.
