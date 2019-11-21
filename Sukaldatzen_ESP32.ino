#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiUdp.h>
#include <NeoPixelBus.h>
#include <SPI.h>
#include "Adafruit_MAX31855.h"
#include <EEPROM.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>
#include <base64.h>
#include <ArduinoJson.h>
#include <NTPClient.h>
#include <math.h>

///////////////////////////////UUID Bluetooth///////////////////////////

#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define SEND_WIFI_MAC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a7"
#define SSID_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"
#define PASSWORD_CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a9"

//////////////////////////Constante para tiempo sleep mode/////////////////////////////////////

#define uS_TO_S_FACTOR 1000000  /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP  5        /* Time ESP32 will go to sleep (in seconds) */

////////////////////////////Acceso a la memoria Flash///////////////////////////////////////////
class FLASHvariables {
  public:
    char ssid[50];
    char password[50];
    char mac[17];
    char mac2[17];
    int lecturacorrecta = 100;
    void save();
    void get();
    void initialize();
} variablesFlash;

void FLASHvariables::save() {
  EEPROM.put(0, variablesFlash);
  EEPROM.commit();
}

void FLASHvariables::get() {
  EEPROM.begin(sizeof(variablesFlash));
  EEPROM.get(0, variablesFlash);
}
void FLASHvariables::initialize() {
  strcpy(variablesFlash.ssid, ""); //esto en el otro archivo estaba comentado
  strcpy(variablesFlash.password, "");
  //strcpy(variablesFlash.mac,mac);
  lecturacorrecta = 190;

  EEPROM.put(0, variablesFlash);
  EEPROM.commit();
}

//////////////////////Acceso a WIFI y base de datos///////////////////////////
#define WIFI_SSID "Stirling"
#define WIFI_PASSWORD "26StR1nGoo9"

//#define WIFI_SSID "Guest"
//#define WIFI_PASSWORD "Inv12oo3"

//#define WIFI_SSID "EmbegaWifi_Lan"
//#define WIFI_PASSWORD "Wifi-Embega-123456789"

//////////////////////Informacion LED y pin Sensor capacitivo//////////////////
#define PIN_LED 35              // adafruit 17
#define NUMPIXELS      6        //adafruit 7
#define SENSOR_CAPACITIVO T0    //gpio4
#define MOSFET_LED 22

//Pines paara la comunicacion SPI con el MAX31855  //ADAFRUIT
#define MAXDO   32                                  //33
#define MAXCS   25                                  //15
#define MAXCLK  33                                  //32
#define MOSFET_MAX31855  26

////////////////////////Variables LED//////////////////////////////
/*#define AZUL 0x0000F0
#define VERDE 0x00FF00
#define NARANJA 0xFF8C00
#define ROJO 0xFF0000
#define APAGADO 0x000000*/
#define SATURACION_COLOR 255

NeoPixelBus<NeoGrbFeature, Neo800KbpsMethod> strip(NUMPIXELS, PIN_LED);
RgbColor rojo(SATURACION_COLOR, 0, 0);
RgbColor verde(0, SATURACION_COLOR, 0);
RgbColor azul(0, 0, SATURACION_COLOR);
RgbColor blanco(SATURACION_COLOR);
RgbColor apagado(0);
RgbColor naranja(255, 140, 0);

HslColor hslRed(rojo);
HslColor hslGreen(verde);
HslColor hslBlue(azul);
HslColor hslWhite(blanco);
HslColor hslBlack(apagado);

/////////////////////////Termopar, WIFI, memoria Flash//////////////////////////////
Adafruit_MAX31855 thermocouple(MAXCLK, MAXCS, MAXDO);
WiFiServer server(80);
template<class T> inline Print &operator <<(Print &obj, T arg) {
  obj.print(arg);
  return obj;
}
esp_chip_info_t chip_info; //Instantiate object chip_info of class esp_chip_info_t to access data on chip hardware

/////////////////////Variables//////////////////////////////
//Valores fijos del circuito
float rAux = 991.0;  //Raux corresponde al valor de la resistencia auxiliar.
float vcc = 3.3;       //Vcc es la tensión de alimentación del circuito potenciométrico.
float beta = 3950.0;    //Beta es la constante característica de la NTC.
float temp0 = 298.0;
float r0 = 1000.0;     //r0 es la resistencia de la NTC a la temperatura 0

//Variables usadas en el cálculo
float vm = 0.0;
float rntc = 0.0;
float temperaturaK = 0.0;

