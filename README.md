# Evaluación final – Microprocesadores II
## Webapp embebida en ESP32 (HMI por servidor web)

**Método HMI:** servidor web embebido en el ESP32 (HTML/JS) que se comunica con el navegador mediante **HTTP + JSON** (`fetch`).
**Sensor:** DS18B20 · **Actuador:** LED embebido · **Almacenamiento del log:** LittleFS (memoria flash del ESP32)

---

## 1. Qué hace el sistema

El ESP32 mide la temperatura con un DS18B20 y sirve desde su propia memoria una página web. Desde cualquier celular o PC conectado a la misma red se puede:

| # | Tarea del enunciado | Dónde se cumple |
|---|---|---|
| 1 | Botones Encender/Apagar del LED y reflejo del estado | Tarjeta **LED embebido**; `POST /api/led?state=1\|0` |
| 2 | Temperatura DS18B20 actualizada cada 2 s sin recargar | Tarjeta **Temperatura**; la página consulta `GET /api/status` cada 1 s y muestra cada lectura nueva (muestreo por defecto: 2 s) |
| 3 | Gráfico Chart.js con las últimas 20 lecturas | Tarjeta **Últimas 20 lecturas**; historial en `GET /api/history` |
| 4 | Semáforo verde/rojo de conexión | Esquina superior derecha; rojo tras 2 consultas fallidas, vuelve a verde solo al reconectar |
| 5 | Intervalo de muestreo de 1 a 5 s | Tarjeta **Intervalo de muestreo**; `POST /api/intervalo?s=N` |
| 6 | Log en LittleFS, descarga y vista previa | `/log.csv` (botón **Descargar log**) y tabla con las últimas 10 entradas (`GET /api/log`) |

Extras: modo punto de acceso si no hay Wi-Fi, alarma de temperatura y de sensor desconectado, reconexión automática del Wi-Fi, Chart.js incrustado en el firmware (la página funciona **sin internet**).

## 2. Materiales

- ESP32 (cualquier variante con LED en GPIO 2) y cable USB
- Sensor DS18B20
- Resistencia de 4.7 kΩ (pull-up)
- Protoboard y cables

## 3. Diagrama de conexiones

```
                 ESP32 DevKit
              ┌───────────────┐
              │            3V3├────────┬───────────────┐
              │               │        │               │
              │               │      [4.7 kΩ]          │
              │               │        │               │
              │         GPIO 4├────────┴──────┐        │
              │               │               │        │
              │            GND├──────┐        │        │
              │               │      │        │        │
              │  LED embebido │      │        │        │
              │   (GPIO 2)    │      │        │        │
              └───────────────┘      │        │        │
                                     │        │        │
                           DS18B20 (vista frontal, cara plana hacia ti)
                                  ┌──┴──┐  ┌──┴──┐  ┌──┴──┐
                                  │  1  │  │  2  │  │  3  │
                                  │ GND │  │DATA │  │ VDD │
                                  └─────┘  └─────┘  └─────┘
```

| DS18B20 | ESP32 |
|---|---|
| Pin 1 – GND | GND |
| Pin 2 – DATA | GPIO 4 (y 4.7 kΩ hacia 3V3) |
| Pin 3 – VDD | 3V3 |

> Agrega aquí una foto de **tu** protoboard (`conexiones.jpg`) en la carpeta de entrega.

## 4. Instalación y ejecución

1. **Arduino IDE**: instala la placa *esp32 by Espressif Systems* (Gestor de placas) y las librerías **OneWire** y **DallasTemperature** (Gestor de librerías).
2. Abre `ESP32_HMI_Webapp/ESP32_HMI_Webapp.ino`.
3. Edita las líneas `WIFI_SSID` y `WIFI_PASS` con tu red. *No subas a internet tu contraseña real.* Si las dejas sin cambiar, el ESP32 crea su propia red `ESP32-HMI` (clave `12345678`).
4. Placa: **ESP32 Dev Module** · Esquema de particiones: **Default 4MB with spiffs**. Selecciona el puerto y pulsa **Subir**.
5. Abre el **Monitor Serie** a **115200 baudios**: ahí aparece la dirección, por ejemplo `http://192.168.1.50`.
6. Abre esa dirección en el navegador del celular o PC (misma red). También funciona `http://esp32-hmi.local`.
   - En modo punto de acceso: conéctate a la red `ESP32-HMI` y abre `http://192.168.4.1`.

