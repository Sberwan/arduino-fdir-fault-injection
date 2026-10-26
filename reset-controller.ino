const unsigned long BAUD_RATE = 9600;
const uint8_t RESET_PIN = 7;

void setup() {
  // Set the idle output HIGH before enabling output mode.
  digitalWrite(RESET_PIN, HIGH);
  pinMode(RESET_PIN, OUTPUT);

  Serial.begin(BAUD_RATE);
  Serial.setTimeout(500);

  Serial.println(F("RESET_CONTROLLER_READY"));
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil(';');
    command.trim();

    if (command == "P") {
      Serial.println(F("PONG"));
    }
    else if (command == "R") {
      Serial.println(F("RESETTING_PRIMARY"));

      // RESET is active-low.
      digitalWrite(RESET_PIN, LOW);
      delay(250);
      digitalWrite(RESET_PIN, HIGH);

      Serial.println(F("RESET_PULSE_COMPLETE"));
    }
    else {
      Serial.println(F("ERROR,UNKNOWN_COMMAND"));
    }
  }
}