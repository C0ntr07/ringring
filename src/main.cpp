/*

  Doorbell ring button hack to open a door using a secret sequence of
  long and short presses.

*/

#include <EEPROM.h>
#include <ESP8266WebServer.h>
#include <IPAddress.h>
#include <ESP8266WiFi.h>
#include <jled.h>
#include <UniversalTelegramBot.h>
#include <WiFiClientSecure.h>

struct Flags {
  unsigned int notify_door_opened_secret_code:1;
  unsigned int notify_door_opened_web_command:1;
  unsigned int notify_debug_sequence:1;
};

struct Settings {
  byte initialized;
  byte sequence[50];
  byte sequence_length;
  uint8_t last_ip_address[4];
  unsigned int POLLING_INTERVAL;
  unsigned int BUTTON_SHORT_PRESS_TIMEOUT;
  unsigned int BUTTON_RELEASE_TIMEOUT;
  unsigned int PULSE_SPACING_TIMEOUT;
  unsigned int DOOR_OPEN_TIME;
  Flags flags;
};

/************************************
             CONSTANTS
 ************************************/


// Static text messages
const char* MESSAGE_DOOR_OPEN_SECRET_CODE = "Yo! Someone just opened the door with the secret code.";
const char* MESSAGE_DOOR_OPEN_WEB_COMMAND = "Yo! Someone just opened the door with the web command.";


// DEFAULTS
const unsigned int DEFAULT_POLLING_INTERVAL = 50;
const unsigned int DEFAULT_BUTTON_SHORT_PRESS_TIMEOUT = 300;
const unsigned int DEFAULT_BUTTON_RELEASE_TIMEOUT = 800;
const unsigned int DEFAULT_PULSE_SPACING_TIMEOUT = 1000;
const unsigned int DEFAULT_DOOR_OPEN_TIME = 1000;

// Settings related parameters
const byte INITIALIZED_MARKER = 0xB1;
const int EEPROM_SETTINGS_ADDRESS = 0;
const Settings DEFAULT_SETTINGS = {
  INITIALIZED_MARKER,
  {0, 0, 0, 0, 1, 1},
  6,
  {0, 0, 0, 0},
  DEFAULT_POLLING_INTERVAL,
  DEFAULT_BUTTON_SHORT_PRESS_TIMEOUT,
  DEFAULT_BUTTON_RELEASE_TIMEOUT,
  DEFAULT_PULSE_SPACING_TIMEOUT,
  DEFAULT_DOOR_OPEN_TIME,
  {0,1,0}

};

// Hardware pin definitions
const int DOOR_BUTTON_PIN = 14;        // D2 the pin that the door button is attached to
const int DOOR_OPEN_RELAY_PIN = 15;    // D8 the ping the door lock relay is attached to
const int LED_PIN = LED_BUILTIN;

// Button events
const int NO_EVENT = 0;
const int BUTTON_PRESSED = 1;
const int BUTTON_RELEASED = 2;

// Key codes
const byte SHORT = 0;
const byte LONG = 1;

// FSM States
const int STATE_IDLE = 0;
const int STATE_WAITING_RELEASE = 1;
const int STATE_CHECKING_SEQUENCE = 2;
const int STATE_WAITING_NEXT_PRESS = 3;
const int STATE_TIMEOUT = 4;
const int STATE_BAD_SEQUENCE = 5;
const int STATE_GOOD_SEQUENCE = 6;

/************************************
      PROGRAM VARIABLES
 ************************************/

ESP8266WebServer server(80);
WiFiClientSecure client;

UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, client);
auto led_blink = JLed(LED_BUILTIN).LowActive().Breathe(1000).DelayAfter(2000).Forever();
Settings settings;

unsigned int last = 0;
unsigned int elapsed = 0;
unsigned int now = 0;
unsigned int time_since_last_press = 0;
unsigned int time_since_last_release = 0;
unsigned int last_button_press = 0;
unsigned int last_button_release = 0;
byte state = STATE_IDLE;
byte last_button_state = HIGH;
byte sequence[50];
byte sequence_length = 0;
byte push_type;
byte event;


