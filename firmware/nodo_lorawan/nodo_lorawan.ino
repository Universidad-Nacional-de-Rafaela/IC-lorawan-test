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
#include "credenciales.h"

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

// ===== TEMPORIZACIÓN =====
// 60 s es cómodo para probar. En producción hay que subirlo bastante: el
// fair-use de LoRaWAN son unos 30 s de aire por día por dispositivo.
const unsigned long INTERVALO_ENVIO = 60000UL;
unsigned long ultimoEnvio = 0;

uint16_t contador = 0;

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

  estado = node.activateOTAA();
  if (estado != RADIOLIB_LORAWAN_NEW_SESSION && estado != RADIOLIB_LORAWAN_SESSION_RESTORED) {
    Serial.print(F("FALLO, codigo "));
    Serial.println(estado);
    Serial.println(F("Si el JoinRequest aparece en ChirpStack pero no vuelve el Accept,"));
    Serial.println(F("las claves de credenciales.h no coinciden con las del device."));
    detener();
  }
  Serial.println(F("OK - unido a la red"));
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
  uint8_t payload[2];
  payload[0] = (contador >> 8) & 0xFF;
  payload[1] = contador & 0xFF;

  Serial.print(F("[TX] contador="));
  Serial.print(contador);
  Serial.print(F(" ... "));

  // sendReceive() manda el uplink y abre las ventanas RX1/RX2.
  int estado = node.sendReceive(payload, sizeof(payload));

  if (estado == RADIOLIB_ERR_NONE) {
    Serial.println(F("enviado (sin downlink)"));
  } else if (estado > 0) {
    Serial.print(F("enviado + downlink en ventana RX"));
    Serial.println(estado);
  } else {
    Serial.print(F("ERROR, codigo "));
    Serial.println(estado);
  }

  contador++;
}

// ===== UTILIDADES =====
// Error irrecuperable: no tiene sentido seguir, pero tampoco resetear en loop.
void detener() {
  Serial.println(F("Nodo detenido. Corregir y volver a flashear."));
  while (true) {
    delay(1000);
  }
}