uint32_t prevTime;
int threshold = 10; //cambiar según valor de sensor capacitivo
int ledbuilt = 13;
bool touch1detected = false;
int contador_conexion_wifi = 0;
int contador_sensor_capacitivo = 0;
byte mac_b[6];
byte mac_b2[6];
char mac[17];
char mac2[17];
float i = 0;
float temp_termopar;
float temp_olla = 23.25; //tª del termopar
float temp_tapa = 0;      // tª del max
float temp_anterior = 0;
bool subir_temperatura = true;
bool temp_maxima = false;

bool apagar = false;

int mediciones = 0;
RTC_DATA_ATTR int mediciones_temp = 0;
String formattedDate;
String dayStamp;
String timeStamp;
String dateTimeStamp;
String dateTimeStampInicio;
//////////////////////////Strings para base de datos///////////////////////////////
char buff[100];
int varCor = 401;

char buf_numero_medicion[4];
char buf_numero_temperatura[4];

bool caracteristica_1 = false, caracteristica_2 = false;
///////////////////////////////Bluetooth////////////////////////////////////

class MyCallbacks: public BLECharacteristicCallbacks {
    void onWrite(BLECharacteristic *pCharacteristic) {
      std::string value = pCharacteristic->getValue();
      BLEUUID uuid = pCharacteristic->getUUID();
      std::string s_uuid = uuid.toString();
      char c_caract[132] = "";
      char c_val[100] = "";

      if (s_uuid.length() > 0) {
        for (int i = 0; i < s_uuid.length(); i++) {
          c_caract[i] = s_uuid[i];
        }
      }
      if (value.length() > 0) {
        for (int i = 0; i < value.length(); i++) {
          c_val[i] = value[i];
        }
      }

      Serial.println("******************************************************************");
      if (strcmp(c_caract, SSID_CHARACTERISTIC_UUID) == 0) {
        Serial.print("Característica SSID: ");
        Serial.println(c_caract);
        Serial.print("SSID: ");
        Serial.println(c_val);
        strcpy(variablesFlash.ssid, c_val);
        Serial.println(variablesFlash.ssid);
        caracteristica_1 = true;
      }
      if (strcmp(c_caract, PASSWORD_CHARACTERISTIC_UUID) == 0) {
        Serial.print("Característica PASSWORD: ");
        Serial.println(c_caract);
        Serial.print("PASSWORD: ");
        Serial.println(c_val);
        strcpy(variablesFlash.password, c_val);
        Serial.println(variablesFlash.password);
        caracteristica_2 = true;
      }
      Serial.println(c_caract);
      Serial.println("******************************************************************");
      if (caracteristica_1 == true && caracteristica_2 == true) {
        Serial.println("ssid y password recibidos");

        variablesFlash.save();

      }
    }
};


void conexionBluetooth() {
  Serial.println("-- Entrando en conexionBluetooth()");
  digitalWrite(ledbuilt, HIGH);
  char buf[100] = "";
  char olla[50] = "Olla Inteligente";

  sprintf(buf, "%s %s", olla, mac);
  BLEDevice::init(buf);
  BLEServer *pServer = BLEDevice::createServer();
  Serial.println("conexionBluetooth(): se crea el server bt");
  BLEService *pService = pServer->createService(SERVICE_UUID);
  Serial.println("conexionBluetooth(): Se crea el servicio BLE");
  BLECharacteristic *pCharacteristic = pService->createCharacteristic(
                                         SSID_CHARACTERISTIC_UUID,
                                         BLECharacteristic::PROPERTY_READ |
                                         BLECharacteristic::PROPERTY_WRITE
                                       );
  Serial.println("conexionBluetooth(): Se crea la caracteristica del SSID");

  BLECharacteristic *pCharacteristic2 = pService->createCharacteristic(
                                          PASSWORD_CHARACTERISTIC_UUID,
                                          BLECharacteristic::PROPERTY_READ |
                                          BLECharacteristic::PROPERTY_WRITE
                                        );
  Serial.println("conexionBluetooth(): Se crea la característica del password");

  BLECharacteristic *pCharacteristic3 = pService->createCharacteristic(
                                          SEND_WIFI_MAC_UUID,
                                          BLECharacteristic::PROPERTY_READ |
                                          BLECharacteristic::PROPERTY_WRITE
                                        );
  Serial.println("conexionBluetooth(): Se crea la caracteristica para enviar MAC WIFI");

  pCharacteristic->setCallbacks(new MyCallbacks());
  pCharacteristic2->setCallbacks(new MyCallbacks());
  pCharacteristic3->setCallbacks(new MyCallbacks());
  Serial.println("conexionBluetooth(): Se setean los callbacks de las caracteristicas");

  Serial.println("Se obtiene dirección MAC en conexionBluetooth()");
  Serial.println(" ");
  WiFi.mode(WIFI_MODE_STA);
  WiFi.macAddress(mac_b2);
  sprintf(mac2, "%02x:%02x:%02x:%02x:%02x:%02x", mac_b2[0], mac_b2[1], mac_b2[2], mac_b2[3], mac_b2[4], mac_b2[5]);
  Serial.print("MAC2: ");
  Serial.println(mac2);
  Serial.println(" ");

  //Se setean los valores de las características a vacío e iniciamos el service
  pCharacteristic->setValue("");
  pCharacteristic2->setValue("");
  pService->start();

  //Advertising: que otros dispositivos puedan verlo
  BLEAdvertising *pAdvertising = pServer->getAdvertising();
  pAdvertising->start();
}
//////////////////////Inicio de la aplicación////////////////////////
long t1 = 0, t2 = 0;

