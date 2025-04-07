#include <Arduino.h>
#include <DHT.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>

// ----- Sensor & Motor Pin Definitions -----
#define DHTPIN 27
#define DHTTYPE DHT11
#define MQ135_PIN 34
#define FLAME_SENSOR_PIN 33
#define MOTOR_PWM_PIN 14
#define RL_VALUE 5  // Load resistance (kΩ)

// ----- Objects and Globals -----
DHT dht(DHTPIN, DHTTYPE);
WebServer server(80);
WebSocketsServer webSocket(81);
float R0 = 10.00;

// ----- Wi-Fi AP Credentials -----
const char* ssid = "Jet Engine";
const char* password = "12345678";

// ----- AQI Breakpoints -----
float breakpoints_CO2[][2]      = {{0, 0}, {1000, 50}, {2000, 100}, {3000, 150}, {4000, 200}, {5000, 300}, {6000, 400}, {7000, 500}};
float breakpoints_NH3[][2]      = {{0, 0}, {100, 50}, {200, 100}, {300, 150}, {400, 200}, {500, 300}, {600, 400}, {700, 500}};
float breakpoints_NOx[][2]      = {{0, 0}, {50, 50}, {100, 100}, {150, 150}, {200, 200}, {300, 300}, {400, 400}, {500, 500}};
float breakpoints_Alcohol[][2]  = {{0, 0}, {50, 50}, {100, 100}, {150, 150}, {200, 200}, {300, 300}, {400, 400}, {500, 500}};
float breakpoints_Benzene[][2]  = {{0, 0}, {1.6, 50}, {3.2, 100}, {4.8, 150}, {6.4, 200}, {8.0, 300}, {9.6, 400}, {11.2, 500}};
float breakpoints_Smoke[][2]    = {{0, 0}, {50, 50}, {100, 100}, {150, 150}, {200, 200}, {300, 300}, {400, 400}, {500, 500}};

// ----- Utility Functions -----

float calculateResistance(int adcVal) {
  return ((4095.0 / adcVal) - 1.0) * RL_VALUE;
}

float calculateConcentration(float ratio, float slope, float intercept) {
  return pow(10, ((log10(ratio) - intercept) / slope));
}

float calculateAQI(float conc, float breakpoints[][2], int len) {
  for (int i = 0; i < len - 1; i++) {
    if (conc >= breakpoints[i][0] && conc < breakpoints[i + 1][0]) {
      float AQI_low = breakpoints[i][1], AQI_high = breakpoints[i + 1][1];
      float conc_low = breakpoints[i][0], conc_high = breakpoints[i + 1][0];
      return ((AQI_high - AQI_low) / (conc_high - conc_low)) * (conc - conc_low) + AQI_low;
    }
  }
  return -1;
}

bool handleFileRead(String path) {
  if (path.endsWith("/")) path += "index.html";
  String contentType = "text/plain";
  if (path.endsWith(".html")) contentType = "text/html";
  else if (path.endsWith(".css")) contentType = "text/css";
  else if (path.endsWith(".js")) contentType = "application/javascript";

  File file = LittleFS.open(path, "r");
  if (file) {
    server.streamFile(file, contentType);
    file.close();
    return true;
  }
  return false;
}

// ----- WebSocket Event Handler -----
void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  if (type == WStype_CONNECTED) {
    IPAddress ip = webSocket.remoteIP(num);
    Serial.printf("[%u] Connected: %s\n", num, ip.toString().c_str());
    webSocket.sendTXT(num, "Connected to ESP32 WebSocket");
  }
  else if (type == WStype_TEXT) {
    String message = String((char*)payload);
    if (message.startsWith("MOTOR_SPEED:")) {
      int speed = message.substring(12).toInt();  // % from slider (0–100)
      int pulseWidth = map(speed, 0, 100, 1000, 2000);  // µs pulse for ESC
      int duty = (pulseWidth * 65535) / 20000;  // Duty cycle for 50 Hz
      ledcWrite(0, duty);
      Serial.printf("ESC Pulse: %d µs (Speed: %d%%, Duty: %d)\n", pulseWidth, speed, duty);
    }
  }
}

