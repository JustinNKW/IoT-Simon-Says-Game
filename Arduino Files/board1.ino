//Required Libraries 
#include <painlessMesh.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266HTTPClient.h>
#include <WiFiClient.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//Mesh Network Configuration
#define MESH_PREFIX   "SIMON_MESH"//Mesh name 
#define MESH_PASSWORD "12345678"  //Mesh password
#define MESH_PORT     5555        //Mesh port used
#define MESH_CHANNEL  6           //Wifi Ch

Scheduler userScheduler;          //painlessmesh task scheduler
painlessMesh mesh;                //create mesh

//Router & Node Red Settings
const char* ROUTER_SSID = "WifiID";                               //Wifi name
const char* ROUTER_PASS = "WifiPwd";                              //Wifi password
const char* NODE_RED_POST_URL = "http://192.168.1.50:1880/simon"; //Node Red endpoint

WiFiClient wifiClient;                                            //TCP Client
ESP8266WebServer server(80);                                      //Web Server port

//Board pins
#define greenLed  D8                //Green led
#define redLed    D7                //Red led
#define yellowLed D6                //Yellow led
#define blueLed   D5                //Blue led
LiquidCrystal_I2C lcd(0x27, 16, 2); //LCD display

//Game settings
enum GameState { IDLE, SHOW_SEQ, USER_INPUT, GAME_OVER }; //Game states
GameState gameState = IDLE;                               //Initial State (IDLE)

//Sequence Storage
const int MAX_SEQ = 255;            //Max sequence length
uint8_t seq[MAX_SEQ];               //Array to store sequence
int seqLen = 0;                     //Current sequence length
int inputIndex = 0;                 //Current input index position
int topScore = 0;                   //Top score

//Game Timer
unsigned long runStartMs = 0;       //Game timer
bool runActive = false;             //Checks if the game is running

//Board 2
uint32_t controllerId = 0;          //Store controller id

//Run storage
String scores[10];                  //Stores scores of last runs
String times[10];                   //Stores time taken
String sequences[10];               //Stores sequences played
String difficulties[10];            //Difficulty taken
int head = 0;                       //Buffer head pointer
int count = 0;                      //Number of runs stored

//For IP tracking
bool ipPrinted = false;             //Print IP only once 
String lastIP = "";                 //Stores the last printed ip


//Difficulty levels 
enum Difficulty { EASY = 0, NORMAL = 1, HARD = 2, VERY_HARD = 3 };
Difficulty difficulty = EASY; //Easy difficulty by default


//Default Difficulty: Easy (establish variables)
uint16_t onMs  = 700;  //onMs refers to LED on duration
uint16_t offMs = 400;  //offMs refers to delay between LED flashes

//Apply difficulty settings if difficulty is changed
void applyDifficulty(Difficulty d) {
  difficulty = d;

  switch (d) {
    case EASY:
      onMs = 700;  offMs = 400;  
      break;
    case NORMAL:
      onMs = 600;  offMs = 350;
      break;
    case HARD:
      onMs = 500;  offMs = 300; 
      break;
    case VERY_HARD:
      onMs = 400;  offMs = 250;
      break;
  }
}

//Difficulty name
String difficultyName() {
  switch (difficulty) {
    case EASY: return "EASY";
    case NORMAL: return "NORMAL";
    case HARD: return "HARD";
    case VERY_HARD: return "V.HARD";
  }
  return "EASY";             //Fallback in case
}

//Mesh friendly delay
void meshWait(uint32_t ms) {
  uint32_t start = millis();
  while (millis() - start < ms) {
    mesh.update();
    server.handleClient();
    delay(1);
  }
}

//Pin mapping for leds (Green, Red, Yellow, Blue)
uint8_t pinForColour(uint8_t c) {
  if (c == 0) return greenLed;    
  if (c == 1) return redLed;
  if (c == 2) return yellowLed;
  return blueLed;
}

//Color Characters (for HTML/Node Red)
char colourChar(uint8_t c) {
  if (c == 0) return 'G';               //Green for G
  if (c == 1) return 'R';               //Red for R
  if (c == 2) return 'Y';               //Yellow for Y
  return 'B';                           //Blue for B
}