void setup() {
  // put your setup code here, to run once:
  //touchAttachInterrupt(SENSOR_CAPACITIVO, gotTouch1, threshold);// estaba comentado
  pinMode (ledbuilt, OUTPUT);
  t1 = millis();
  esp_chip_info(&chip_info);
  Serial.begin(115200);
  //analogReference(EXTERNAL);
  strip.Begin();
  //strip.setBrightness(40); // 1/3 brightness
  prevTime = millis();
  pinMode(MOSFET_MAX31855, OUTPUT);
  pinMode(MOSFET_LED, OUTPUT);
  digitalWrite(MOSFET_LED, HIGH);

  variablesFlash.get();
  //variablesFlash.initialize();
#if defined (__AVR_ATtiny85__)
  if (F_CPU == 80000000) clock_prescale_set(clock_div_1);
#endif

  boolean despertado_temporizador = false;

  if (get_wakeup_reason() == ESP_SLEEP_WAKEUP_TIMER) despertado_temporizador = true;

  Serial.println("-- Se llama a conexionBluetooth() desde el if del comienzo de la aplicacion");
  conexionBluetooth();
  while (!conexionWIFI(despertado_temporizador)) {

  }
  Serial.println("-- Se llama a desconectarBluetooth() desde void setup()");
  desconectarBluetooth();
  sprintf(buff, "Cazuelas/%s/Estado", mac);
  bool med = false;
  int contador = 0;

  sprintf(buff, "Cazuelas/%s/Mediciones/Numero mediciones", mac);

  int num_cocciones = 0;
  t1 = millis();

  // Serial.print("Numero de cocciones:  ");
  //Serial.println(num_cocciones);
  Serial.println("RAZON de encendido");
  switch (get_wakeup_reason()) {
    case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER :
      mediciones = num_cocciones;
      Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD :
      mediciones_temp = 0;
      mediciones = num_cocciones + 1;
      Serial.println("Wakeup caused by touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;
    default :
      mediciones_temp = 0;
      mediciones = num_cocciones + 1;
      Serial.println("Ni idea");
      break;
  }
  leerTemperatura();
  actualizarBD(1);
  touchAttachInterrupt(SENSOR_CAPACITIVO, gotTouch1, threshold);

  //leerTemperatura();
  //Serial.println("CLEARDATA");
  Serial.println("LABEL, Tiempo encendido");
}

void desconectarBluetooth() {
  digitalWrite(ledbuilt, LOW);
  BLEDevice::deinit(true);
  Serial.println("-- Desconectar bluetooth ejecutado");
}

esp_sleep_wakeup_cause_t get_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();
  Serial.println(wakeup_reason);
  return wakeup_reason;
}

/////////////////////////Bucle////////////////////////////////

void loop() {
  Serial.println("-- Entramos en el bucle de lectura de temperaturas");
  deteccionSensorCapacitivo();
  leerTemperatura();
    actualizarBD(2);
  t1 = millis();
  if (temp_olla >= 141) {
    temp_maxima = true;
  }
  while (temp_maxima) {
    animacionLEDtemperaturaMaxima();
  }
  colores_led();
  temp_anterior = temp_olla;
  deteccionSensorCapacitivo();
  t2 = millis();
  while (millis() - t2 < 10000) {

  }
}

//////////////////////////////////////////////////////////////
void animacionLEDtemperaturaMaxima() {
  establecer_color_led(apagado);
  delay(100);
  establecer_color_led(rojo);
  delay(100);
}

