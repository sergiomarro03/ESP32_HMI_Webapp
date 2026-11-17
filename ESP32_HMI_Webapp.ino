/*
  Evaluación final - Microprocesadores II
  Webapp embebida en ESP32 (servidor web con HTML/JS y API HTTP/JSON)

  Conexiones: DS18B20 DATA -> GPIO 4 (con pull-up de 4.7k a 3V3), VDD -> 3V3, GND -> GND
              LED embebido -> GPIO 2
  Librerías:  OneWire, DallasTemperature (placa: ESP32 Dev Module)
*/

#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <LittleFS.h>
#include <OneWire.h>
#include <DallasTemperature.h>

#include "index_html.h"
#include "chartjs_gz.h"

#define VERSION_FIRMWARE "1.0"

// ---- Configuración ----

const char* WIFI_SSID = "TU_SSID";
const char* WIFI_PASS = "TU_PASSWORD";

// Si no conecta al Wi-Fi en 15 s, el ESP32 crea su propia red (la página queda en 192.168.4.1)
const char* AP_SSID = "ESP32-HMI";
const char* AP_PASS = "12345678";

const char* NOMBRE_MDNS = "esp32-hmi";

const uint8_t PIN_LED     = 2;
const uint8_t PIN_DS18B20 = 4;

const uint8_t INTERVALO_MIN_S     = 1;
const uint8_t INTERVALO_MAX_S     = 5;
const uint8_t INTERVALO_INICIAL_S = 2;

// Alarma de temperatura, con histéresis para evitar rebotes
const float TEMP_ALARMA_C = 30.0;
const float HISTERESIS_C  = 1.0;

const char*   RUTA_LOG          = "/log.csv";
const char*   RUTA_LOG_ANTERIOR = "/log_anterior.csv";
const size_t  LOG_MAX_BYTES     = 256UL * 1024UL;
const size_t  LOG_MIN_LIBRE     = 32UL * 1024UL;
const uint8_t LOG_VISTA_N       = 10;
const uint8_t HISTORIAL_N       = 20;

// ---- Objetos y variables globales ----

WebServer         servidor(80);
OneWire           busOneWire(PIN_DS18B20);
DallasTemperature ds18b20(&busOneWire);

bool     ledEncendido = false;
uint32_t intervaloMs  = INTERVALO_INICIAL_S * 1000UL;

float    tempActual   = NAN;          // NAN = sin lectura válida
bool     alarmaTemp   = false;
bool     alarmaSensor = false;
uint32_t secuencia    = 0;            // cuenta las lecturas
char     etiquetaUltima[12] = "--";

// El sensor se lee en dos pasos para no bloquear el servidor web
bool     conversionEnCurso  = false;
uint32_t inicioConversionMs = 0;
uint32_t esperaConversionMs = 800;

// Últimas lecturas para el gráfico
float   histValores[HISTORIAL_N];
char    histEtiquetas[HISTORIAL_N][12];
uint8_t histCantidad = 0;
uint8_t histInicio   = 0;

bool     sistemaArchivosListo = false;
size_t   logBytes = 0;
uint32_t logTotal = 0;                // entradas escritas desde el arranque

bool modoEstacion = false;            // true = conectado a un router, false = punto de acceso
bool wifiOk       = false;

// ---- Marca de tiempo ----


void marcaTiempo(char* salida, size_t tam) {
  snprintf(salida, tam, "%lu", (unsigned long)(millis() / 1000));
}



void escribirEncabezadoLog() {
  File f = LittleFS.open(RUTA_LOG, "w");
  if (!f) return;
  const char* encabezado = "timestamp,evento,valor,estado\n";
  f.print(encabezado);
  f.close();
  logBytes = strlen(encabezado);
}

// Si el log crece demasiado, pasa a /log_anterior.csv y se empieza uno nuevo
void rotarLogSiHaceFalta() {
  size_t libre = LittleFS.totalBytes() - LittleFS.usedBytes();
  if (logBytes < LOG_MAX_BYTES && libre > LOG_MIN_LIBRE) return;
  LittleFS.remove(RUTA_LOG_ANTERIOR);
  LittleFS.rename(RUTA_LOG, RUTA_LOG_ANTERIOR);
  escribirEncabezadoLog();
}

// Agrega una línea al log y la muestra por el puerto serie
void registrarEvento(const char* evento, const char* valor, const char* estado) {
  char marca[24];
  marcaTiempo(marca, sizeof(marca));

  char linea[96];
  int n = snprintf(linea, sizeof(linea), "%s,%s,%s,%s\n", marca, evento, valor, estado);
  if (n <= 0) return;

  Serial.print("[LOG] ");
  Serial.print(linea);

  if (!sistemaArchivosListo) return;
  rotarLogSiHaceFalta();

  File f = LittleFS.open(RUTA_LOG, "a");
  if (!f) return;
  f.print(linea);
  f.close();
  logBytes += strlen(linea);
  logTotal++;
}

