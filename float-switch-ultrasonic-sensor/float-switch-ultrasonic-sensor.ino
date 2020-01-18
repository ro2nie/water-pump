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
int delayStartInt = 12000; //Debounce for when well signals enough water. Pump needs to wait or else it wil stop/start in quick succession.
int minuteCountdown = 0;

void setup_wifi() {
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
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.println("] ");
  const char* timeToWaitTopic = "water-well/time-to-wait";
  const char* waterTankIntentTopic = "water-tank/status";
  
  if (strcmp(topic, waterTankIntentTopic) == 0) {
    Serial.println("TODO-0");
    waterTankIntent = "";
    for (int i = 0; i < length; i++) {
      waterTankIntent += (char)payload[i];
    }
    Serial.println("Received water-tank/status" + waterTankIntent);
  } else if (strcmp(topic, timeToWaitTopic) == 0) {
    delayStart = "";
    for (int i = 0; i < length; i++) {
      delayStart += (char)payload[i];
    }
    delayStartInt = delayStart.toInt() * 60 * 1000;
    client.publish("water-well/time", delayStart.c_str());
    Serial.println("Received message water-well/time-to-wait" + delayStart);
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
      client.subscribe("water-tank/status");
      client.subscribe("water-well/time-to-wait");      
      client.publish("water-pump/controller/status", "OFF");
      delay(500);
      client.publish("water-pump/controller/status", "ON");
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
  setup_wifi();
  client.setServer(mqttServer, 1883);
  client.setCallback(callback);
  Serial.println("About to set initial waiting time");
  Serial.println(delayStartInt);  
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
      pumpState = false;  //Then switch it off
      Serial.print("Pump OFF at: " + String(timeStopped) + "\n");
      digitalWrite(LED_BUILTIN, HIGH);
      client.publish("water-pump/status", "OFF");
      client.publish("water-well/time", delayStart.c_str());
      digitalWrite(RELAY_SWITCH, LOW);
    }
  }
  else //If there is enough water in the well
  {
    timeStarted = millis();

    //If the pump is running
    if (pumpState) {
      if (waterTankIntent == "OFF" || waterTankIntent != "ON") { //Water tank is full. If anything else other than "ON or OFF" is sent. Switch off
        pumpState = false;  //Then switch it off
        Serial.println("Pump OFF");
        digitalWrite(LED_BUILTIN, HIGH);
        client.publish("water-pump/status", "OFF");        
        client.publish("water-well/time", delayStart.c_str());
        digitalWrite(RELAY_SWITCH, LOW);
      }
    } else { //if pump not running
      if (waterTankIntent == "ON") { //Water tank is empty
        if ((timeStarted - timeStopped) >= delayStartInt) { //And the delay counter is over, then switch pump on
          pumpState = true;
          Serial.println("Pump ON");
          digitalWrite(LED_BUILTIN, LOW);
          client.publish("water-pump/status", "ON");
          client.publish("water-well/time", String(minuteCountdown).c_str());
          digitalWrite(RELAY_SWITCH, HIGH);
        } else { //Send how long it will be until pump started every minute
          int timeToWait = ((delayStartInt -(timeStarted - timeStopped)) / 60000);
          if (timeToWait != minuteCountdown) {
            minuteCountdown = timeToWait;
            String countdown = String(minuteCountdown + 1);
            Serial.println("Starting in: " + countdown + " minutes");
            client.publish("water-well/time", countdown.c_str());            
          }
        }
      }
    }
  }
}
