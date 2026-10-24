# Seguridad

Este repositorio es público y es material de clase de IC III.

## Cómo reportar un problema

Si encontrás una vulnerabilidad o una credencial publicada, **no abras un issue público**.
Reportalo en privado desde la pestaña **Security** del repositorio, con **Report a vulnerability**,
o por mail a la cátedra: matias.wanzenried@unraf.edu.ar. Si es una credencial, no la copies en el
reporte: alcanza con decir en qué archivo y commit está.

## Credenciales

- Las claves reales van en `.env` y en `firmware/nodo_lorawan/credenciales.h`, que no se versionan.
  `.env.example` trae valores de ejemplo, que hay que cambiar.
- ChirpStack arranca con el usuario `admin` / `admin`. Si el stack queda corriendo más allá de una
  práctica, cambiá esa contraseña desde la web apenas lo levantes.
- La web de ChirpStack y el broker MQTT escuchan solo en `127.0.0.1`. No los publiques en la red
  sin cambiar antes la contraseña de ChirpStack y sin desactivar el acceso anónimo del broker.
