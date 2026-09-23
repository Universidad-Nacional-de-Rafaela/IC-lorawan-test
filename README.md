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

## Si sos alumno: empezá por acá

**No levantes el stack de Docker.** En el aula hay **un solo gateway y un solo network
server**, los de la cátedra. El resto del repo es para entender cómo está armado ese
servidor, no para correrlo.

Si levantás tu propio ChirpStack, tu nodo igual va a transmitir contra el gateway del aula,
que reenvía al servidor de la cátedra — no al tuyo. Vas a ver un `JoinRequest` que nadie
contesta y parece un problema de radio, cuando en realidad estás mirando el servidor
equivocado.

Lo que sí tenés que hacer:

1. **Pedile al docente las claves de tu dispositivo**: `JOIN_EUI`, `DEV_EUI` y la
   *Application key*. Cada uno tiene las suyas — dos nodos con el mismo `DEV_EUI` se pisan
   y ninguno funciona bien.
2. **Instalá RadioLib** desde el gestor de librerías del IDE de Arduino.
3. **Creá tu `credenciales.h`** a partir de la plantilla, y completalo con tus claves:
   ```bash
   cd firmware/nodo_lorawan
   cp credenciales.h.example credenciales.h
   ```
   Ese archivo está en `.gitignore`: **tus claves no se suben al repo**.
4. **Configurá el IDE**: placa `XIAO_ESP32S3`, y **USB CDC On Boot: Enabled** — sin eso el
   monitor serie queda mudo y no vas a ver nada de lo que sigue.
5. **Flasheá** `firmware/nodo_lorawan/nodo_lorawan.ino` y abrí el monitor a **115200**.

Tenés que ver:

```
=== Banco LoRaWAN UNRaf - nodo de prueba ===
Iniciando radio SX1262... OK
Uniendo a la red (OTAA)... OK - join nuevo
[TX] contador=0 ... enviado (sin downlink)
```

Dónde se traba y qué significa:

| Se cuelga en | Qué pasa |
|---|---|
| `Iniciando radio SX1262` | La radio no responde. Módulo mal encastrado, o placa distinta al kit XIAO + Wio-SX1262 (el pinout del sketch es de ese kit). |
| `Uniendo a la red` | El join no cierra: claves mal copiadas, o el docente todavía no dio de alta tu dispositivo. |

El firmware está escrito para el kit **XIAO ESP32S3 + Wio-SX1262** con conector B2B. En otra
placa los pines no coinciden: compila igual y no transmite nada. Los `#define` del pinout
están al principio del sketch.

## Datos del banco

| | |
|---|---|
| Gateway | Milesight UG, EUI `24E124FFFEFAFC09`, MAC `24:E1:24:FA:FC:09` |
| Plan de bandas | AU915 sub-banda 2 (canales 8-15 + 65) — Argentina |
| Red del cable | notebook `192.168.23.100` ↔ gateway `192.168.23.150` |
| Web de ChirpStack | http://localhost:8080 (`admin` / `admin`) |

## Arranque del servidor (cátedra)

Esto lo corre quien monta el banco, una sola vez. Si sos alumno, saltealo.

```bash
cp .env.example .env
# generar un secreto propio:  openssl rand -base64 32
docker compose up -d
./scripts/escuchar.sh          # ver el tráfico MQTT en vivo
```

## Los cuatro cuidados que hacen fallar esto en silencio

Casi todos los problemas de un setup LoRaWAN no dan error: los paquetes simplemente se pierden y
no hay nada en los logs. Estos son los que importan:

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

**4. Un nodo que no persista sus nonces no puede reiniciarse.** RadioLib arranca el DevNonce en
cero y lo incrementa por cada JoinRequest; ChirpStack lleva la lista de los ya usados para
rechazar replays. Un nodo que vuelva a arrancar desde cero manda un nonce repetido y su join se
rechaza — y desde el monitor serie se ve idéntico a no tener cobertura. `nodo_lorawan.ino` guarda
nonces y sesión en la NVS del ESP32, así que sobrevive a los reinicios; para firmware ajeno que no
lo haga, está `scripts/reset-nonces.sh`.

Y una que rompe distinto: **RadioLib elige la versión de LoRaWAN por el puntero `nwkKey`** de
`beginOTAA()`. Si no es nulo pasa a modo 1.1 y calcula el MIC del JoinRequest con la clave
equivocada. Un array de 16 ceros es un puntero válido, así que para LoRaWAN 1.0.x va `nullptr`.

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
scripts/generar-credenciales.sh        arma el credenciales.h de un nodo desde ChirpStack
scripts/registrar-alcance.sh           registra uplinks a CSV para pruebas de alcance
scripts/reset-nonces.sh                destraba un join rechazado por DevNonce repetido
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