//Convert time to readable time (HH:MM:SS)
String timeTaken(unsigned long ms) {
  unsigned long s = ms / 1000UL;
  char buf[9];
  sprintf(buf, "%02lu:%02lu:%02lu", s / 3600UL, (s % 3600UL) / 60UL, s % 60UL);
  return String(buf);
}

//Generate the sequence using colorchar
String seqToString(int len) {
  String s = "";
  for (int i = 0; i < len; i++) s += colourChar(seq[i]);
  return s;
}

//Calculate score
int completedScore() {
  return (seqLen > 0) ? (seqLen - 1) : 0;
}

//Log run
void logRun(unsigned long durationMs) {
  int score = completedScore();
  if (score > topScore) topScore = score;

  scores[head] = String(score);
  times[head] = timeTaken(durationMs);
  sequences[head] = seqToString(seqLen);
  difficulties[head] = difficultyName();

  head = (head + 1) % 10;
  if (count < 10) count++;
}

//Led flash duration (depends on difficulty selected)
void flashLedByColour(uint8_t c) {
  uint8_t led = pinForColour(c);
  digitalWrite(led, HIGH);
  meshWait(onMs);
  digitalWrite(led, LOW);
  meshWait(offMs);
}

// ---------------- LCD ----------------

//Idle screen (with difficlty setting)
void showIdleScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("START to begin");
  lcd.setCursor(0, 1); lcd.print("Diff:");
  lcd.print(difficultyName());                        //Show current selected difficulty
}

//Watch screen
//Simon says sequence will be playing
void showWatchScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Watch carefully");
  lcd.setCursor(0, 1); lcd.print(difficultyName());
}

//User input state screen
void showYourTurnScreen() {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Your turn");
  lcd.setCursor(0, 1); lcd.print("Len:");
  lcd.print(seqLen);                              //Show sequence length (how many buttons the user needs to press)
}

//Game over screen
void showGameOverScreen(int score, const String& timeStr) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("GAME OVER");
  lcd.setCursor(0, 1); lcd.print("Score:");
  lcd.print(score);                               //Display final score
}

//Mesh messaging to board
void sendToController(const String& msg) {
  if (controllerId != 0) mesh.sendSingle(controllerId, msg);
  else mesh.sendBroadcast(msg);
}


// ---------------- Node-RED ----------------
bool postToNodeRed(int score, unsigned long durationMs, const String& seqStr) {
  if (WiFi.status() != WL_CONNECTED) return false;
  HTTPClient http;
  if (!http.begin(wifiClient, NODE_RED_POST_URL)) return false;

  http.addHeader("Content-Type", "application/json");

  String body = "{";
  body += "\"score\":" + String(score) + ",";
  body += "\"time_ms\":" + String(durationMs) + ",";
  body += "\"time\":\"" + timeTaken(durationMs) + "\",";
  body += "\"sequence\":\"" + seqStr + "\",";
  body += "\"difficulty\":\"" + difficultyName() + "\",";
  body += "\"topScore\":" + String(topScore) + ",";
  body += "\"nodeId\":" + String(mesh.getNodeId());
  body += "}";

  int code = http.POST(body);
  http.end();

  return (code >= 200 && code < 300);
}


// ---------------- IP Handling ----------------
void printIPsOnceConnected() {
  static bool printedAP = false;
  static bool printedConnected = false;
  static unsigned long lastAttemptMsg = 0;

  // Print mesh AP IP once
  if (!printedAP) {
    Serial.print("[B1] Mesh AP IP: ");
    Serial.println(WiFi.softAPIP());
    printedAP = true;
  }

  // If not connected, print a friendly "attempting" message every 2 seconds
  if (WiFi.status() != WL_CONNECTED) {
    printedConnected = false; // allow connected message again later if it reconnects

    unsigned long now = millis();
    if (now - lastAttemptMsg >= 2000) {
      Serial.println("[B1] Attempting to connect to router WiFi...");
      lastAttemptMsg = now;
    }
    return;
  }

  // Connected: print once (and again if IP changes)
  String ip = WiFi.localIP().toString();
  if (!ipPrinted || ip != lastIP || !printedConnected) {
    Serial.print("[B1] Router WiFi connected. IP: ");
    Serial.println(ip);
    ipPrinted = true;
    lastIP = ip;
    printedConnected = true;
  }
}