// Devuelve las últimas n líneas del log (sin el encabezado)
String ultimasLineasLog(uint8_t n) {
  if (!sistemaArchivosListo) return "";
  File f = LittleFS.open(RUTA_LOG, "r");
  if (!f) return "";

  // Solo se lee el final del archivo, donde están las últimas líneas
  const size_t VENTANA = 1536;
  size_t tam = f.size();
  size_t inicio = (tam > VENTANA) ? tam - VENTANA : 0;
  f.seek(inicio);
  String texto = f.readString();
  f.close();

  if (inicio > 0) {
    int p = texto.indexOf('\n');
    if (p >= 0) texto = texto.substring(p + 1);   // la primera línea puede venir cortada
  }

  // Buffer circular con las últimas n líneas
  String anillo[LOG_VISTA_N];
  uint8_t cantidad = 0, cabeza = 0;
  int pos = 0;
  while (pos < (int)texto.length()) {
    int fin = texto.indexOf('\n', pos);
    if (fin < 0) break;
    String linea = texto.substring(pos, fin);
    pos = fin + 1;
    if (linea.length() == 0 || linea.startsWith("timestamp,")) continue;
    anillo[cabeza] = linea;
    cabeza = (cabeza + 1) % LOG_VISTA_N;
    if (cantidad < LOG_VISTA_N) cantidad++;
  }

  String salida;
  uint8_t usar = (n < cantidad) ? n : cantidad;
  for (uint8_t i = 0; i < usar; i++) {
    uint8_t idx = (cabeza + LOG_VISTA_N - usar + i) % LOG_VISTA_N;
    salida += anillo[idx];
    salida += '\n';
  }
  return salida;
}

// ---- Sensor DS18B20 ----

// Guarda la lectura en el historial circular del gráfico
void guardarEnHistorial(float valor, const char* etiqueta) {
  uint8_t pos;
  if (histCantidad < HISTORIAL_N) {
    pos = (histInicio + histCantidad) % HISTORIAL_N;
    histCantidad++;
  } else {
    pos = histInicio;
    histInicio = (histInicio + 1) % HISTORIAL_N;
  }
  histValores[pos] = valor;
  strlcpy(histEtiquetas[pos], etiqueta, sizeof(histEtiquetas[pos]));
}

// Procesa una lectura nueva: historial, log y alarmas
void procesarLectura(float t) {
  char etiqueta[12];
  marcaTiempo(etiqueta, sizeof(etiqueta));
  secuencia++;
  strlcpy(etiquetaUltima, etiqueta, sizeof(etiquetaUltima));

  bool falla = isnan(t) || t == DEVICE_DISCONNECTED_C;

  if (falla) {
    tempActual = NAN;
    guardarEnHistorial(NAN, etiqueta);
    registrarEvento("LECTURA_TEMP", "--", "ERROR_SENSOR");
    if (!alarmaSensor) {
      alarmaSensor = true;
      registrarEvento("ALARMA_SENSOR", "--", "ACTIVA");
    }
    return;
  }

  tempActual = t;
  guardarEnHistorial(t, etiqueta);
  char valor[12];
  snprintf(valor, sizeof(valor), "%.2f", t);
  registrarEvento("LECTURA_TEMP", valor, "OK");

  if (alarmaSensor) {
    alarmaSensor = false;
    registrarEvento("ALARMA_SENSOR", "--", "NORMAL");
  }
  if (!alarmaTemp && t >= TEMP_ALARMA_C) {
    alarmaTemp = true;
    registrarEvento("ALARMA_TEMP", valor, "ACTIVA");
  } else if (alarmaTemp && t <= TEMP_ALARMA_C - HISTERESIS_C) {
    alarmaTemp = false;
    registrarEvento("ALARMA_TEMP", valor, "NORMAL");
  }
}

// Mide según el intervalo configurado. La conversión del DS18B20 tarda ~750 ms,
// así que se inicia y se lee después, sin usar delay() para no congelar la web
void atenderSensor() {
  uint32_t ahora = millis();

  if (!conversionEnCurso) {
    if (ahora - inicioConversionMs >= intervaloMs) {
      ds18b20.requestTemperatures();
      inicioConversionMs = ahora;
      conversionEnCurso = true;
    }
    return;
  }

  if (ahora - inicioConversionMs < esperaConversionMs) return;
  conversionEnCurso = false;
  procesarLectura(ds18b20.getTempCByIndex(0));
}

