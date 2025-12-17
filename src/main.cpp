#include <Arduino.h>
#include <Stepper.h>
#include <WiFi.h>
#include <PubSubClient.h>

// ==========================================
// WIFI CONFIGURATION
// ==========================================
const char* ssid = "TURKSAT-KABLONET-RQ0A-2.4G";
const char* password = "598e5577c916";

// ==========================================
// MQTT CONFIGURATION
// ==========================================
const char* mqtt_server = "broker.emqx.io";
const int mqtt_port = 1883;
const char* mqtt_topic_cmd = "suntracker/cmd";
const char* mqtt_topic_status = "suntracker/status";

WiFiClient espClient;
PubSubClient client(espClient);

// ==========================================
// STEPPER CONFIGURATION
// ==========================================
const int STEPS_PER_REVOLUTION = 2048;
Stepper myStepper(STEPS_PER_REVOLUTION, D0, D2, D1, D3);

// Global variables for non-blocking movement
long stepsRemaining = 0;
bool isMoving = false;

// Sequence Control
enum SequenceState { SEQ_IDLE, SEQ_MOVING_CW, SEQ_WAITING, SEQ_MOVING_CCW };
SequenceState sequenceState = SEQ_IDLE;
unsigned long sequenceTimer = 0;
const int STEPS_15_DEG = 85; // (2048 / 360) * 15 = ~85 steps

void callback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("] ");
  Serial.println(message);

  if (message == "cw") {
    stepsRemaining = STEPS_PER_REVOLUTION;
    isMoving = true;
    client.publish(mqtt_topic_status, "Moving Clockwise");
  } 
  else if (message == "ccw") {
    stepsRemaining = -STEPS_PER_REVOLUTION;
    isMoving = true;
    client.publish(mqtt_topic_status, "Moving Counter-Clockwise");
  }
  else if (message == "stop") {
    stepsRemaining = 0;
    isMoving = false;
    sequenceState = SEQ_IDLE;
    digitalWrite(D0, LOW); digitalWrite(D1, LOW); digitalWrite(D2, LOW); digitalWrite(D3, LOW);
    client.publish(mqtt_topic_status, "Stopped");
  }
  else if (message == "test15") {
    sequenceState = SEQ_MOVING_CW;
    stepsRemaining = STEPS_15_DEG;
    client.publish(mqtt_topic_status, "Starting 15 deg sequence");
  }
  else if (message == "move45") {
    sequenceState = SEQ_IDLE;
    stepsRemaining = 256;
    isMoving = true;
    client.publish(mqtt_topic_status, "Moving 45 degrees CW");
  }
}

void reconnect() {
  // Loop until we're reconnected
  while (!client.connected()) {
    Serial.print("Attempting MQTT connection...");
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);
    
    if (client.connect(clientId.c_str())) {
      Serial.println("connected");
      client.subscribe(mqtt_topic_cmd);
      client.publish(mqtt_topic_status, "Online");
    } else {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  
  // Stepper setup
  myStepper.setSpeed(10); // 10 RPM

  // WiFi setup
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println("");
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
    
    client.setServer(mqtt_server, mqtt_port);
    client.setCallback(callback);
  } else {
    Serial.println("WiFi Failed");
  }
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    if (!client.connected()) {
      reconnect();
    }
    client.loop();
  }

  // State Machine for Sequence and Manual Control
  switch (sequenceState) {
    case SEQ_IDLE:
      // Manual Control Logic
      if (stepsRemaining != 0) {
        if (stepsRemaining > 0) {
          myStepper.step(1);
          stepsRemaining--;
        } else {
          myStepper.step(-1);
          stepsRemaining++;
        }
      } else if (isMoving) {
        // Movement finished naturally
        isMoving = false;
        digitalWrite(D0, LOW); digitalWrite(D1, LOW); digitalWrite(D2, LOW); digitalWrite(D3, LOW);
        client.publish(mqtt_topic_status, "Idle");
      }
      break;

    case SEQ_MOVING_CW:
      if (stepsRemaining > 0) {
        myStepper.step(1);
        stepsRemaining--;
      } else {
        // Finished 15 deg CW, start waiting
        sequenceState = SEQ_WAITING;
        sequenceTimer = millis();
      }
      break;

    case SEQ_WAITING:
      if (millis() - sequenceTimer >= 3000) {
        // Wait done, move back
        sequenceState = SEQ_MOVING_CCW;
        stepsRemaining = STEPS_15_DEG; // Reset count for return trip
      }
      break;

    case SEQ_MOVING_CCW:
      if (stepsRemaining > 0) {
        myStepper.step(-1); // Move CCW
        stepsRemaining--;
      } else {
        // Sequence Complete
        sequenceState = SEQ_IDLE;
        digitalWrite(D0, LOW); digitalWrite(D1, LOW); digitalWrite(D2, LOW); digitalWrite(D3, LOW);
        client.publish(mqtt_topic_status, "Sequence Complete");
      }
      break;
  }
}
        // Wait done, move back
        sequenceState = SEQ_MOVING_CCW;
        stepsRemaining = STEPS_15_DEG; // Reset count for return trip
      }
      break;

    case SEQ_MOVING_CCW:
      if (stepsRemaining > 0) {
        myStepper.step(-1); // Move CCW
        stepsRemaining--;
      } else {
        // Sequence Complete
        sequenceState = SEQ_IDLE;
        digitalWrite(D0, LOW); digitalWrite(D1, LOW); digitalWrite(D2, LOW); digitalWrite(D3, LOW);
      }
      break;
  }
}