## 5. API del ESP32

| Método | Ruta | Descripción |
|---|---|---|
| GET | `/` | Página web |
| GET | `/chart.js` | Chart.js 4.6.1 (incrustado, comprimido con gzip) |
| GET | `/api/status` | Estado: temperatura, LED, intervalo, alarmas, etc. |
| GET | `/api/history` | Últimas 20 lecturas (etiquetas y valores) |
| GET | `/api/log` | Últimas 10 líneas del log (texto) |
| GET | `/log.csv` | Descarga del log completo |
| POST | `/api/led?state=1\|0` | Enciende / apaga el LED |
| POST | `/api/intervalo?s=1..5` | Cambia el intervalo de muestreo (otro valor → error 400) |

## 6. Registro (log)

Archivo `/log.csv` en LittleFS con el formato `timestamp,evento,valor,estado`:

```
timestamp,evento,valor,estado
0,ARRANQUE,v1.0,OK
0,WIFI,192.168.1.50,CONECTADO
1,LECTURA_TEMP,24.37,OK
8,CMD_LED,ON,OK
11,CMD_INTERVALO,4s,OK
24,ALARMA_TEMP,30.12,ACTIVA
```

| Evento | Cuándo se registra | Valor | Estado |
|---|---|---|---|
| `ARRANQUE` | al iniciar | versión | `OK` |
| `WIFI` | al iniciar, al perder / recuperar la red | IP | `CONECTADO`, `PUNTO_ACCESO`, `PERDIDO`, `RECUPERADO` |
| `LECTURA_TEMP` | cada lectura del sensor | °C | `OK` o `ERROR_SENSOR` |
| `CMD_LED` | botón Encender/Apagar | `ON` / `OFF` | `OK` |
| `CMD_INTERVALO` | cambio de intervalo | segundos | `OK` o `RECHAZADO` |
| `ALARMA_TEMP` | temperatura ≥ 30 °C (con histéresis de 1 °C) | °C | `ACTIVA` / `NORMAL` |
| `ALARMA_SENSOR` | el DS18B20 no responde | `--` | `ACTIVA` / `NORMAL` |
| `DESCARGA_LOG` | al descargar el log | bytes | `OK` |

- El `timestamp` son los segundos transcurridos desde que se encendió el ESP32 (no tiene reloj de fecha y hora). Vuelve a 0 en cada reinicio y cada arranque queda marcado con el evento `ARRANQUE`.
- Para no llenar la flash, al llegar a 256 KB el log pasa a `/log_anterior.csv` y se empieza uno nuevo.

## 7. Estructura de la carpeta

```
ESP32_HMI_Webapp.ino   firmware (servidor web, sensor, log)
index_html.h           página web (HTML + CSS + JavaScript)
chartjs_gz.h           Chart.js incrustado como arreglo de bytes
README.md              este documento
log.csv                log real descargado desde la página (evidencia)
capturas/              capturas de pantalla
video.mp4              video demostrativo
```

`chartjs_gz.h` se generó comprimiendo `chart.umd.min.js` (paquete npm `chart.js`) con `gzip -9` y convirtiéndolo a arreglo C con `xxd -i`. Así la página no depende de internet para dibujar el gráfico.

## 8. Referencias y código de terceros

- [Chart.js](https://www.chartjs.org) 4.6.1 – licencia MIT.
- Librerías de Arduino: OneWire, DallasTemperature, y las incluidas en el núcleo del ESP32 (WiFi, WebServer, ESPmDNS, LittleFS).

## 9. Conclusiones

> *(Escribe aquí tus propias conclusiones: qué aprendiste, qué dificultades tuviste, qué mejorarías.)*
