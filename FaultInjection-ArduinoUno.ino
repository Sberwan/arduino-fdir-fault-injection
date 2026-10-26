#include <stdint.h>
#include <avr/pgmspace.h>

const unsigned long SERIAL_BAUD = 9600;
const uint8_t BUFFER_SIZE = 32;

// A bounded SRAM area for the experiment.
// We will NOT write to arbitrary SRAM addresses.
volatile uint8_t experimentMemory[BUFFER_SIZE];
uint8_t expectedData[BUFFER_SIZE];

// A test string stored in SRAM, with its reference in program memory.
volatile char testString[] = "Hello world";
const char expectedTestString[] PROGMEM = "Hello world";

volatile uint8_t* flipPointer = &experimentMemory[0];
uint8_t pointerIndex = 0;

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(500);

  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    expectedData[i] = 42 + i;
    experimentMemory[i] = expectedData[i];
  }

  Serial.println(F("READY"));
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil(';');
    command.trim();
    executeCommand(command);
  }
}

void executeCommand(String command) {
  if (command == "P") {
    Serial.println(F("PONG"));
    return;
  }

  if (command == "INFO") {
    Serial.print(F("INFO,"));
    Serial.print(BUFFER_SIZE);
    Serial.print(F(",0x"));
    Serial.println((unsigned int)(uintptr_t)&experimentMemory[0], HEX);
    return;
  }

  if (command == "O") {
    reportPointer();
    return;
  }

  if (command == "T") {
    checkTestString();
    return;
  }

  unsigned int index;
  unsigned int bit;
  unsigned int value;

  if (sscanf(command.c_str(), "M,%u", &index) == 1) {
    movePointer(index);
    return;
  }

  if (sscanf(command.c_str(), "F,%u", &bit) == 1) {
    flipBit(bit);
    return;
  }

  if (sscanf(command.c_str(), "S,%u", &value) == 1) {
    setCurrentValue(value);
    return;
  }

  Serial.println(F("ERROR,UNKNOWN_COMMAND"));
}

void movePointer(unsigned int index) {
  if (index >= BUFFER_SIZE) {
    Serial.println(F("ERROR,INVALID_INDEX"));
    return;
  }

  pointerIndex = index;
  flipPointer = &experimentMemory[pointerIndex];

  bool pointerOK =
    (flipPointer == &experimentMemory[pointerIndex]);

  Serial.print(F("MOVED,"));
  Serial.print(pointerIndex);
  Serial.print(F(",0x"));
  Serial.print((unsigned int)(uintptr_t)flipPointer, HEX);
  Serial.print(',');
  Serial.println(pointerOK ? 1 : 0);
}

void reportPointer() {
  bool pointerOK =
    (flipPointer == &experimentMemory[pointerIndex]);

  Serial.print(F("POINTER,"));
  Serial.print(pointerIndex);
  Serial.print(F(",0x"));
  Serial.print((unsigned int)(uintptr_t)flipPointer, HEX);
  Serial.print(',');
  Serial.print(*flipPointer);
  Serial.print(',');
  Serial.print(expectedData[pointerIndex]);
  Serial.print(',');
  Serial.println(pointerOK ? 1 : 0);
}

void flipBit(unsigned int bit) {
  if (bit > 7) {
    Serial.println(F("ERROR,INVALID_BIT"));
    return;
  }

  uint8_t beforeValue = *flipPointer;

  // Flip exactly one selected bit and leave the fault in place.
  *flipPointer ^= (uint8_t)(1U << bit);

  uint8_t afterValue = *flipPointer;

  Serial.print(F("FLIPPED,"));
  Serial.print(pointerIndex);
  Serial.print(',');
  Serial.print(bit);
  Serial.print(',');
  Serial.print(beforeValue);
  Serial.print(',');
  Serial.println(afterValue);
}

void setCurrentValue(unsigned int value) {
  if (value > 255) {
    Serial.println(F("ERROR,INVALID_VALUE"));
    return;
  }

  *flipPointer = (uint8_t)value;

  Serial.print(F("SET,"));
  Serial.print(pointerIndex);
  Serial.print(',');
  Serial.println(*flipPointer);
}

