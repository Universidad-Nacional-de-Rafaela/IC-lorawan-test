// Codec del nodo de prueba, para pegar en ChirpStack:
//   Device profiles -> [tu perfil] -> Codec -> Payload codec: JavaScript functions
//
// Convierte los 2 bytes que manda nodo_lorawan.ino en un objeto legible, para
// ver el valor en la web y recibirlo ya decodificado por MQTT.

function decodeUplink(input) {
  if (input.bytes.length < 2) {
    return { errors: ["payload demasiado corto: se esperaban 2 bytes"] };
  }

  // Big-endian, igual que lo arma el firmware.
  var contador = (input.bytes[0] << 8) | input.bytes[1];

  return {
    data: {
      contador: contador
    }
  };
}

// Downlink de ejemplo: manda un contador de 2 bytes de vuelta al nodo.
function encodeDownlink(input) {
  var valor = input.data.contador || 0;
  return {
    bytes: [(valor >> 8) & 0xff, valor & 0xff]
  };
}
