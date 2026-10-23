//Required Libraries 
#include <painlessMesh.h>

//Mesh Network Configuration
#define MESH_PREFIX   "SIMON_MESH"  //Mesh name 
#define MESH_PASSWORD "12345678"    //Mesh password
#define MESH_PORT     5555          //Mesh port used
#define MESH_CHANNEL  6             //Wifi Ch

Scheduler userScheduler;      //painlessmesh task scheduler
painlessMesh mesh;            //create mesh

//Board pins
#define startResetBtn D0      //Start&Reset button
#define blueBtn       D1      //Blue Button
#define yellowBtn     D2      //Yellow Button
#define redBtn        D3      //Red Button
#define greenBtn      D4      //Green Button
#define buzzerPin     D5      //Blue Button

uint32_t board1Id = 0;        // Stores the node ID of Board 1 once communication is established


const unsigned long DEBOUNCE_MS = 180;    // Debounce timing to prevent repeated button messages from being sent too quickly

unsigned long lastSendMs = 0;       // Stores the time when the last message was sent


// Store previous button states to detect button press transitions
bool lastStartReset = HIGH;
bool lastBlue       = HIGH;
bool lastYellow     = HIGH;
bool lastRed        = HIGH;
bool lastGreen      = HIGH;

// Track Board 1's current game state
enum RemoteState { REMOTE_UNKNOWN, REMOTE_IDLE, REMOTE_SHOW_SEQ, REMOTE_USER_INPUT, REMOTE_GAME_OVER };
RemoteState remoteState = REMOTE_UNKNOWN;

// Custom delay function that keeps the mesh responsive during waiting
void meshWait(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    mesh.update();
    delay(1);
  }
}

// Check whether enough time has passed to allow another message to be sent
bool canSend() {
  unsigned long now = millis();
  if (now - lastSendMs < DEBOUNCE_MS) return false;
  lastSendMs = now;
  return true;
}

// Activate buzzer for a short duration
void buzz(uint16_t durationMs = 250) {
  digitalWrite(buzzerPin, HIGH);
  meshWait(durationMs);
  digitalWrite(buzzerPin, LOW);
}

// Send a message to Board 1 through the mesh network
void sendMsg(const String& msg) {
  if (!canSend()) return;

  if (board1Id != 0) {
    mesh.sendSingle(board1Id, msg);
  } else {
    mesh.sendBroadcast(msg);
  }

  Serial.println("[B2] TX " + msg);
}

// Update Board 2’s stored version of Board 1’s state based on received messages
void updateRemoteStateFromMessage(const String& msg) {
  if (msg == "STATE:IDLE") {
    remoteState = REMOTE_IDLE;
  } else if (msg == "STATE:SHOW_SEQ") {
    remoteState = REMOTE_SHOW_SEQ;
  } else if (msg == "STATE:USER_INPUT") {
    remoteState = REMOTE_USER_INPUT;
  } else if (msg == "STATE:GAME_OVER") {
    remoteState = REMOTE_GAME_OVER;
  } else if (msg == "GAMEOVER") {
    remoteState = REMOTE_GAME_OVER;
  } else if (msg == "ROUND") {
    remoteState = REMOTE_SHOW_SEQ;
  }
}

// Callback function that runs whenever Board 2 receives a mesh message
void receivedCallback(uint32_t from, String &msg) {
  msg.trim();
  board1Id = from;

  Serial.printf("[B2] RX from %u: %s\n", from, msg.c_str());

  updateRemoteStateFromMessage(msg);

  // Buzz only once on wrong input
  if (msg == "WRONG") {
    buzz();
  }
}

//Setup
void setup() {
  Serial.begin(9600);        // Start serial communication
  delay(50);                 // Short startup delay

  //configure buttons
  pinMode(startResetBtn, INPUT_PULLUP);
  pinMode(blueBtn, INPUT_PULLUP);
  pinMode(yellowBtn, INPUT_PULLUP);
  pinMode(redBtn, INPUT_PULLUP);
  pinMode(greenBtn, INPUT_PULLUP);

  //configure buzzer
  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  //-------------------DEBUG----------------------------------------------------------------------
  // Enable mesh debug messages for troubleshooting
  mesh.setDebugMsgTypes(ERROR | STARTUP | CONNECTION | COMMUNICATION);

  // Initialise mesh as station-only node on the specified channel
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_STA, MESH_CHANNEL);

  // Tell the mesh that a root node exists somewhere in the network
  mesh.setContainsRoot(true);

  // Register callback for received messages
  mesh.onReceive(&receivedCallback);
}

//loop
void loop() {
  mesh.update();

  // Read current button states
  bool curStartReset = digitalRead(startResetBtn);
  bool curBlue       = digitalRead(blueBtn);
  bool curYellow     = digitalRead(yellowBtn);
  bool curRed        = digitalRead(redBtn);
  bool curGreen      = digitalRead(greenBtn);


  // Start/Reset button pressed
  if (lastStartReset == HIGH && curStartReset == LOW) {
    if (remoteState == REMOTE_SHOW_SEQ || remoteState == REMOTE_USER_INPUT) {
      sendMsg("RESET");
    } else {
      sendMsg("START");
    }
  }

  if (lastGreen == HIGH && curGreen == LOW)   sendMsg("PRESS:0"); //Green button pressed
  if (lastRed == HIGH && curRed == LOW)       sendMsg("PRESS:1"); //Red button pressed
  if (lastYellow == HIGH && curYellow == LOW) sendMsg("PRESS:2"); //Yellow button pressed
  if (lastBlue == HIGH && curBlue == LOW)     sendMsg("PRESS:3"); //Blue button pressed

  // Update previous button states for next loop cycle
  lastStartReset = curStartReset;
  lastBlue       = curBlue;
  lastYellow     = curYellow;
  lastRed        = curRed;
  lastGreen      = curGreen;
  delay(5); //Short delay
}