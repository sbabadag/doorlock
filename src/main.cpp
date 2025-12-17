#include <Arduino.h>
#include <Stepper.h>
#include <WiFi.h>
#include <WebServer.h>

// ==========================================
// WIFI CONFIGURATION
// ==========================================
const char* ssid = "TURKSAT-KABLONET-RQ0A-2.4G";
const char* password = "598e5577c916";

// ==========================================
// STEPPER CONFIGURATION
// ==========================================
const int STEPS_PER_REVOLUTION = 2048;
Stepper myStepper(STEPS_PER_REVOLUTION, D0, D2, D1, D3);

// ==========================================
// WEB SERVER
// ==========================================
WebServer server(80);

// Global variables for non-blocking movement
long stepsRemaining = 0;
bool isMoving = false;

// Sequence Control
enum SequenceState { SEQ_IDLE, SEQ_MOVING_CW, SEQ_WAITING, SEQ_MOVING_CCW };
SequenceState sequenceState = SEQ_IDLE;
unsigned long sequenceTimer = 0;
const int STEPS_15_DEG = 85; // (2048 / 360) * 15 = ~85 steps

// Enable CORS for GitHub Pages access
void sendCORSHeaders() {
  server.sendHeader("Access-Control-Allow-Origin", "*");
  server.sendHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
  server.sendHeader("Access-Control-Allow-Headers", "*");
}

void handleRoot() {
  sendCORSHeaders();
  server.send(200, "text/plain", "Stepper Controller Online. Use /action?type=cw|ccw|stop");
}

void handleAction() {
  sendCORSHeaders();
  if (server.hasArg("type")) {
    String type = server.arg("type");
    
    if (type == "cw") {
      stepsRemaining = STEPS_PER_REVOLUTION; // 1 full revolution
      isMoving = true;
      server.send(200, "text/plain", "Moving Clockwise");
    } 
    else if (type == "ccw") {
      stepsRemaining = -STEPS_PER_REVOLUTION; // 1 full revolution
      isMoving = true;
      server.send(200, "text/plain", "Moving Counter-Clockwise");
    }
    else if (type == "stop") {
      stepsRemaining = 0;
      isMoving = false;
      sequenceState = SEQ_IDLE; // Cancel any sequence
      // Turn off coils to save power/heat when stopped (optional)
      digitalWrite(D0, LOW);
      digitalWrite(D1, LOW);
      digitalWrite(D2, LOW);
      digitalWrite(D3, LOW);
      server.send(200, "text/plain", "Stopped");
    }
    else if (type == "test15") {
      // Calibrate (assume 0) -> Move 15 deg CW -> Wait 3s -> Move back
      sequenceState = SEQ_MOVING_CW;
      stepsRemaining = STEPS_15_DEG;
      server.send(200, "text/plain", "Starting 15 deg sequence");
    }
    else if (type == "move45") {
      // Move 45 degrees Clockwise
      // 2048 steps / 8 = 256 steps
      sequenceState = SEQ_IDLE; // Ensure we are in manual mode
      stepsRemaining = 256;
      isMoving = true;
      server.send(200, "text/plain", "Moving 45 degrees CW");
    }
    else {
      server.send(400, "text/plain", "Unknown action");
    }
  } else {
    server.send(400, "text/plain", "Missing type argument");
  }
}

void handleOptions() {
  sendCORSHeaders();
  server.send(200);
}

void setup() {
  Serial.begin(115200);
  
  // Stepper setup
  myStepper.setSpeed(10); // 10 RPM

  // WiFi setup
  WiFi.mode(WIFI_STA); // Explicitly set station mode
  WiFi.disconnect();   // Disconnect from any previous session
  delay(100);

  Serial.println("Scanning for networks...");
  int n = WiFi.scanNetworks();
  Serial.println("Scan done");
  if (n == 0) {
      Serial.println("No networks found. CHECK ANTENNA!");
  } else {
      Serial.print(n);
      Serial.println(" networks found:");
      for (int i = 0; i < n; ++i) {
          Serial.print(i + 1);
          Serial.print(": ");
          Serial.print(WiFi.SSID(i));
          Serial.print(" (");
          Serial.print(WiFi.RSSI(i));
          Serial.println(")");
      }
  }

  Serial.print("Connecting to ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 40) { // Increased timeout to 20 seconds
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  Serial.println("");
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi connected.");
    Serial.println("IP address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("WiFi connection failed!");
    Serial.print("Error code: ");
    Serial.println(WiFi.status());
    Serial.println("Check credentials, 2.4GHz network, and antenna.");
  }

  // Web Server setup
  server.on("/", HTTP_GET, handleRoot);
  server.on("/action", HTTP_GET, handleAction);
  server.onNotFound([]() {
    if (server.method() == HTTP_OPTIONS) {
      handleOptions();
    } else {
      server.send(404, "text/plain", "Not Found");
    }
  });
  
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();

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
      }
      break;
  }
}