void sendMessage(const char *message) {
  bot.sendMessage(TELEGRAM_CHAT_ID, message);
}

void printSequence(byte *seq, byte seq_length) {
  Serial.print("[ ");
  byte i;
   for (i=0;i<seq_length;i++) {
        if (seq[i] == SHORT) {
          Serial.print("SHORT");
        } else {
          Serial.print("LONG");
        }
        if (i<seq_length-1) {
          Serial.print(", ");
        }
   }

   Serial.println(" ]");
}

void loadSettings() {
    // Read settings from EEPROM
    EEPROM.begin(512);
    EEPROM.get(EEPROM_SETTINGS_ADDRESS, settings);
    Serial.println("Loading settings from EEPROM.");

    if (settings.initialized != INITIALIZED_MARKER) {
        Serial.println("--> First run detected, defaults loaded.");
        EEPROM.put(EEPROM_SETTINGS_ADDRESS, DEFAULT_SETTINGS);
        EEPROM.commit();
        settings = DEFAULT_SETTINGS;
    }
    EEPROM.end();
}

void saveSettings() {
  EEPROM.begin(512);
  EEPROM.put(EEPROM_SETTINGS_ADDRESS, settings);
  EEPROM.commit();
  EEPROM.end();
}

void setupHardware() {
  // initialize the button pin as a input:
  pinMode(DOOR_BUTTON_PIN, INPUT);

  // initialize the LED as an output, powered off:
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  // initialize the door relay pin as an output, relay open (inverse logic):
  pinMode(DOOR_OPEN_RELAY_PIN, OUTPUT);
  digitalWrite(DOOR_OPEN_RELAY_PIN, HIGH);
}