// ---- Red: Wi-Fi y mDNS ----

bool conectarWifi() {
  if (strlen(WIFI_SSID) == 0 || strcmp(WIFI_SSID, "TU_SSID") == 0) {
    Serial.println("Wi-Fi sin configurar: se usará el punto de acceso.");
    return false;
  }
  Serial.printf("Conectando a la red \"%s\"", WIFI_SSID);
  WiFi.mode(WIFI_STA);
  WiFi.setHostname(NOMBRE_MDNS);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300);
    Serial.print('.');
  }
  Serial.println();
  return WiFi.status() == WL_CONNECTED;
}

void iniciarPuntoAcceso() {
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_SSID, AP_PASS);
}

// Registra en el log cuando se pierde o se recupera el Wi-Fi, y reintenta conectar
void vigilarWifi() {
  static uint32_t ultimaRevision = 0, ultimoReintento = 0;
  if (!modoEstacion) return;
  if (millis() - ultimaRevision < 1000) return;
  ultimaRevision = millis();

  bool conectado = (WiFi.status() == WL_CONNECTED);
  if (conectado != wifiOk) {
    wifiOk = conectado;
    if (conectado) registrarEvento("WIFI", WiFi.localIP().toString().c_str(), "RECUPERADO");
    else           registrarEvento("WIFI", "--", "PERDIDO");
  }
  if (!conectado && millis() - ultimoReintento > 10000) {
    ultimoReintento = millis();
    WiFi.reconnect();
  }
}

// ---- Servidor web: API ----

// Convierte un float a texto JSON ("null" si no hay valor)
void numeroJson(char* salida, size_t tam, float v) {
  if (isnan(v)) snprintf(salida, tam, "null");
  else          snprintf(salida, tam, "%.2f", v);
}

// Respuesta JSON con el estado completo del sistema
void enviarEstado() {
  char temp[12];
  numeroJson(temp, sizeof(temp), tempActual);

  char json[320];
  snprintf(json, sizeof(json),
    "{\"temp\":%s,\"sensorOk\":%s,\"led\":%s,\"intervalo\":%lu,"
    "\"alarma\":%s,\"alarmaSensor\":%s,\"umbral\":%.1f,"
    "\"seq\":%lu,\"etiqueta\":\"%s\","
    "\"logOk\":%s,\"logCount\":%lu,\"uptime\":%lu}",
    temp, isnan(tempActual) ? "false" : "true",
    ledEncendido ? "true" : "false", (unsigned long)(intervaloMs / 1000),
    alarmaTemp ? "true" : "false", alarmaSensor ? "true" : "false", TEMP_ALARMA_C,
    (unsigned long)secuencia, etiquetaUltima,
    sistemaArchivosListo ? "true" : "false", (unsigned long)logTotal,
    (unsigned long)(millis() / 1000));

  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json", json);
}

void manejarPagina() {
  servidor.sendHeader("Cache-Control", "no-cache");
  servidor.send_P(200, "text/html; charset=utf-8", INDEX_HTML);
}

// Chart.js se guarda comprimido en el firmware y se envía tal cual (gzip)
void manejarChartJs() {
  servidor.sendHeader("Content-Encoding", "gzip");
  servidor.sendHeader("Cache-Control", "public, max-age=604800");
  servidor.send_P(200, "application/javascript", (PGM_P)CHARTJS_GZ, CHARTJS_GZ_LEN);
}

// Últimas lecturas, de la más antigua a la más nueva
void manejarHistorial() {
  String json = "{\"labels\":[";
  for (uint8_t i = 0; i < histCantidad; i++) {
    if (i) json += ',';
    json += '"';
    json += histEtiquetas[(histInicio + i) % HISTORIAL_N];
    json += '"';
  }
  json += "],\"values\":[";
  for (uint8_t i = 0; i < histCantidad; i++) {
    if (i) json += ',';
    char num[12];
    numeroJson(num, sizeof(num), histValores[(histInicio + i) % HISTORIAL_N]);
    json += num;
  }
  json += "],\"seq\":";
  json += String((unsigned long)secuencia);
  json += '}';
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "application/json", json);
}

void manejarEstado() { enviarEstado(); }

void manejarLed() {
  String estado = servidor.arg("state");
  if (estado != "1" && estado != "0") {
    servidor.send(400, "text/plain", "state debe ser 0 o 1");
    return;
  }
  ledEncendido = (estado == "1");
  digitalWrite(PIN_LED, ledEncendido ? HIGH : LOW);
  registrarEvento("CMD_LED", ledEncendido ? "ON" : "OFF", "OK");
  enviarEstado();
}