/////////////////////////Funcion interrupcion timer///////////////////
void dormirConTemporizador() {
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +
                 " Seconds");
  Serial.println("Going to sleep now ZZZZZZzzzzzzzZZZZZZZZzzzzzzzz");
  Serial.flush();
  t2 = millis();
  long t = t2 - t1;
  Serial.print("Tiempo: ");
  //Serial.print("DATA,");
  Serial.println(t);
  //deteccionSensorCapacitivo();
  esp_deep_sleep_start();
  Serial.println("This will never be printed");
}
///////////////////////////Funciones para interrupciones////////////////////////

void gotTouch1() {
  if (temp_maxima) {
    temp_maxima = false;
  }
  touch1detected = true;
}

void callback() {
  //placeholder callback function
}

/////////////////////////////WIFI/////////////////////////////////

bool conexionWIFI(boolean temporizador) {
  //WiFi.begin(variablesFlash.ssid, variablesFlash.password);
  WiFi.begin(WIFI_SSID,WIFI_PASSWORD); //Para pruebas
  Serial.println("Conectando a red WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    Serial.print(".");
    if (!temporizador) {
      if (contador_conexion_wifi % 2 == 0) {
        establecer_color_led(azul);
      } else {
        establecer_color_led(apagado);
      }
    }
    delay(100);
    if (contador_conexion_wifi == 10) {
      contador_conexion_wifi = 0;
      return false;
    }
    contador_conexion_wifi++;

  }
  establecer_color_led(verde);
  Serial.println();
  Serial.print("connected: ");
  Serial.println(WiFi.localIP());

  Serial.printf("Se obtiene dirección MAC en conexionWIFI()");
  WiFi.macAddress(mac_b);

  sprintf(mac, "%02x:%02x:%02x:%02x:%02x:%02x", mac_b[0], mac_b[1], mac_b[2], mac_b[3], mac_b[4], mac_b[5]);
  Serial.print("MAC:");
  Serial.println(mac);
  return true;
}

///////////////////////////////////////Detección sensor capacitivo////////////////////////////////////////////

void deteccionSensorCapacitivo() {
  while (touch1detected) {
    delay(100);
    if (contador_sensor_capacitivo > 1) {
      if (!apagar) {
        if (contador_sensor_capacitivo % 2 == 0) {
          establecer_color_led(azul);
        } else {
          establecer_color_led(apagado);
        }
      }
    }
    if (touchRead(SENSOR_CAPACITIVO) > threshold) {
      touch1detected = false;

    } else {
      touch1detected = true;
      Serial.println("apagando");
      contador_sensor_capacitivo++;
    }

    if (contador_sensor_capacitivo == 15) {
      establecer_color_led(apagado);
      apagar = true;
    }
  }
  if (apagar) {
    sprintf(buff, "Cazuelas/%s/Estado", mac);
    leerTemperatura();
    actualizarBD(3);
    establecer_color_led(apagado);
    Serial.println("Apagando modulo");
    touchAttachInterrupt(SENSOR_CAPACITIVO, callback, threshold);
    esp_sleep_enable_touchpad_wakeup();
    esp_deep_sleep_start();

    colores_led();
  } else {

    colores_led();
  }
  contador_sensor_capacitivo = 0;
  apagar = false;
}

////////////////////////////////////////Leer temperatura del termopar//////////////////////////////////

void leerTemperatura() {
//Bloque de cálculo
  vm=(vcc / 1024)*( analogRead(0) );                //Calcular tensión en la entrada
  rntc = rAux / ((vcc/vm)-1);                       //Calcular la resistencia de la NTC
  temperaturaK = beta/(log(rntc/r0)+(beta/temp0));  //Calcular la temperatura en Kelvin

  //Restar 273 para pasar a grados celsus
  //Serial.println(temperaturaK - 273);
  
//  digitalWrite(MOSFET_MAX31855, HIGH);
//  double t = millis();
//  while (millis() - t < 100); //tiempo de espera de 5 ms para dar tiempo a activar el mosfet
  temp_olla = temperaturaK - 273;//thermocouple.readCelsius(); //termopar
  temp_tapa = thermocouple.readInternal(); //interna del ic max
  //digitalWrite(MOSFET_MAX31855, LOW);
}

//////////////////////////////////////Establecer color LED////////////////////////////////////////////
void colores_led() {

  if (temp_olla >= 0 && temp_olla < 100) {
    establecer_color_led(verde);
  } else if (temp_olla >= 100 && temp_olla < 120 ) {
    establecer_color_led(naranja);
  } else if (temp_olla >= 120) {
    establecer_color_led(rojo);
  }
}