//Game reset
//Sets everything to 0 and change to idle state
void resetGameToIdle() {
  seqLen = 0;
  inputIndex = 0;
  runActive = false;
  gameState = IDLE;
  showIdleScreen();
  sendToController("STATE:IDLE");
}

//Start game
void startGame() {
  seqLen = 1;             //starts with sequence 1
  inputIndex = 0;         //reset the input tracker
  seq[0] = random(0, 4);  //Generates first number 
  runStartMs = millis();  //Record game start time
  runActive = true;       //Mark run as active
  gameState = SHOW_SEQ;   //Move to sequence display state
}

//Show Sequence state handling
void showSequence() {
  showWatchScreen();      //Tell user to watch sequence
  meshWait(400);          //short pause before sequence starts

  for (int i = 0; i < seqLen; i++) {
    flashLedByColour(seq[i]); //Flash each colour in the sequence
  }

  inputIndex = 0;           //Reset input index for player's turn
  gameState = USER_INPUT;   //Switch to player input state
  showYourTurnScreen();     //Show user-turn screen on LCD
}

//Game over state handling
void endGame() {
  unsigned long duration = runActive ? (millis() - runStartMs) : 0;       //Total run duration 
  runActive = false;                                                      //Mark the run as over
  logRun(duration);                                                       //save the run result (score, time, sequence) into the log arrays

  int lastIdx = (head - 1 + 10) % 10;                                     //Retrieve run index (index 0-9)  
  int score = scores[lastIdx].toInt();                                    //Get the score from the stored results
  String lastSeq = sequences[lastIdx];                                    //Retrieve the sequence string

  gameState = GAME_OVER;                                                  //Set game state to game over

  sendToController("GAMEOVER");                                           //Notify board 2 game over
  sendToController("RESULT:" + String(score) + "," + String(duration));   //Notify board 2 the result

  showGameOverScreen(score, times[lastIdx]);                              //Show game over screen on the lcd
  meshWait(2200);                                                         //Show the game over screen for 2.2 seconds

  lcd.clear();                                                            //Clear Lcd screen
  lcd.setCursor(0, 0); lcd.print("Top:");                                 //Show the top score screen
  lcd.print(topScore);                                                    //Show the top score screen
  lcd.setCursor(0, 1); lcd.print("Press START");                          //Prompt player to start a new game 
  meshWait(2200);                                                         //Wait for 2.2 seconds

  postToNodeRed(score, duration, lastSeq);                                //Send the result to Node-RED for logging / dashboard display

  resetGameToIdle();                                                      //Reset the game variables and return to IDLE state
}

void onPress(uint8_t c) {
  if (gameState != USER_INPUT) return;                                    //Ignore presses unless player is entering sequence

  if (seq[inputIndex] != c) {                                             //Check if pressed colour matches expected colour
    sendToController("WRONG");                                            //Notify controller of wrong input
    endGame();                                                            //End game on incorrect press
    return;
  }

  inputIndex++;                                                            //Move to next expected input
  sendToController("OK");                                                  //Notify controller of correct input

  if (inputIndex >= seqLen) {                                              //If whole sequence was entered correctly
    if (seqLen < MAX_SEQ) seq[seqLen++] = random(0, 4);                    //Add another random colour
    gameState = SHOW_SEQ;                                                  //Start next round by showing extended sequence
    sendToController("ROUND");                                             //Notify controller new round has started
  }
}