void setupCommunications() {
    client.setInsecure();
    delay(100);
    Serial.println("");

    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    delay(100);

    // Attempt to connect to Wifi network:
    Serial.print("Connecting Wifi: ");
    Serial.println(WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    while (WiFi.status() != WL_CONNECTED) {
        Serial.print(".");
        delay(500);
    }

    Serial.println("");
    Serial.println("WiFi connected");

    Serial.print("Last IP address was: ");
    char string[15];
    sprintf( string, "%u.%u.%u.%u",
      settings.last_ip_address[0],
      settings.last_ip_address[1],
      settings.last_ip_address[2],
      settings.last_ip_address[3]
    );
    Serial.println(string);

    IPAddress current_ip = WiFi.localIP();
    Serial.print("Current IP address: "); Serial.println(current_ip);
    if (
        settings.last_ip_address[0] != current_ip[0] ||
        settings.last_ip_address[1] != current_ip[1] ||
        settings.last_ip_address[2] != current_ip[2] ||
        settings.last_ip_address[3] != current_ip[3]
    ) {
      Serial.print("IP Changed");
      settings.last_ip_address[0] = current_ip[0];
      settings.last_ip_address[1] = current_ip[1];
      settings.last_ip_address[2] = current_ip[2];
      settings.last_ip_address[3] = current_ip[3];
      bot.sendMessage(TELEGRAM_CHAT_ID, "RingRing online at " + current_ip.toString());
      saveSettings();
    }
}

int getButtonEvent() {
  int changed;
  int event = NO_EVENT;
  int button_state;

  button_state = digitalRead(DOOR_BUTTON_PIN);
  changed = button_state != last_button_state;

  if (changed && button_state == LOW) {
    event = BUTTON_PRESSED;
    Serial.println("received LOW, button PRESSED");
  } else if (changed && button_state == HIGH) {
    event = BUTTON_RELEASED;
    Serial.println("received HIGH, button RELEASED");
  } else if (!changed) {
    event = NO_EVENT;
  }

  last_button_state = button_state;
  return event;
}

void initializeSequence() {
  sequence_length = 0;
}

bool sequenceIsFinished() {
    int validSequenceLength = settings.sequence_length / sizeof(byte);
  return validSequenceLength == sequence_length;
}

bool currentSequenceIsValid() {
  int i;
  for (i=0;i<sequence_length;i++) {
    if (settings.sequence[i] != sequence[i]) {
      return false;
    }
  }
  return true;
}


void openDoor() {
    digitalWrite(DOOR_OPEN_RELAY_PIN, LOW);
    delay(settings.DOOR_OPEN_TIME);
    digitalWrite(DOOR_OPEN_RELAY_PIN, HIGH);
    Serial.println("Door opened");
}


void handleAPIOpenDoor () {
    if(!server.authenticate(WWW_USERNAME, WWW_PASSWORD))
        return server.requestAuthentication();

    openDoor();
    if (settings.flags.notify_door_opened_web_command ) {
      sendMessage(MESSAGE_DOOR_OPEN_WEB_COMMAND);
    }

    server.send(200, "text/plain", "Door opened");
}


void handleAPISetKey () {
    if(!server.authenticate(WWW_USERNAME, WWW_PASSWORD))
        return server.requestAuthentication();

    String key = server.arg("value");
    byte i;
    byte sequence[50];
    byte sequence_length = 0;

    if (key != "") {
        for (i=0;i<key.length();i++) {
            if (key[i] == 'S') {
                sequence[sequence_length] = SHORT;
                sequence_length++;
            } else if (key[i] == 'L') {
                sequence[sequence_length] = LONG;
                sequence_length++;
            } else {
                return server.send(200, "text/plain", "Wong char detected");
            }
        }
        memcpy(settings.sequence, sequence, sequence_length);
        settings.sequence_length = sequence_length;
        saveSettings();
       server.send(200, "text/plain", "Code changed");
    } else {
        server.send(200, "text/plain", "Wrong length");
    }
};

void handleAPISetParam () {
    if(!server.authenticate(WWW_USERNAME, WWW_PASSWORD))
        return server.requestAuthentication();

    String short_press_timeout = server.arg("short");
    if (short_press_timeout != "") {
      settings.BUTTON_SHORT_PRESS_TIMEOUT = short_press_timeout.toInt();
    }

    String long_press_timeout = server.arg("long");
    if (long_press_timeout != "") {
      settings.BUTTON_RELEASE_TIMEOUT = long_press_timeout.toInt();
    }

    String pulse_spacing_timeout = server.arg("wait");
    if (pulse_spacing_timeout != "") {
      settings.PULSE_SPACING_TIMEOUT = pulse_spacing_timeout.toInt();
    }

    String notify_door_opened_secret_code = server.arg("notify_open_via_code");
    if (notify_door_opened_secret_code != "") {
      settings.flags.notify_door_opened_secret_code = notify_door_opened_secret_code.toInt();
    }

    String notify_door_opened_web_command = server.arg("notify_open_via_command");
    if (notify_door_opened_web_command != "") {
      settings.flags.notify_door_opened_web_command = notify_door_opened_web_command.toInt();
    }


    saveSettings();
    server.send(200, "text/plain", "Params saved");
};


void setup() {
    Serial.begin(115200);

    loadSettings();
    setupHardware();
    setupCommunications();
    initializeSequence();

    server.on("/door/open", handleAPIOpenDoor);
    server.on("/door/key/set", handleAPISetKey);
    server.on("/param/set", handleAPISetParam);
    server.begin();

    Serial.println("");
    Serial.println("Current settings");
    Serial.println("");
    Serial.print("  Expected Key        : "); printSequence(settings.sequence, settings.sequence_length);
    Serial.print("  Polling interval    : "); Serial.println(settings.POLLING_INTERVAL);
    Serial.print("  Short press (ms)    : "); Serial.println(settings.BUTTON_SHORT_PRESS_TIMEOUT);
    Serial.print("  Long press (ms)     : "); Serial.println(settings.BUTTON_RELEASE_TIMEOUT);
    Serial.print("  Pulse spacing (ms)  : "); Serial.println(settings.PULSE_SPACING_TIMEOUT);
    Serial.print("  Open door hold (ms) : "); Serial.println(settings.DOOR_OPEN_TIME);
    Serial.print("  Notify Key open     : "); Serial.println(settings.flags.notify_door_opened_secret_code);
    Serial.print("  Notify web open     : "); Serial.println(settings.flags.notify_door_opened_web_command);

    Serial.println("");

    Serial.println("Program started, waiting for a sequence...");
}


void loop() {
    led_blink.Update();
    server.handleClient();

    now = millis();
    elapsed = now - last;

    if (elapsed > settings.POLLING_INTERVAL) {
        switch(state) {
            case STATE_IDLE :
                event = getButtonEvent();
                if (event == BUTTON_PRESSED) {
                  state = STATE_WAITING_RELEASE;
                  Serial.println("Starting sequence");
                  last_button_press = now;
                  led_blink.Blink(100, 100).DelayAfter(0).Forever();
                }
                break;
            case STATE_WAITING_RELEASE :
                event = getButtonEvent();
                time_since_last_press = now - last_button_press;
                if (event == BUTTON_RELEASED && time_since_last_press <= settings.BUTTON_RELEASE_TIMEOUT) {
                    last_button_release = now;
                    push_type = (time_since_last_press < settings.BUTTON_SHORT_PRESS_TIMEOUT) ? SHORT : LONG;
                    sequence[sequence_length] = push_type;
                    sequence_length++;
                    state = STATE_CHECKING_SEQUENCE;
                    if (push_type == SHORT) {
                      Serial.print("SHORT");
                    } else {
                      Serial.print("LONG");
                    }
                    Serial.print(", ");
                    Serial.print(time_since_last_press);
                    Serial.println("ms ");
                    Serial.println("Valid release detected, will check current sequence");
                } else if (event == BUTTON_RELEASED && time_since_last_press > settings.BUTTON_RELEASE_TIMEOUT) {
                    last_button_release = now;
                    state = STATE_TIMEOUT;
                    Serial.println("Timeout while waiting for button release");
                }
                break;
            case STATE_WAITING_NEXT_PRESS :
                event = getButtonEvent();
                time_since_last_release = now - last_button_release;

                if (time_since_last_release > settings.BUTTON_RELEASE_TIMEOUT) {
                  state = STATE_TIMEOUT;
                  Serial.println("Timeout while waiting for next sequence item");
                } else if (event == BUTTON_PRESSED) {
                  state = STATE_WAITING_RELEASE;
                  Serial.print("New press detected.");
                  last_button_press = now;
                }
                break;
            case STATE_TIMEOUT :
                initializeSequence();
                state = STATE_IDLE;
                last_button_press = 0;
                Serial.println("Reset & waiting for new sequence");
                Serial.println("-----------------------------------------------------");
                led_blink.Breathe(1000).DelayAfter(2000).Forever();
                break;
            case STATE_CHECKING_SEQUENCE :
                time_since_last_release = now - last_button_release;

                if (!sequenceIsFinished() && currentSequenceIsValid()) {
                    state = STATE_WAITING_NEXT_PRESS;
                    Serial.print("Partial sequence is VALID, waiting for next press");
                    printSequence(sequence, sequence_length);
                } else if (currentSequenceIsValid() && sequenceIsFinished() && time_since_last_release >= settings.PULSE_SPACING_TIMEOUT) {
                    state = STATE_GOOD_SEQUENCE;
                    Serial.print("VALID finished sequence detected! ");
                    printSequence(sequence, sequence_length);
                } else if (!currentSequenceIsValid()) {
                    state = STATE_BAD_SEQUENCE;
                    Serial.print("INVALID sequence detected: ");
                    printSequence(sequence, sequence_length);
                }
                break;
            case STATE_BAD_SEQUENCE :
                initializeSequence();
                state = STATE_IDLE;
                last_button_press = 0;
                Serial.println("Reset & waiting for new sequence");
                Serial.println("-----------------------------------------------------");
                led_blink.Breathe(1000).DelayAfter(2000).Forever();
                break;
            case STATE_GOOD_SEQUENCE :
                initializeSequence();
                Serial.println("Opening door now");
                openDoor();
                if (settings.flags.notify_door_opened_secret_code) {
                  sendMessage(MESSAGE_DOOR_OPEN_SECRET_CODE);
                }
                state = STATE_IDLE;
                last_button_press = 0;
                Serial.println("Reset & waiting for new sequence");
                Serial.println("-----------------------------------------------------");
                led_blink.Breathe(1000).DelayAfter(2000).Forever();
                break;
        }
        last = now;
    }
}
