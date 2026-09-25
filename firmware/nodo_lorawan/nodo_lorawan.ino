/*
 * Banco LoRaWAN UNRaf — nodo de prueba
 *
 * Manda un contador por LoRaWAN al gateway Milesight, que lo reenvía a
 * ChirpStack, que lo publica en MQTT. Sirve para validar la cadena completa
 * de punta a punta.
 *
 * HARDWARE: Seeed XIAO ESP32S3 + Wio-SX1262 (conector B2B, no cableado suelto)
 *
 * DEPENDENCIAS: solo RadioLib (>= 7.0). Gestor de librerías del IDE -> "RadioLib".
 *               No usa ArduinoJson: el payload LoRaWAN es binario y compacto;
 *               la traducción a JSON la hace el codec del lado de ChirpStack.
 *
 * IDE: Placa "XIAO_ESP32S3". USB CDC On Boot: ENABLED.
 *      (sin eso el monitor serie queda mudo)
 *
 * ANTES DE COMPILAR: cp credenciales.h.example credenciales.h  y completar
 * con las claves que genera ChirpStack.
 */

#include <RadioLib.h>
#include <Preferences.h>
#include "credenciales.h"

// ===== PERSISTENCIA EN NVS =====
// Guarda los nonces y la sesion LoRaWAN en la memoria no volatil del ESP32.
//
// SIN ESTO EL NODO NO PUEDE REINICIARSE. RadioLib arranca el DevNonce en 0
// (LoRaWAN.cpp:298) y lo incrementa por cada JoinRequest. ChirpStack lleva la
// lista de nonces ya usados para rechazar replays, asi que el segundo arranque
// manda un nonce repetido y el join se rechaza. Desde el monitor serie se ve
// igual que si no hubiera cobertura: el nodo transmite y nadie le contesta.
Preferences almacen;

const char* NVS_ESPACIO = "lorawan";
const char* NVS_NONCES  = "nonces";
const char* NVS_SESION  = "sesion";

// ===== PINES DEL WIO-SX1262 =====
// Valores del conector B2B del kit XIAO ESP32S3 + Wio-SX1262.
// No están en el wiki de Seeed; salen de la discusión #1361 de RadioLib,
// verificados por varios usuarios con el ejemplo Ping/Pong.
// Si el nodo no transmite, esto es lo primero a revisar.
#define PIN_NSS    41
#define PIN_DIO1   39
#define PIN_NRST   42
#define PIN_BUSY   40
#define PIN_ANT_SW 38   // habilita el camino de antena; va en HIGH

SX1262 radio = new Module(PIN_NSS, PIN_DIO1, PIN_NRST, PIN_BUSY);

// ===== CONFIGURACIÓN LORAWAN =====
// Argentina: AU915, sub-banda 2 (canales 8-15).
//
// OJO con la numeración, que no coincide entre las dos puntas:
//   - RadioLib  numera las sub-bandas desde 1  -> acá va 2
//   - ChirpStack las numera desde 0            -> allá es "au915_1"
// Son la misma sub-banda. Si no coinciden, el join sale y nadie contesta.
const LoRaWANBand_t Region  = AU915;
const uint8_t       SUBBANDA = 2;

LoRaWANNode node(&radio, &Region, SUBBANDA);

// ===== DATA RATE FIJO =====
// Para pruebas de alcance el ADR molesta: cerca del gateway el servidor sube
// el nodo a DR5 (SF7), y al alejarse no lo baja hasta que se pierden decenas
// de uplinks. Lo que se mediria es el alcance de SF7, no el de la red.
// Con ADR apagado el nodo transmite siempre a DATARATE.
//
// AU915: DR0 = SF12 ... DR5 = SF7, todos a 125 kHz. DR2 (SF10) es el mas
// lento que respeta el dwell time de 400 ms con este payload (~370 ms en el
// aire): con DR0 o DR1 cada sendReceive() falla con
// RADIOLIB_ERR_DWELL_TIME_EXCEEDED sin transmitir. Para volver al ADR:
// USAR_ADR = true.
const bool    USAR_ADR = false;
const uint8_t DATARATE = 2;

