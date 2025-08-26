#include <WiFi.h>          // Conexión Wi-Fi
#include <PubSubClient.h>  // Cliente MQTT

// <<< ADICIONADO >>>
#include <OneWire.h>
#include <DallasTemperature.h>
// <<< FIN ADICIONADO >>>

// --- Configuración de Wi-Fi ---
const char* ssid = "Fibertel SARA 2.4GHz";
const char* password = "ferbasso";

// --- Configuración del Broker MQTT ---
const char* mqtt_broker_ip = "192.168.0.239";  // IP de tu broker (PC o Raspberry Pi)
const int mqtt_port = 1883;
const char* mqtt_user = "ferca27";
const char* mqtt_password = "1234";
const char* mqtt_client_id = "ESP32_Cliente_Piscina";

// --- Topics MQTT ---
const char* topic_publish = "piscina/datos";
const char* topic_subscribe = "piscina/control/bomba";

// --- Pin del LED (o relé simulando la bomba) ---
const int LED_PIN = 15;

// <<< ADICIONADO: Pines y objetos sensores >>>
#define ONE_WIRE_BUS 4   // Pin de datos del DS18B20 (ajusta si usas otro)
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);

#define TRIG_PIN 5
#define ECHO_PIN 18
// <<< FIN ADICIONADO >>>

// --- Objetos Wi-Fi y MQTT ---
WiFiClient espClient;
PubSubClient client(espClient);

// --- Variables de tiempo ---
long lastMsg = 0;

// -------------------------------------------------------------------
// setup(): Se ejecuta al inicio
// -------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  Serial.println("\n--- ESP32 como Cliente MQTT ---");

  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);  // Apagado por defecto

  // <<< ADICIONADO: inicialización sensores >>>
  sensors.begin();               // DS18B20
  pinMode(TRIG_PIN, OUTPUT);     // HC-SR04
  pinMode(ECHO_PIN, INPUT);
  Serial.println("Sensores inicializados.");
  // <<< FIN ADICIONADO >>>

  setup_wifi();  // Conexión Wi-Fi

  client.setServer(mqtt_broker_ip, mqtt_port);
  client.setCallback(callback);  // Función que se llama al recibir mensajes
}

// -------------------------------------------------------------------
// setup_wifi(): Conecta el ESP32 al Wi-Fi
// -------------------------------------------------------------------
void setup_wifi() {
  delay(10);
  Serial.print("Conectando a WiFi: ");
  Serial.println(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi conectado.");
  Serial.print("IP local: ");
  Serial.println(WiFi.localIP());
}

// -------------------------------------------------------------------
// callback(): Se ejecuta al recibir un mensaje MQTT
// -------------------------------------------------------------------
void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Mensaje recibido en [");
  Serial.print(topic);
  Serial.print("]: ");

  String receivedMessage;

  for (int i = 0; i < length; i++) {
    char c = (char)payload[i];
    Serial.print(c);
    receivedMessage += c;
  }
  Serial.println();

  receivedMessage.trim();  // Elimina espacios o saltos

  if (String(topic) == topic_subscribe) {
    if (receivedMessage == "Encendido") {
      Serial.println("→ Encendiendo bomba (LED ON)");
      digitalWrite(LED_PIN, HIGH);
    } else if (receivedMessage == "Apagado") {
      Serial.println("→ Apagando bomba (LED OFF)");
      digitalWrite(LED_PIN, LOW);
    } else {
      Serial.println("→ Comando no reconocido.");
    }
  }
}

// -------------------------------------------------------------------
// reconnect(): Reintenta la conexión MQTT si se pierde
// -------------------------------------------------------------------
void reconnect() {
  while (!client.connected()) {
    Serial.print("Conectando al broker MQTT (");
    Serial.print(mqtt_broker_ip);
    Serial.print(")... ");

    if (client.connect(mqtt_client_id, mqtt_user, mqtt_password)) {
      Serial.println("¡Conectado!");

      client.subscribe(topic_subscribe);
      Serial.print("Suscripto a: ");
      Serial.println(topic_subscribe);

      client.publish(topic_publish, "ESP32 conectado al broker");
    } else {
      Serial.print("Falló. Código: ");
      Serial.print(client.state());
      Serial.println(" → Reintentando en 5 segundos...");
      delay(5000);
    }
  }
}

// -------------------------------------------------------------------
// loop(): Bucle principal
// -------------------------------------------------------------------
void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  long now = millis();
  if (now - lastMsg > 30000) {
    lastMsg = now;

    // <<< ADICIONADO: leer sensores reales >>>

    // ---- Leer temperatura DS18B20 ----
    sensors.requestTemperatures();
    float tempC = sensors.getTempCByIndex(0); // devuelve float
    int temperatura;
    if (tempC == DEVICE_DISCONNECTED_C) {
      Serial.println("Error: DS18B20 desconectado!");
      temperatura = 0; // valor por defecto si falla
    } else {
      temperatura = (int)round(tempC); // redondear a int para mantener formato anterior
    }

    // ---- Leer distancia HC-SR04 ----
    long duracion;
    float distancia_cm;

    digitalWrite(TRIG_PIN, LOW);
    delayMicroseconds(2);

    digitalWrite(TRIG_PIN, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG_PIN, LOW);

    duracion = pulseIn(ECHO_PIN, HIGH, 30000); // timeout 30 ms
    if (duracion == 0) {
      // no hubo eco (posible fallo o objeto fuera de rango)
      distancia_cm = -1;
      Serial.println("Warning: no se detectó pulso en HC-SR04 (fuera de alcance o cableado).");
    } else {
      distancia_cm = duracion * 0.0343 / 2.0;  // Velocidad del sonido (cm/µs)
    }

    int nivelAgua;
    if (distancia_cm < 0) {
      nivelAgua = 0; // fallback
    } else {
      nivelAgua = (int)round(distancia_cm); // envío en cm (entero)
    }

    // ---- Mostrar resultados ----
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.print(" °C | Distancia: ");
    if (distancia_cm < 0) Serial.print("N/A"); else Serial.print(distancia_cm);
    Serial.println(" cm");

    // Crear mensaje JSON (igual estructura que antes)
    String message = "{";
    message += "\"temperatura\":" + String(temperatura) + ",";
    message += "\"nivel_agua_cm\":" + String(nivelAgua);
    message += "}";

    Serial.print("Publicando en [");
    Serial.print(topic_publish);
    Serial.print("]: ");
    Serial.println(message);

    client.publish(topic_publish, message.c_str());

    // <<< FIN ADICIONADO >>>
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado. Reintentando...");
    setup_wifi();
  }
}