void checkTestString() {
  bool intact = true;

  for (uint8_t i = 0; i < sizeof(testString); i++) {
    char expected = (char)pgm_read_byte(&expectedTestString[i]);

    if (testString[i] != expected) {
      intact = false;
      break;
    }
  }

  Serial.print(F("TEST,"));
  for (uint8_t i = 0; i < sizeof(testString) - 1; i++) {
    Serial.print(testString[i]);
  }
  Serial.print(',');
  Serial.println(intact ? F("OK") : F("CORRUPTED"));
}





/* WORKGIN VERSION OF THE 100 PRE PLANNED FAULT INJECTIONS IN 1 ARDUINO BOARD
#include <stdint.h>
#include <stdio.h>

const unsigned long SERIAL_BAUD = 9600;
const uint8_t BUFFER_SIZE = 32;

// The buffer being tested: 32 bytes in SRAM.
volatile uint8_t testData[BUFFER_SIZE];

// Expected values kept separately for comparison and recovery.
uint8_t expectedData[BUFFER_SIZE];

void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.setTimeout(500);

  for (uint8_t i = 0; i < BUFFER_SIZE; i++) {
    expectedData[i] = 42 + i;
    testData[i] = expectedData[i];
  }

  Serial.println(F("READY"));
  Serial.println(F("Commands: P; INFO; F,index,bit;"));
}

void loop() {
  if (Serial.available() > 0) {
    String command = Serial.readStringUntil(';');
    command.trim();
    executeCommand(command);
  }
}

void executeCommand(String command) {
  if (command == "P") {
    Serial.println(F("PONG"));
    return;
  }

  if (command == "INFO") {
    Serial.print(F("BUFFER_SIZE,"));
    Serial.println(BUFFER_SIZE);
    Serial.print(F("BUFFER_BASE,0x"));
    Serial.println((unsigned int)(uintptr_t)&testData[0], HEX);
    return;
  }

  unsigned int index;
  unsigned int bit;

  if (sscanf(command.c_str(), "F,%u,%u", &index, &bit) == 2) {
    injectAndVerify(index, bit);
    return;
  }

  Serial.println(F("ERROR,UNKNOWN_COMMAND"));
}

void injectAndVerify(unsigned int index, unsigned int bit) {
  if (index >= BUFFER_SIZE || bit > 7) {
    Serial.println(F("ERROR,INVALID_INDEX_OR_BIT"));
    return;
  }

  uint8_t beforeValue = testData[index];
  uint16_t address = (uintptr_t)&testData[index];

  // Flip exactly one selected bit.
  testData[index] ^= (uint8_t)(1U << bit);

  uint8_t afterValue = testData[index];

  // Detection: compare the working data with the expected copy.
  bool detected = (afterValue != expectedData[index]);

  // Recovery: restore the original value.
  if (detected) {
    testData[index] = expectedData[index];
  }

  // Verify recovery independently of the initial detection.
  bool recovered = (testData[index] == expectedData[index]);

  const char* classification;

  if (!detected) {
    classification = "NOT_DETECTED";
  } else if (recovered) {
    classification = "DETECTED_RECOVERED";
  } else {
    classification = "DETECTED_RECOVERY_FAILED";
  }

  // Format:
  // RESULT,index,bit,address,before,after,detected,recovered,class
  Serial.print(F("RESULT,"));
  Serial.print(index);
  Serial.print(',');
  Serial.print(bit);
  Serial.print(F(",0x"));
  Serial.print(address, HEX);
  Serial.print(',');
  Serial.print(beforeValue);
  Serial.print(',');
  Serial.print(afterValue);
  Serial.print(',');
  Serial.print(detected ? 1 : 0);
  Serial.print(',');
  Serial.print(recovered ? 1 : 0);
  Serial.print(',');
  Serial.println(classification);
}
*/