// ===== TEMPORIZACIÓN =====
// 20 s es para pruebas de alcance caminando: da un punto cada pocos metros.
// En producción hay que subirlo bastante: el fair-use de LoRaWAN son unos
// 30 s de aire por día por dispositivo.
const unsigned long INTERVALO_ENVIO = 20000UL;
unsigned long ultimoEnvio = 0;

uint16_t contador = 0;

// ===== REINTENTOS =====
// Cada lectura se manda como uplink CONFIRMADO: ChirpStack responde con un ACK
// en RX1 o RX2. Si el ACK no vuelve -porque el uplink no llego al gateway, o
// porque llego pero el ACK se perdio de vuelta- se reintenta hasta
// MAX_INTENTOS veces antes de darla por perdida y pasar a la siguiente.
//
// El costo: el gateway es half-duplex, y mientras transmite un ACK no escucha
// a nadie. Con muchos nodos confirmando cada minuto, los ACK mismos empiezan a
// tapar uplinks ajenos. Para un aula alcanza; para una red cargada conviene
// CONFIRMADO = false, y entonces solo se reintentan los errores locales de la
// radio (el nodo no tiene forma de saber si el paquete llego).
const bool          CONFIRMADO   = true;
const uint8_t       MAX_INTENTOS = 3;
//
// Las esperas estan pensadas para que los reintentos entren, casi siempre,
// dentro de INTERVALO_ENVIO: cada intento sin ACK ya tarda unos 4-6 s entre
// ventanas RX y el RETRANSMIT_TIMEOUT de RadioLib. Si se pasan, el proximo
// envio sale apenas terminan.
const unsigned long ESPERA_BASE  = 2000UL;   // se duplica en cada reintento
const unsigned long ESPERA_AZAR  = 1000UL;   // jitter, ver esperarReintento()

// ===== SETUP =====
void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 5000) { }   // espera al monitor, pero no cuelga

  Serial.println();
  Serial.println(F("=== Banco LoRaWAN UNRaf - nodo de prueba ==="));

  // El switch de antena tiene que estar habilitado antes de transmitir.
  pinMode(PIN_ANT_SW, OUTPUT);
  digitalWrite(PIN_ANT_SW, HIGH);

  Serial.print(F("Iniciando radio SX1262... "));
  int estado = radio.begin();
  if (estado != RADIOLIB_ERR_NONE) {
    Serial.print(F("FALLO, codigo "));
    Serial.println(estado);
    Serial.println(F("Revisar los pines del B2B y que el modulo este bien encastrado."));
    detener();
  }
  Serial.println(F("OK"));

  // El Wio-SX1262 usa DIO2 para conmutar TX/RX.
  radio.setDio2AsRfSwitch(true);

  Serial.print(F("Uniendo a la red (OTAA)... "));

  // El tercer argumento (nwkKey) va en nullptr A PROPOSITO.
  //
  // RadioLib decide la version de LoRaWAN por este puntero, no por una opcion:
  //   if(nwkKey) { this->rev = 1; ... }     <- LoRaWAN.cpp:675
  // Cualquier puntero no nulo -aunque apunte a 16 ceros- lo pone en modo 1.1,
  // y ahi el MIC del JoinRequest se calcula con nwkKey en vez de appKey.
  // Contra un device profile 1.0.3 el join falla sin decir por que.
  // Para LoRaWAN 1.0.x va nullptr y la unica clave es APP_KEY.
  node.beginOTAA(JOIN_EUI, DEV_EUI, nullptr, APP_KEY);

  // Recuperar lo guardado ANTES de activar: beginOTAA() limpia los nonces, asi
  // que restaurarlos antes no serviria de nada.
  almacen.begin(NVS_ESPACIO, false);
  restaurarBuffer(NVS_NONCES, RADIOLIB_LORAWAN_NONCES_BUF_SIZE, true);
  restaurarBuffer(NVS_SESION, RADIOLIB_LORAWAN_SESSION_BUF_SIZE, false);

  estado = node.activateOTAA();
  if (estado != RADIOLIB_LORAWAN_NEW_SESSION && estado != RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.print(F("FALLO, codigo "));
    Serial.println(estado);
    Serial.println(F("Si el JoinRequest aparece en ChirpStack pero no vuelve el Accept:"));
    Serial.println(F("  - claves de credenciales.h distintas a las del device, o"));
    Serial.println(F("  - DevNonce repetido (correr scripts/reset-nonces.sh)."));
    detener();
  }

  if (estado == RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.println(F("OK - sesion anterior recuperada, sin join"));
  } else {
    Serial.println(F("OK - join nuevo"));
  }

  // El join consume un DevNonce: guardarlo YA, antes de cualquier otra cosa.
  guardarBuffer(NVS_NONCES, node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

  fijarDatarate();
  Serial.println();

  ultimoEnvio = millis() - INTERVALO_ENVIO;   // primer envio inmediato
}