// ----- Sensor Data Collection and WebSocket Broadcast -----
void updateSensorData() {
  // MQ135 readings
  int sensorValue = analogRead(MQ135_PIN);
  float Rs = calculateResistance(sensorValue);
  float ratio = Rs / R0;

  // PPM Estimations
  float ppm_CO2      = calculateConcentration(ratio, -0.42, 0.37);
  float ppm_NH3      = calculateConcentration(ratio, -0.45, 0.35);
  float ppm_NOx      = calculateConcentration(ratio, -0.40, 0.30);
  float ppm_Alcohol  = calculateConcentration(ratio, -0.35, 0.40);
  float ppm_Benzene  = calculateConcentration(ratio, -0.42, 0.29);
  float ppm_Smoke    = calculateConcentration(ratio, -0.50, 0.31);

  float AQI_CO2      = calculateAQI(ppm_CO2, breakpoints_CO2, 8);
  float AQI_NH3      = calculateAQI(ppm_NH3, breakpoints_NH3, 8);
  float AQI_NOx      = calculateAQI(ppm_NOx, breakpoints_NOx, 8);
  float AQI_Alcohol  = calculateAQI(ppm_Alcohol, breakpoints_Alcohol, 8);
  float AQI_Benzene  = calculateAQI(ppm_Benzene, breakpoints_Benzene, 8);
  float AQI_Smoke    = calculateAQI(ppm_Smoke, breakpoints_Smoke, 8);

  float finalAQI = max({AQI_CO2, AQI_NH3, AQI_NOx, AQI_Alcohol, AQI_Benzene, AQI_Smoke});

  // DHT11 readings
  float temp = dht.readTemperature();
  float humidity = dht.readHumidity();
  if (isnan(temp) || isnan(humidity)) return;

  // Flame sensor
  int flame = digitalRead(FLAME_SENSOR_PIN);

  // Compose and send sensor data
  String data = "Temp:" + String(temp) +
                ",Humidity:" + String(humidity) +
                ",AQI:" + String(finalAQI) +
                ",NH3:" + String(ppm_NH3) +
                ",NOx:" + String(ppm_NOx) +
                ",Alcohol:" + String(ppm_Alcohol) +
                ",Benzene:" + String(ppm_Benzene) +
                ",Smoke:" + String(ppm_Smoke) +
                ",CO2:" + String(ppm_CO2) +
                ",Flame:" + String(flame);

  webSocket.broadcastTXT(data);
}

// ----- Setup -----
void setup() {
  Serial.begin(115200);
  dht.begin();
  pinMode(MQ135_PIN, INPUT);
  pinMode(FLAME_SENSOR_PIN, INPUT);

  // Motor PWM setup
  ledcSetup(0, 50, 16);  // 50 Hz, 16-bit resolution
  ledcAttachPin(MOTOR_PWM_PIN, 0);
  ledcWrite(0, (1000 * 65535) / 20000);  // Start with 1000 µs pulse (off)
  delay(2000);

  // LittleFS
  if (!LittleFS.begin()) {
    Serial.println("LittleFS mount failed!");
    return;
  }

  // Wi-Fi Access Point setup
  IPAddress local_IP(192, 168, 1, 125);
  IPAddress gateway(192, 168, 0, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);
  Serial.println("WiFi AP Started: " + WiFi.softAPIP().toString());

  // Start WebSocket
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  // Start HTTP server
  server.on("/", HTTP_GET, []() {
    if (!handleFileRead("/index.html")) {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.onNotFound([]() {
    if (!handleFileRead(server.uri())) {
      server.send(404, "text/plain", "Not Found");
    }
  });

  server.begin();
}

// ----- Loop -----
void loop() {
  webSocket.loop();
  server.handleClient();
  updateSensorData();  // Every 1 second
  delay(1000);
}