// Acepta solo valores entre INTERVALO_MIN_S y INTERVALO_MAX_S
void manejarIntervalo() {
  long segundos = servidor.arg("s").toInt();
  char valor[12];
  snprintf(valor, sizeof(valor), "%lds", segundos);

  if (segundos < INTERVALO_MIN_S || segundos > INTERVALO_MAX_S) {
    registrarEvento("CMD_INTERVALO", valor, "RECHAZADO");
    servidor.send(400, "text/plain", "intervalo fuera de rango (1 a 5 s)");
    return;
  }
  intervaloMs = (uint32_t)segundos * 1000UL;
  registrarEvento("CMD_INTERVALO", valor, "OK");
  enviarEstado();
}

void manejarLogVista() {
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.send(200, "text/plain; charset=utf-8", ultimasLineasLog(LOG_VISTA_N));
}

// Envía /log.csv completo como descarga
void manejarDescargaLog() {
  if (!sistemaArchivosListo || !LittleFS.exists(RUTA_LOG)) {
    servidor.send(404, "text/plain", "El log no esta disponible");
    return;
  }
  File f = LittleFS.open(RUTA_LOG, "r");
  size_t tam = f.size();
  servidor.sendHeader("Content-Disposition", "attachment; filename=\"log.csv\"");
  servidor.sendHeader("Cache-Control", "no-store");
  servidor.streamFile(f, "text/csv; charset=utf-8");
  f.close();

  char valor[16];
  snprintf(valor, sizeof(valor), "%luB", (unsigned long)tam);
  registrarEvento("DESCARGA_LOG", valor, "OK");
}

void manejarNoEncontrado() {
  if (servidor.uri() == "/favicon.ico") servidor.send(204);
  else                                  servidor.send(404, "text/plain", "No encontrado");
}

void configurarRutas() {
  servidor.on("/",              HTTP_GET,  manejarPagina);
  servidor.on("/chart.js",      HTTP_GET,  manejarChartJs);
  servidor.on("/api/status",    HTTP_GET,  manejarEstado);
  servidor.on("/api/history",   HTTP_GET,  manejarHistorial);
  servidor.on("/api/log",       HTTP_GET,  manejarLogVista);
  servidor.on("/log.csv",       HTTP_GET,  manejarDescargaLog);
  servidor.on("/api/led",       HTTP_POST, manejarLed);
  servidor.on("/api/intervalo", HTTP_POST, manejarIntervalo);
  servidor.onNotFound(manejarNoEncontrado);
  servidor.begin();
}

// ---- setup() y loop() ----

void setup() {
  Serial.begin(115200);
  delay(300);
  Serial.println();
  Serial.println("=== HMI ESP32 - Webapp embebida v" VERSION_FIRMWARE " ===");

  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, LOW);

  // Sistema de archivos del log (si es la primera vez, lo formatea)
  sistemaArchivosListo = LittleFS.begin(true);
  if (sistemaArchivosListo) {
    File f = LittleFS.open(RUTA_LOG, "r");
    size_t tam = f ? f.size() : 0;
    if (f) f.close();
    if (tam == 0) escribirEncabezadoLog();
    else          logBytes = tam;
  } else {
    Serial.println("ERROR: no se pudo montar LittleFS. El log queda desactivado.");
  }

  ds18b20.begin();
  ds18b20.setWaitForConversion(false);
  esperaConversionMs = ds18b20.millisToWaitForConversion(ds18b20.getResolution()) + 50;
  inicioConversionMs = millis() - intervaloMs;   // la primera lectura es inmediata

  modoEstacion = conectarWifi();
  if (modoEstacion) wifiOk = true;
  else              iniciarPuntoAcceso();

  registrarEvento("ARRANQUE", "v" VERSION_FIRMWARE, "OK");
  if (modoEstacion) registrarEvento("WIFI", WiFi.localIP().toString().c_str(), "CONECTADO");
  else              registrarEvento("WIFI", WiFi.softAPIP().toString().c_str(), "PUNTO_ACCESO");

  if (MDNS.begin(NOMBRE_MDNS)) MDNS.addService("http", "tcp", 80);

  configurarRutas();

  IPAddress ip = modoEstacion ? WiFi.localIP() : WiFi.softAPIP();
  Serial.print("Abre en el navegador:  http://");
  Serial.println(ip);
  Serial.printf("                  o:   http://%s.local\n", NOMBRE_MDNS);
  if (!modoEstacion) Serial.printf("Red Wi-Fi del ESP32: \"%s\"  (clave: %s)\n", AP_SSID, AP_PASS);
}

void loop() {
  servidor.handleClient();
  atenderSensor();
  vigilarWifi();
  delay(2);
}