void establecer_color_led(RgbColor color) {

  for (int i = 0; i < NUMPIXELS; i++) {
    strip.SetPixelColor(i, color);
  }
  strip.Show();
}
//////////////////////////Pasar numero a String////////////////////////////////////
void numero_medicion_string() {
  sprintf(buf_numero_medicion, "%s", "");
  if (mediciones >= 10) {
    if (mediciones >= 100) {
      if (mediciones >= 1000) {
        sprintf(buf_numero_medicion, "%d", mediciones);
      } else {
        sprintf(buf_numero_medicion, "%d%d", 0, mediciones);
      }
    } else {
      sprintf(buf_numero_medicion, "%d%d%d", 0, 0, mediciones);
    }
  } else {
    sprintf(buf_numero_medicion, "%d%d%d%d", 0, 0, 0, mediciones);
  }
  //Serial.println(buf_numero_medicion);
}
void numero_temperatura_medida() {
  //sprintf(buf_numero_temperatura, "%s","");
  // Serial.print("1");Serial.println(buf_numero_temperatura);
  if (mediciones_temp >= 10) {
    if (mediciones_temp >= 100) {
      if (mediciones_temp >= 1000) {
        sprintf(buf_numero_temperatura, "%d", mediciones_temp);
      } else {
        sprintf(buf_numero_temperatura, "%d%d", 0, mediciones_temp);
      }
    } else {
      sprintf(buf_numero_temperatura, "%d%d%d", 0, 0, mediciones_temp);
    }
  } else {
    sprintf(buf_numero_temperatura, "%d%d%d%d", 0, 0, 0, mediciones_temp);
  }
  // Serial.print("2");
  //Serial.println(buf_numero_temperatura);
}
//////////////Obtener fecha y hora de NTP////////////////
void obtenerNTP() {
  WiFiUDP ntpUDP;
  NTPClient timeClient(ntpUDP);
  timeClient.begin();
  timeClient.setTimeOffset(0); //GMT +1 //quito lo de 3600
  while (!timeClient.update()) {
    timeClient.forceUpdate();
  }
  formattedDate = timeClient.getFormattedDate();
  int splitT = formattedDate.indexOf("T"); //Partimos la fecha y la hora
  dayStamp = formattedDate.substring(0, splitT); //fecha
  timeStamp = formattedDate.substring(splitT + 1, formattedDate.length() - 1); //hora
  //Ahora nos queda juntar las dos con un espacio (fecha y hora)
  dateTimeStamp = dayStamp + " " + timeStamp;
  Serial.println("dateTimeStamp obtenida en obtenertNTP(): " + dateTimeStamp);
}
/////////////////////////////////////Actualización de la base de datos///////////////////////////////
void actualizarBD(int tipo) {
  
  Serial.println("-- Entrando en actualizarBD() " + tipo);
  String authUsername = "android";
  String authPassword = "Becario2017";
  String medicFFin = " ";
  //codificamos en base64
  String auth = base64::encode(authUsername + ":" + authPassword);
  //Creamos objeto JSON a enviar
  const int capacity = JSON_OBJECT_SIZE(12);
  StaticJsonBuffer<capacity> jb;
  obtenerNTP(); //obtenemos la hora desde internet
  if(tipo == 1){
    dateTimeStampInicio = dateTimeStamp;
    medicFFin = (char*)0; //null
  }
  if(tipo == 2){
    medicFFin =  (char*)0;
  }
  if(tipo == 3){
    medicFFin = dateTimeStamp;
  }
  JsonObject& json = jb.createObject(); //JsonObject& json = buffer.createObject(); //jsonBuffer
  json["idMac"] = WiFi.macAddress();
  json["medicionFechaInicio"] = dateTimeStampInicio;
  json["medicionFechaFin"] = medicFFin;
  json["timestamp"] = dateTimeStamp;
  json["tempsInt"] = temp_olla;//temp_olla;
  json["tempsTapa"] = temp_tapa;//temp_tapa;
  Serial.print("El JSON que hemos generado: ");
  json.printTo(Serial);
  Serial.println("");
  String payload=" ";
  json.printTo(payload);
  Serial.println("========> La payload que se va a enviar: "  + payload);
  String payloadV = " ";
  HTTPClient http;
  http.begin("http://10.128.0.104:9200/mediciones_sukaldatzen/_doc");//separado por comas no compila
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", "Basic " + auth);
  int httpCode = http.POST(payload); //payload es la información que deseamos enviar
  if (httpCode > 0) { //Chequeamos qué código devuelve
    String payloadV = http.getString();
    Serial.println("Código HTTP: " + httpCode);
    Serial.println("Respuesta: " + payloadV); //payloadV es la información que obtenemos
  } else {
    Serial.println("Error en la petición HTTP");
  }
  http.end();
  mediciones_temp++;
}