// ===== LOOP PRINCIPAL =====
void loop() {
  unsigned long ahora = millis();

  if (ahora - ultimoEnvio >= INTERVALO_ENVIO) {
    ultimoEnvio = ahora;
    enviarContador();
  }

  delay(50);
}

// ===== ENVIO =====
void enviarContador() {
  // Payload de 2 bytes, big-endian. El codec de ChirpStack lo vuelve a armar.
  // Todos los reintentos llevan el mismo contador: si un ACK se pierde y el
  // uplink si habia llegado, del lado del servidor se ve el valor repetido.
  uint8_t payload[2];
  payload[0] = (contador >> 8) & 0xFF;
  payload[1] = contador & 0xFF;

  bool entregado = false;
  for (uint8_t intento = 1; intento <= MAX_INTENTOS && !entregado; intento++) {
    if (intento > 1) {
      esperarReintento(intento);
    }

    Serial.print(F("[TX] contador="));
    Serial.print(contador);
    Serial.print(F(" intento "));
    Serial.print(intento);
    Serial.print('/');
    Serial.print(MAX_INTENTOS);
    Serial.print(F(" ... "));

    entregado = intentarEnvio(payload, sizeof(payload));
  }

  if (!entregado) {
    Serial.println(F("[TX] PERDIDO: se agotaron los intentos, sigue con el proximo"));
  }

  contador++;
}

// Un intento de uplink. Devuelve true si el dato quedo entregado.
bool intentarEnvio(const uint8_t* payload, size_t tam) {
  // sendReceive() solo completa el evento de bajada si llega un downlink, asi
  // que tiene que arrancar en cero. El ACK se lee de aca y no del evento de
  // subida: en RadioLib 7.7.1 eventUp->confirming sale siempre en false.
  LoRaWANEvent_t bajada = {};

  // sendReceive() manda el uplink y abre las ventanas RX1/RX2.
  int estado = node.sendReceive(payload, tam, 1, CONFIRMADO, nullptr, &bajada);

  // Guardar la sesion en CADA intento, no solo en los exitosos: aun con error
  // RadioLib avanza el FCnt, y si el nodo se reinicia con uno viejo ChirpStack
  // descarta los uplinks por contador repetido. Son hasta MAX_INTENTOS
  // escrituras de NVS por envio; frente al wear leveling del ESP32 sigue
  // siendo despreciable. Si algun dia se envia cada pocos segundos, conviene
  // espaciarlo.
  guardarBuffer(NVS_SESION, node.getBufferSession(), RADIOLIB_LORAWAN_SESSION_BUF_SIZE);

  if (estado == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
    // La sesion se invalido (RadioLib la borra, p.ej., si el servidor no
    // responde un rekey). Reintentar el uplink asi no sirve: primero el join.
    Serial.println(F("sin sesion"));
    reunirse();
    return false;
  }

  if (estado < RADIOLIB_ERR_NONE) {
    Serial.print(F("ERROR, codigo "));
    Serial.println(estado);
    return false;
  }

  if (CONFIRMADO && !bajada.confirming) {
    Serial.println(F("sin ACK del servidor"));
    return false;
  }

  if (estado > 0) {
    Serial.print(CONFIRMADO ? F("confirmado, ACK en RX") : F("enviado + downlink en RX"));
    Serial.println(estado);
  } else {
    Serial.println(F("enviado (sin downlink)"));
  }
  return true;
}

