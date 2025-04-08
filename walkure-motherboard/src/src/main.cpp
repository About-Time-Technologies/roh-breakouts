

#define DEBUG  // Uncomment this line to enable debugging messages

#ifdef DEBUG
#define DEBUG_PRINT(x)    Serial.print(x)
#define DEBUG_PRINTLN(x)  Serial.println(x)
#define DEBUG_PRINTF(...) Serial.printf(__VA_ARGS__)
#else
#define DEBUG_PRINT(x)
#define DEBUG_PRINTLN(x)
#define DEBUG_PRINTF(...)
#endif

#include <Arduino.h>
#include <math.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>

#include <ElegantOTA.h>

// PWM Configuration
const uint8_t pwmChannel = 1;
const uint8_t pwmResolution = 8; // PWM resolution of 16 bits
const uint16_t pwmThreshold = 0;  // Low Value Threshold for PWM output


// Pins
const uint8_t fanPin = FAN_PIN1;
const uint8_t fanMax = 108;
const uint8_t fanMin = 100;

unsigned long lastUpdate = 0;
unsigned long updateInterval = 2500;

OneWire tempPCB(TEMP_PCB);
OneWire tempAir(TEMP_AIR);

DallasTemperature sensorPCB(&tempPCB);
DallasTemperature sensorAir(&tempAir);

DeviceAddress addressPCB, addressAir;

float tempPCB_C, tempAir_C;
String tempPCB_Cs = "", tempAir_Cs = "";
bool tempPCB_found = true, tempAir_found = true;

// PWM frequency variable
uint16_t pwmFreq = 25000;


AsyncWebServer server(80);
const char* ssid = "W-Streetlight-1-Temp";
const char* password = "94499449";
unsigned long ota_progress_millis = 0;
IPAddress local_IP(192, 168, 1, 1);
IPAddress gateway(192, 168, 1, 1);
IPAddress subnet(255, 255, 0, 0);

const String localIPURL = "http://192.168.1.1";

const char index_html[] PROGMEM = R"rawliteral(
<!DOCTYPE HTML><html>
<head>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <link rel="stylesheet" href="https://use.fontawesome.com/releases/v5.7.2/css/all.css" integrity="sha384-fnmOCqbTlWIlj8LyTjo7mOUStjsKC4pOpQbqyi7RrhN7udi9RwhKkMHpvLbHG9Sr" crossorigin="anonymous">
  <style>
    html {
     font-family: Arial;
     display: inline-block;
     margin: 0px auto;
     text-align: center;
    }
    h2 { font-size: 3.0rem; }
    p { font-size: 3.0rem; }
    .units { font-size: 1.2rem; }
    .ds-labels{
      font-size: 1.5rem;
      vertical-align:middle;
      padding-bottom: 15px;
    }
  </style>
