#include <ESP8266WiFi.h>
#include <ArduinoOTA.h>
#include <SoftwareSerial.h> 
#include <ModbusMaster.h>
#include <Ticker.h>
#include <ESPAsyncWebServer.h>
#include <ESPDash.h>
#include "AsyncJson.h"
#include "ArduinoJson.h"
#include "credentials.h"

SoftwareSerial energyMeter(D7, D8, false);

AsyncWebServer server(80);

ModbusMaster node;

double voltage_usage = 0.0, current_usage = 0.0, active_power = 0.0, active_energy = 0.0, frequency = 0.0, power_factor = 0.0, over_power_alarm = 0.0; 
bool modbus_status = false;

StaticJsonDocument<1024> server_raw_values;

uint8_t result;  uint16_t data[6];

void pzemdata(){

    //node.clearResponseBuffer();

    Serial.println("Getting values...");

    ESP.wdtDisable();
    result = node.readInputRegisters(0x0000, 10);
    ESP.wdtEnable(1);
    
    if (result == node.ku8MBSuccess) {
      modbus_status = true;
      voltage_usage      = (node.getResponseBuffer(0x00) / 10.0f);
      current_usage      = ((node.getResponseBuffer(0x02)<<16 | node.getResponseBuffer(0x01)) / 1000.000f);
      active_power       = ((node.getResponseBuffer(0x04)<<16 | node.getResponseBuffer(0x03)) / 10.0f);
      active_energy      = ((node.getResponseBuffer(0x06)<<16 | node.getResponseBuffer(0x05)) / 1.0f);
      frequency          = (node.getResponseBuffer(0x07) / 10.0f);
      power_factor       = (node.getResponseBuffer(0x08) / 100.0f);
      over_power_alarm   = (node.getResponseBuffer(0x09));

      server_raw_values["modbus_status"] = modbus_status;
      server_raw_values["voltage_usage"] = voltage_usage;
      server_raw_values["current_usage"] = current_usage;
      server_raw_values["active_power"] = active_power;
      server_raw_values["active_energy"] = active_energy;
      server_raw_values["frequency"] = frequency;
      server_raw_values["power_factor"] = power_factor;
      server_raw_values["over_power_alarm"] = over_power_alarm;


      Serial.print("VOLTAGE:           ");   Serial.println(voltage_usage);   // V
      Serial.print("CURRENT_USAGE:     ");   Serial.println(current_usage, 3);  //  A
      Serial.print("ACTIVE_POWER:      ");   Serial.println(active_power);   //  W
      Serial.print("ACTIVE_ENERGY:     ");   Serial.println(active_energy, 3);  // kWh
      Serial.print("FREQUENCY:         ");   Serial.println(frequency);    // Hz
      Serial.print("POWER_FACTOR:      ");   Serial.println(power_factor);
      Serial.print("OVER_POWER_ALARM:  ");   Serial.println(over_power_alarm, 0);
      Serial.println("====================================================");  
    } else{
      modbus_status = false;
      Serial.println("Failed to read Modbus");
    }
}

void updateDashboardValues() {
  ESPDash.updateNumberCard("voltage", (int)voltage_usage);
  ESPDash.updateNumberCard("current", (int)current_usage);
  ESPDash.updateNumberCard("active_power", (int)active_power);
  ESPDash.updateNumberCard("active_energy", (int)active_energy);
  ESPDash.updateNumberCard("frequency", (int)frequency);
  ESPDash.updateNumberCard("power_factor", (int)power_factor);
  ESPDash.updateNumberCard("alarm", (int)over_power_alarm);
  ESPDash.updateStatusCard("modbus_status", modbus_status);
}

Ticker collectValues(pzemdata, 100);
Ticker updateDashboard(updateDashboardValues, 500);

void setupOTA() {
  // If we need to change OTA port
  ArduinoOTA.setPort(8266);
 
  ArduinoOTA.setHostname("EnergyMeter");
 
  // Password to upload via OTA
  // ArduinoOTA.setPassword((const char *)"123");
 
  ArduinoOTA.onStart([]() {
    Serial.println("Starting remote update....");
  });
  ArduinoOTA.onEnd([]() {
    Serial.println("Finish remote update");
  });
  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%r", (progress / (total / 100)));
  });
  ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error [%u]: ", error);
    if (error == OTA_AUTH_ERROR) Serial.println("Auth failed");
    else if (error == OTA_BEGIN_ERROR) Serial.println("Start error");
    else if (error == OTA_CONNECT_ERROR) Serial.println("Connection error");
    else if (error == OTA_RECEIVE_ERROR) Serial.println("Reception error");
    else if (error == OTA_END_ERROR) Serial.println("Finish error");
  });
  Serial.println("Activate OTA");
  ArduinoOTA.begin();
}

void setupWIFI() {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.waitForConnectResult() != WL_CONNECTED) {
    Serial.println("Connection to WIFI network failed. Reseting....");
    delay(5000);
    ESP.restart();
  }
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());
}

void setupEnergyMeter() {
  energyMeter.begin(9600);
  // Modbus slave ID 1
  node.begin(1, energyMeter);

  // Start collection of values
  collectValues.start();
}

void setupDashboard() {
  ESPDash.addNumberCard("voltage", "Voltage (V)", 0);
  ESPDash.addNumberCard("current", "Current (A)", 0);
  ESPDash.addNumberCard("active_power", "Active Power (W)", 0);
  ESPDash.addNumberCard("active_energy", "Active Energy (Wh)", 0);
  ESPDash.addNumberCard("frequency", "Frequency (Hz)", 0);
  ESPDash.addNumberCard("power_factor", "Power Factor", 0);
  ESPDash.addNumberCard("alarm", "Alarm", 0);
  ESPDash.addStatusCard("modbus_status", "Modbus Status", false);

  ESPDash.init(server);
  server.begin();

  // Start collection of values to dashboard
  updateDashboard.start();
}

void setup() {
  // Serial.begin(115200);
  setupWIFI();
  setupOTA();
  setupEnergyMeter();
  setupDashboard();
  server.on("/raw", HTTP_GET, [](AsyncWebServerRequest *request){
        String buffer;
        serializeJsonPretty(server_raw_values, buffer);
        request->send(200, "application/json", buffer);
  });
  Serial.println("All done");
}

void loop() {
  ArduinoOTA.handle();
  collectValues.update();
  updateDashboard.update();
}