//---------------------------------HTML----------------------------------------------------------------
String makePage() {
  String h = "<!doctype html><html><head><meta charset='utf-8'>"
             "<meta name='viewport' content='width=device-width, initial-scale=1'>"
             "<meta http-equiv='refresh' content='2'>"
             "<style>"
             "body{font-family:Arial,sans-serif;background:#fff;color:#222;padding:20px;}"
             "h2{margin-bottom:6px;} h3{margin-top:24px;margin-bottom:8px;}"
             ".btn{display:inline-block;margin-top:8px;padding:6px 12px;border:1px solid #06c;"
             "border-radius:6px;color:#06c;text-decoration:none;font-size:14px;}"
             ".btn:hover{background:#f0f6ff;}"
             "table{border-collapse:collapse;width:100%;margin-top:10px;font-size:14px;}"
             "th,td{border:1px solid #ddd;padding:8px;text-align:center;}"
             "th{background:#f3f3f3;font-weight:600;}"
             "tr:nth-child(even){background:#fafafa;}"
             "code{font-family:monospace;font-size:15px;letter-spacing:2px;}"
             "</style></head><body>";
  
  String state = (gameState == IDLE ? "IDLE" :
                gameState == SHOW_SEQ ? "SHOW_SEQ" :
                gameState == USER_INPUT ? "USER_INPUT" : "GAME_OVER");

  h += "<h2>Simon Says </h2>";
  h += "<p><b>Top Score:</b> " + String(topScore) + "</p>";
  h += "<p><b>Selected Difficulty:</b> " + difficultyName() + "</p>";
  h += "<p><b>State:</b> " + state + "</p>";
  h += "<p><b>Current Sequence:</b> <code>" + seqToString(seqLen) + "</code></p>";
  h += "<p><b>Mesh NodeID:</b> " + String(mesh.getNodeId()) + "</p>";
  h += "<a class='btn' href='/'>Refresh</a> ";
  h += "<a class='btn' href='/start'>Start</a> ";
  h += "<a class='btn' href='/reset'>Reset</a>";


  h += "<h3>Last Runs</h3><table>";
  h += "<tr><th>Score</th><th>Time</th><th>Difficulty</th><th>Sequence</th></tr>";

  for (int i = 0; i < count; i++) {
    int idx = head - 1 - i;
    if (idx < 0) idx += 10;
    h += "<tr>";
    h += "<td>" + scores[idx] + "</td>";
    h += "<td>" + times[idx] + "</td>";
    h += "<td>" + difficulties[idx] + "</td>";
    h += "<td><code>" + sequences[idx] + "</code></td>";
    h += "</tr>";
  }

  h += "</table></body></html>";
  return h;
}

void handleRoot() { server.send(200, "text/html", makePage()); }

void handleStatus() {
  String state = (gameState == IDLE ? "IDLE" :
                gameState == SHOW_SEQ ? "SHOW_SEQ" :
                gameState == USER_INPUT ? "USER_INPUT" : "GAME_OVER");

  String json = "{";
  json += "\"gameState\":\"" + state + "\",";
  json += "\"currentLevel\":" + String(seqLen) + ",";
  json += "\"topScore\":" + String(topScore) + ",";
  json += "\"runActive\":" + String(runActive ? "true" : "false") + ",";
  json += "\"meshNodeId\":" + String(mesh.getNodeId()) + ",";
  json += "\"difficulty\":\"" + difficultyName() + "\",";
  json += "\"currentSequence\":\"" + seqToString(seqLen) + "\",";
  json += "\"inputIndex\":" + String(inputIndex) + ",";
  json += "\"maxSeq\":" + String(MAX_SEQ) + ",";
  json += "\"wifiConnected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false");
  if (WiFi.status() == WL_CONNECTED) {
    json += ",\"ip\":\"" + WiFi.localIP().toString() + "\"";
  }
  json += "}";
  server.send(200, "application/json", json);
}