</head>
<body>
  <h2>Walkure LED Array Monitoring</h2>
  <p>
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="ds-labels">PCB Temperature</span> 
    <span id="tempPCB">%tempPCB%</span>
    <sup class="units">&deg;C</sup>
  </p>
  <p>`
    <i class="fas fa-thermometer-half" style="color:#059e8a;"></i> 
    <span class="ds-labels">Air Temperature</span>
    <span id="tempAir">%tempAir%</span>
    <sup class="units">&deg;C</sup>
  </p>
</body>
<script>
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("tempPCB").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/tempPCB", true);
  xhttp.send();
}, 2000) ;
setInterval(function ( ) {
  var xhttp = new XMLHttpRequest();
  xhttp.onreadystatechange = function() {
    if (this.readyState == 4 && this.status == 200) {
      document.getElementById("tempAir").innerHTML = this.responseText;
    }
  };
  xhttp.open("GET", "/tempAir", true);
  xhttp.send();
}, 2000) ;
</script>
</html>)rawliteral";

void setupFanPWM() {
    // Set up the PWM channels for Fans
    ledcSetup(pwmChannel, pwmFreq, pwmResolution);
    ledcAttachPin(fanPin, pwmChannel);

    // Now we can read the actual PWM frequency
    DEBUG_PRINTF("Initial PWM frequency: %d Hz\n", ledcReadFreq(pwmChannel));
    
    ledcWrite(pwmChannel, fanMax);
}

void setupTempSensors() {
    // Start temperature sensors
    sensorAir.begin();
    sensorPCB.begin();

    DEBUG_PRINTF("Air temp device count: %i\n", sensorAir.getDeviceCount());
    DEBUG_PRINTF("         parasitic on: %s\n", sensorAir.isParasitePowerMode() ? "on" : "off");
    if (!sensorAir.getAddress(addressAir, 0)) { 
        DEBUG_PRINTLN("Unable to find air temperature sensor");
        tempAir_C = 40.0f;
        tempAir_found = false;
    }
    sensorAir.setResolution(addressAir, 9);

    DEBUG_PRINTF("PCB temp device count: %i\n", sensorPCB.getDeviceCount());
    DEBUG_PRINTF("         parasitic on: %s\n", sensorPCB.isParasitePowerMode() ? "on" : "off");

    if (!sensorPCB.getAddress(addressPCB, 0)) { 
        DEBUG_PRINTLN("Unable to find PCB temperature sensor");
        tempPCB_C = 40.0f;
        tempPCB_found = false;
    }
    sensorPCB.setResolution(addressPCB, 9);

    DEBUG_PRINTLN("Temp Sensors Initialised");
}

String webProcessor(const String& var) {
    if (var == "tempPCB") {
        return String(tempPCB_C);
    }

    if (var == "tempAir") {
        return String(tempAir_C);
    }

    return String();
}

void onOTAStart() {
    DEBUG_PRINTLN("OTA Update Started");
}
void onOTAProgress(size_t current, size_t final) {
    if (millis() - ota_progress_millis > 1000) {
        ota_progress_millis = millis();
        DEBUG_PRINTF("OTA Progress Current: %u bytes, Final: %u bytes\n\r", current, final);
    }
}

void onOTAEnd(bool success) {
    if (success) {
        DEBUG_PRINTLN("OTA Updated Successfully");
    } else {
        DEBUG_PRINTLN("OTA Update Failed");
    }
}

void startAP() {
    if (WiFi.getMode() != WIFI_OFF) return;

    DEBUG_PRINTLN("Starting OTA mode");
    WiFi.softAPConfig(local_IP, gateway, subnet);
    WiFi.softAP(ssid, password, 6, 1, 3);

    IPAddress IP = WiFi.softAPIP();
    DEBUG_PRINTLN(IP);

    ElegantOTA.begin(&server);
    ElegantOTA.onStart(onOTAStart);
    ElegantOTA.onEnd(onOTAEnd);
    ElegantOTA.onProgress(onOTAProgress);

    server.begin();
}

void stopAP() {
    if (WiFi.getMode() == WIFI_OFF) return;

    DEBUG_PRINTLN("Stopping OTA mode");
    server.end();
    WiFi.disconnect();
    WiFi.mode(WIFI_OFF);
}

void setupWeb() {
    // Required
	server.on("/connecttest.txt", [](AsyncWebServerRequest *request) { request->redirect("http://logout.net"); });	// windows 11 captive portal workaround
	server.on("/wpad.dat", [](AsyncWebServerRequest *request) { request->send(404); });								// Honestly don't understand what this is but a 404 stops win 10 keep calling this repeatedly and panicking the esp32 :)

	// Background responses: Probably not all are Required, but some are. Others might speed things up?
	// A Tier (commonly used by modern systems)
	server.on("/generate_204", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });		   // android captive portal redirect
	server.on("/redirect", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });			   // microsoft redirect
	server.on("/hotspot-detect.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });  // apple call home
	server.on("/canonical.html", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });	   // firefox captive portal call home
	server.on("/success.txt", [](AsyncWebServerRequest *request) { request->send(200); });					   // firefox captive portal call home
	server.on("/ncsi.txt", [](AsyncWebServerRequest *request) { request->redirect(localIPURL); });			   // windows call home

    // return 404 to webpage icon
	server.on("/favicon.ico", [](AsyncWebServerRequest *request) { request->send(404); });	// webpage icon

    // Route for root / web page
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/html", index_html, webProcessor);
    });
    server.on("/tempAir", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", String(tempAir_C).c_str());
    });
    server.on("/tempPCB", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send(200, "text/plain", String(tempPCB_C).c_str());
    });

    // the catch all
	server.onNotFound([](AsyncWebServerRequest *request) {
		request->send(200, "text/html", index_html, webProcessor);
	});

    DEBUG_PRINTLN("Web processor initialised");
}


void setup() {
    #ifdef DEBUG
    Serial.begin(115200);
    #endif
    WiFi.mode(WIFI_OFF);

    delay(1000);


    setupFanPWM();
    setupTempSensors();
    setupWeb();
    startAP();
}

void readTempAir() {
    if (tempAir_found) {
        sensorAir.requestTemperatures();

        tempAir_C = sensorAir.getTempC(addressAir);
    }
}

void readTempPCB() {
    if (tempPCB_found) {
        sensorPCB.requestTemperatures();

        tempPCB_C = sensorPCB.getTempC(addressPCB);
    }
}

void loop() {
    if ((millis() - lastUpdate) > updateInterval) {
        readTempAir();
        readTempPCB();

        DEBUG_PRINTF("Air temp: %f\n", tempAir_C);
        DEBUG_PRINTF("PCB temp: %f\n", tempPCB_C);

        lastUpdate = millis();
    }
}
