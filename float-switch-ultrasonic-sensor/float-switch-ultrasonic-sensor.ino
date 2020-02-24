#include <ESP8266WiFi.h>
#include <PubSubClient.h>

#define WELL_LEVEL_SENSOR D5
const int RELAY_SWITCH = 5;

const char* ssid = "<enter WiFi SSID here>";
const char* password = "<enter WiFi WPA/WPA2 secret here>";
const char* mqttServer = "<enter MQTT IP address/domain>";

WiFiClient espClient;
PubSubClient client(espClient);

unsigned long timeStopped = 0;
unsigned long timeStarted = 0;
bool pumpState = false;
String waterTankIntent = "OFF";
String delayStart = "";
int delayStartInt = 1800000; //Half hr - Debounce for when well signals enough water. Pump needs to wait or else it wil stop/start in quick succession.
int minuteCountdown = 0;

const char* timeToWaitTopic = "water-well/recovery-time";
const char* waterTankIntentTopic = "water-tank/status";
const char* waterPumpStatusTopic = "water-pump/status";
const char* waterPumpOnlineTopic = "water-pump/online";
const char* waterWellTimeTopic = "water-well/recovery-countdown";

void setupWifi() {
  delay(10);
  // Start by connecting to a WiFi network
  Serial.println("Connecting to: " + String(ssid));
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

void callback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [" + String(topic) + "]");
  if (strcmp(topic, waterTankIntentTopic) == 0) {
    waterTankIntent = "";
    for (int i = 0; i < length; i++) {
      waterTankIntent += (char)payload[i];
    }
    Serial.println("Received " + String(waterTankIntentTopic) + " " + waterTankIntent);
  } else if (strcmp(topic, timeToWaitTopic) == 0) {
    delayStart = "";
    for (int i = 0; i < length; i++) {
      delayStart += (char)payload[i];
    }
    delayStartInt = delayStart.toInt() * 60000;
    Serial.println("Time to start" + String(delayStartInt));

    client.publish(waterWellTimeTopic, delayStart.c_str());
    Serial.println("Received " + String(timeToWaitTopic) + " " + delayStart);
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.println("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP8266Client-";
    clientId += String(random(0xffff), HEX);
    // Attempt to connect
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(waterTankIntentTopic);
      client.subscribe(timeToWaitTopic);
      client.publish(waterPumpOnlineTopic, "OFF");
      delay(500);
      client.publish(waterPumpOnlineTopic, "ON");

      Serial.println("About to set initial waiting time");
      client.publish(timeToWaitTopic, String(delayStartInt/60000).c_str());
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      // Wait 5 seconds before retrying
      delay(5000);
    }
  }
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);     // Initialize the LED_BUILTIN pin as an output
  pinMode(WELL_LEVEL_SENSOR, INPUT_PULLUP);      // set pin as input
  pinMode(RELAY_SWITCH, OUTPUT);      // set pin as output
  digitalWrite(LED_BUILTIN, HIGH);  //Start light as off. High is use because current needs to flow through, to turn off

  Serial.begin(9600); //Open a serial port.
  setupWifi();
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  if (digitalRead(WELL_LEVEL_SENSOR) == HIGH)  //If the water well level is low
  {
    timeStopped = millis();
    if (pumpState) {  //And the pump is already running
      setPumpState(false, delayStart);
    }
  }
  else //If there is enough water in the well
  {
    timeStarted = millis();
    //If the pump is running
    if (pumpState) {
      if (waterTankIntent == "OFF" || waterTankIntent != "ON") { //Water tank is full. If anything else other than "ON or OFF" is sent. Switch off
        setPumpState(false, "0");
      }
    } else { //if pump not running
      if ((timeStarted - timeStopped) >= delayStartInt) { //And the delay counter is over, then switch pump on
        if (waterTankIntent == "ON") { //Water tank is empty
          setPumpState(true, "0");
        }
      } else { //Send how long it will be until pump started every minute
        int timeToWait = ((delayStartInt - (timeStarted - timeStopped)) / 60000);
        if (timeToWait != minuteCountdown) {
          minuteCountdown = timeToWait;
          Serial.println("Starting in: " + String(minuteCountdown) + " minutes");
          client.publish(waterWellTimeTopic, String(minuteCountdown).c_str());
        }
      }
    }
  }
}

void setPumpState(bool state, String wellTimeToRecover) {
  pumpState = state;
  const String stateStr = state ? "ON" : "OFF";
  Serial.println("Pump " + stateStr);
  digitalWrite(LED_BUILTIN, (state ? LOW : HIGH));
  client.publish(waterPumpStatusTopic, stateStr.c_str());
  client.publish(waterWellTimeTopic, wellTimeToRecover.c_str());
  digitalWrite(RELAY_SWITCH, (state ? HIGH : LOW));
}