void handleRuns() {
  String json = "{";
  json += "\"count\":";
  json += String(count);
  json += ",\"runs\":[";

  for (int i = 0; i < count; i++) {  //Number of stored runs
    int idx = head - 1 - i;            //Read newest run first
    if (idx < 0) idx += 10;             //Wrap around circular buffer

    json += "{";
    json += "\"score\":";
    json += String(scores[idx].toInt());
    json += ",\"time\":\"";
    json += times[idx];
    json += "\",\"difficulty\":\"";
    json += difficulties[idx];
    json += "\",\"sequence\":\"";
    json += sequences[idx];
    json += "\"";
    json += "}";

    if (i < count - 1) {              
      json += ",";          //Add comma between entries except last one
    }
  }

  json += "]}";            //Return run history as JSON

  Serial.println("[B1] /runs JSON:");    
  Serial.println(json);

  server.send(200, "application/json", json);
}


void handleStart() {
  if (gameState == IDLE) startGame();                 //Only allow starting from idle state
  server.send(200, "text/plain", "OK");               //Reply to browser/client
}

void handleReset() {
  resetGameToIdle();                                  //Reset the game
  server.send(200, "text/plain", "OK");               //Reply to browser/client
}

//Mesh communication (with board 2)
void receivedCallback(uint32_t from, String &msg) {
  msg.trim();
  controllerId = from;

  Serial.printf("[B1] RX from %u: %s\n", from, msg.c_str());

  // In IDLE, colour buttons select difficulty
  if (gameState == IDLE && msg.startsWith("PRESS:")) {
    int c = msg.substring(6).toInt();

    // 0=green, 1=red, 2=yellow, 3=blue
    // blue=easy, yellow=normal, red=hard, green=very hard
    if (c == 3) applyDifficulty(EASY);
    else if (c == 2) applyDifficulty(NORMAL);
    else if (c == 1) applyDifficulty(HARD);
    else if (c == 0) applyDifficulty(VERY_HARD);

    Serial.print("[B1] Difficulty now: ");
    Serial.println(difficultyName());

    showIdleScreen();
    return;
  }

  // Start game only if currently idle
  if (msg == "START") {
    if (gameState == IDLE) {
      Serial.println("[B1] START received");
      startGame();
    }
    return;
  }

  // Reset game from any active state
  if (msg == "RESET") {
    Serial.println("[B1] RESET received");
    resetGameToIdle();
    return;
  }

  // Colour presses during user input
  if (msg.startsWith("PRESS:")) {
    int c = msg.substring(6).toInt();
    if (c >= 0 && c <= 3) onPress((uint8_t)c);
    return;
  }
}




//Startup
void setup() {
  Serial.begin(9600);                     //Begin serial 
  delay(50);                              //Short startup delay

  //Set outputs for leds
  pinMode(greenLed, OUTPUT);
  pinMode(redLed, OUTPUT);
  pinMode(yellowLed, OUTPUT);
  pinMode(blueLed, OUTPUT);

  //Ensure leds start off
  digitalWrite(greenLed, LOW);
  digitalWrite(redLed, LOW);
  digitalWrite(yellowLed, LOW);
  digitalWrite(blueLed, LOW);


  //LCD
  Wire.begin(D2, D1);
  lcd.init();
  lcd.backlight();

  //Seed random generator using current time
  randomSeed(millis());                   


  //Mesh network
  mesh.setDebugMsgTypes(ERROR | STARTUP);
  mesh.init(MESH_PREFIX, MESH_PASSWORD, &userScheduler, MESH_PORT, WIFI_AP_STA, MESH_CHANNEL);
  mesh.setRoot(true);
  mesh.setContainsRoot(true);
  mesh.onReceive(&receivedCallback);
  mesh.stationManual(ROUTER_SSID, ROUTER_PASS);   //Connect to router
  Serial.println("[B1] Connecting to router (non-blocking)...");

  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/runs", handleRuns);
  server.on("/start", handleStart);
  server.on("/reset", handleReset);
  server.begin();

  //On setup, difficulty is set to easy
  applyDifficulty(EASY);
  showIdleScreen();
}

//Program Loop
void loop() {
  mesh.update();                //Keep the mesh updated
  server.handleClient();        //Maintain the mesh network
  printIPsOnceConnected();      //Web requests

  if (gameState == SHOW_SEQ) {
    showSequence();             //Display LCD sequence
  }
}