// Espera exponencial con un poco de azar. El azar importa en el aula: si dos
// nodos chocaron en el aire, sin jitter reintentarian al mismo tiempo y
// volverian a chocar.
void esperarReintento(uint8_t intento) {
  unsigned long espera = (ESPERA_BASE << (intento - 2)) + random(ESPERA_AZAR);

  // Si RadioLib todavia no habilita el proximo uplink (duty cycle, dwell
  // time), esperar al menos eso: si no, sendReceive() falla sin transmitir.
  unsigned long minimo = node.timeUntilUplink();
  if (espera < minimo) {
    espera = minimo;
  }

  Serial.print(F("     reintento en "));
  Serial.print(espera / 1000.0, 1);
  Serial.println(F(" s"));
  delay(espera);
}

// Rehace el join OTAA despues de perder la sesion. Si falla no se detiene: el
// proximo intento vuelve a encontrar el nodo sin sesion y lo prueba de nuevo.
void reunirse() {
  Serial.print(F("     rehaciendo el join... "));
  int estado = node.activateOTAA();

  // Cada JoinRequest consume un DevNonce, haya respuesta o no.
  guardarBuffer(NVS_NONCES, node.getBufferNonces(), RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

  if (estado == RADIOLIB_LORAWAN_NEW_SESSION) {
    Serial.println(F("OK"));
    fijarDatarate();   // el join nuevo vuelve al data rate por defecto
  } else {
    Serial.print(F("FALLO, codigo "));
    Serial.println(estado);
  }
}

// Se llama despues de cada join: activateOTAA() arma la sesion con el data
// rate por defecto, y la sesion restaurada trae el ultimo que eligio el ADR.
void fijarDatarate() {
  node.setADR(USAR_ADR);
  if (USAR_ADR) {
    Serial.println(F("Data rate: ADR (lo elige ChirpStack)"));
    return;
  }

  int16_t estado = node.setDatarate(DATARATE);
  Serial.print(F("Data rate: DR"));
  Serial.print(DATARATE);
  if (estado == RADIOLIB_ERR_NONE) {
    Serial.println(F(" fijo, ADR apagado"));
  } else {
    Serial.print(F(" rechazado, codigo "));
    Serial.println(estado);
  }
}

// ===== PERSISTENCIA =====
// Devuelve true si habia algo valido guardado y se aplico.
bool restaurarBuffer(const char* clave, size_t tam, bool esNonces) {
  if (!almacen.isKey(clave)) {
    return false;
  }

  uint8_t buffer[tam];
  if (almacen.getBytes(clave, buffer, tam) != tam) {
    return false;   // guardado incompleto: se descarta y se hace join nuevo
  }

  // setBuffer*() valida un checksum interno, asi que un buffer corrupto o en
  // blanco se rechaza solo: no hace falta chequearlo aca.
  int16_t estado = esNonces ? node.setBufferNonces(buffer)
                            : node.setBufferSession(buffer);
  return estado == RADIOLIB_ERR_NONE;
}

void guardarBuffer(const char* clave, const uint8_t* datos, size_t tam) {
  almacen.putBytes(clave, datos, tam);
}

// ===== UTILIDADES =====
// Error irrecuperable: no tiene sentido seguir, pero tampoco resetear en loop.
void detener() {
  Serial.println(F("Nodo detenido. Corregir y volver a flashear."));
  while (true) {
    delay(1000);
  }
}
