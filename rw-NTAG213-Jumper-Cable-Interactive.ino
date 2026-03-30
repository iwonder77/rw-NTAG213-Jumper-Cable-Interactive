/**
 * Project: Jumper Cable Interactive
 * Description: utility sketch for writing and verifying RFID tags with pos/neg cable
 *              identification for the jumper cable museum interactive
 *
 * Author: Isai Sanchez
 * Date: 8-13-25
 * Hardware:
 *  - MCU: Arduino Nano
 *  - RFID module: M5Stack RFID2 reader
 *  - NTAG213: Adafruit NTAG213 tags
 * Libraries:
 *  - Wire.h (I2C communication library): https://docs.arduino.cc/language-reference/en/functions/communication/wire/
 *  - MFRC522v2.h (Main RFID library): https://github.com/OSSLibraries/Arduino_MFRC522v2
 * Usage:
 * 1) Connect a single RFID2 (WS1850S) reader over I2C
 * 2) Open Serial Monitor at 115200 baud
 * 3) Select an option from the menu
 * 4) Place a blank NTAG213 tag on the reader when prompted
 * 5) Wait for write + verification to complete before removing
 * Notes:
 *  - Version checking of RFID2 reader bypassed due to WS1850S/MFRC522 differences
 *  - Scan mode functionality implemented to ensure tag data was properly set
 *
 * (c) Thanksgiving Point Exhibits Electronics Team — 2025
*/

#include <Wire.h>
#include <MFRC522v2.h>
#include <MFRC522DriverI2C.h>
#include <MFRC522Debug.h>

// ----- APP STATE -----
enum class Mode {
  MENU,
  WRITE_POS1,
  WRITE_POS2,
  WRITE_NEG3,
  WRITE_NEG4,
  SCAN
};
Mode currentMode = Mode::MENU;

// ----- CONSTANTS -----
const uint8_t TAG_START_PAGE = 4;  // read/write to tag starting from this page

// ----- RFID HARDWARE SETUP -----
const uint8_t RFID2_WS1850S_ADDR = 0x28;

MFRC522DriverI2C driver{ RFID2_WS1850S_ADDR, Wire };
MFRC522 reader{ driver };

// ----- TAG DATA -----
struct JumperCableData {
  char type[4];      // "POS" or "NEG"
  uint8_t id;        // 1, 2, 3, or 4 (for the 4 cable ends)
  uint8_t checksum;  // XOR validation
};

// =====================================================================
// UTILITY FUNCTIONS
// =====================================================================

uint8_t calculateChecksum(const uint8_t *data, uint8_t length) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < length; i++) {
    sum ^= data[i];  // XOR checksum
  }
  return sum;
}

void printMenu() {
  Serial.println();
  Serial.println("====== RFID Tag Writer ======");
  Serial.println("1) Write POS-1 tag");
  Serial.println("2) Write POS-2 tag");
  Serial.println("3) Write NEG-3 tag");
  Serial.println("4) Write NEG-4 tag");
  Serial.println("5) Scan tag");
  Serial.println("=============================");
  Serial.print("Enter choice: ");
}

// =====================================================================
// TAG DATA HELPERS
// =====================================================================

/** Build a JumperCableData struct with checksum filled in. */
JumperCableData createTag(const char *type, uint8_t id) {
  JumperCableData tag;
  strncpy(tag.type, type, sizeof(tag.type));
  tag.id = id;
  tag.checksum = calculateChecksum((uint8_t *)&tag, sizeof(tag) - 1);
  return tag;
}

/**
 * Read raw bytes from TAG_START_PAGE into buffer.
 * Does NOT halt the tag — caller manages the tag lifecycle.
 * ==============================
 * MIFARE_Read(byte blockAddr, byte *buffer, byte *bufferSize)
 * -- byte blockAddr:   replace with page num for NTAG213 tags
 * -- byte *buffer:     a buffer is a temporary array in RAM holding the bytes we read from the tag,
 *                      pass in a pointer to the first element of this array, so the function can fill
 *                      it with the tag's data
 * -- byte *bufferSize: size of the buffer, passed as reference to allow func to update it and tell us
 *                      how many bites it wrote into our buffer array
 * RETURNS: a status code if read was successful
 * ==============================
 * NOTE: MIFARE_Read always returns 16 bytes (4 pages) + 2 CRC bytes,
 * so the buffer passed here MUST be at least 18 bytes.
 */
bool readRawPage(uint8_t page, byte *buffer, byte bufferSize) {
  byte size = bufferSize;
  return reader.MIFARE_Read(page, buffer, &size) == MFRC522::StatusCode::STATUS_OK;
}

/**
 * Parse a JumperCableData struct from a raw byte buffer.
 * Validates checksum. Even on failure the struct is populated via memcpy so the
 * caller can inspect fields for diagnostics.
 */
bool parseTagData(const byte *buffer, JumperCableData &out) {
  memcpy(&out, buffer, sizeof(JumperCableData));

  uint8_t expected = calculateChecksum((uint8_t *)&out, sizeof(out) - 1);
  return expected == out.checksum;
}

/** Print validated tag data to Serial. */
void printTagData(const JumperCableData &data) {
  Serial.print("  Type:     ");
  Serial.println(data.type);
  Serial.print("  ID:       ");
  Serial.println(data.id);
  Serial.println("  Checksum: valid");
}

/**
 * Write a JumperCableData struct to TAG_START_PAGE, spanning pages as needed.
 * JumperCableData is 6 bytes, so it occupies 2 pages (pages 4 and 5).
 * Does NOT halt the tag — caller manages the tag lifecycle.
 *
 * ==============================
 * MIFARE_Ultralight_Write(byte page, byte *buffer, byte bufferSize)
 * writes a 4 byte page to the active MIFARE Ultralight PICC
 *  -- byte page:        the page (2-15) to write to
 *  -- byte *buffer:     the 4 bytes to write to the PICC
 *  -- byte *bufferSize: size of the buffer, must be at least 4 bytes
 *  RETURNS: a status code if read was successful
 *  ==============================
 */
bool writeTagData(const JumperCableData &data) {
  uint8_t byte_array[sizeof(data)];
  memcpy(byte_array, &data, sizeof(data));

  uint8_t totalPages = (sizeof(data) + 3) / 4;  // round up to nearest page

  for (uint8_t i = 0; i < totalPages; i++) {
    uint8_t pageData[4] = { 0x00, 0x00, 0x00, 0x00 };
    for (uint8_t j = 0; j < 4; j++) {
      uint8_t idx = (i * 4) + j;
      if (idx < sizeof(data)) {
        pageData[j] = byte_array[idx];
      }
    }

    MFRC522::StatusCode status =
      reader.MIFARE_Ultralight_Write(TAG_START_PAGE + i, pageData, sizeof(pageData));
    if (status != MFRC522::StatusCode::STATUS_OK) {
      Serial.print("  Failed to write page ");
      Serial.println(TAG_START_PAGE + i);
      return false;
    }
  }
  return true;
}

/**
 * Read back TAG_START_PAGE and compare raw bytes against expected data.
 * Does NOT halt the tag — caller manages the tag lifecycle.
 */
bool verifyWrite(const JumperCableData &expected) {
  byte buffer[18];
  if (!readRawPage(TAG_START_PAGE, buffer, sizeof(buffer))) {
    return false;
  }

  uint8_t expectedBytes[sizeof(expected)];
  memcpy(expectedBytes, &expected, sizeof(expected));
  return memcmp(buffer, expectedBytes, sizeof(expected)) == 0;
}

/** End communication with the current tag. */
void releaseTag() {
  reader.PICC_HaltA();
  reader.PCD_StopCrypto1();
}

/** Block until the tag is removed from the reader. */
void waitForTagRemoval() {
  Serial.println("Remove tag.");
  while (reader.PICC_IsNewCardPresent()) {
    delay(50);
  }
}

// =====================================================================
// WRITE MODE
// =====================================================================

void writeSingleTag(const char *type, uint8_t id) {
  Serial.println();
  Serial.print("Place tag to write: ");
  Serial.print(type);
  Serial.print("-");
  Serial.println(id);

  // wait for a tag to appear
  while (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    delay(50);
  }
  Serial.println("Tag detected.");

  JumperCableData tag = createTag(type, id);

  if (!writeTagData(tag)) {
    Serial.println("FAILED: could not write to tag.");
    releaseTag();
    waitForTagRemoval();
    currentMode = Mode::MENU;
    printMenu();
    return;
  }
  Serial.println("Write complete.");

  // read back and verify byte-for-byte
  delay(10);  // brief settle time before read-back
  Serial.print("Verifying... ");

  if (verifyWrite(tag)) {
    Serial.println("OK");
    printTagData(tag);
  } else {
    Serial.println("FAILED");
    Serial.println("Read-back does not match written data. Try again.");
  }

  releaseTag();
  waitForTagRemoval();

  Serial.println("Done.");
  currentMode = Mode::MENU;
  printMenu();
}

// =====================================================================
// SCAN MODE
// =====================================================================

void scanMode() {
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    return;
  }

  Serial.println();
  Serial.println("Tag detected. Reading...");

  byte buffer[18];
  if (!readRawPage(TAG_START_PAGE, buffer, sizeof(buffer))) {
    Serial.println("Failed to read tag data.");
    releaseTag();
    waitForTagRemoval();
    return;
  }

  JumperCableData data;
  if (!parseTagData(buffer, data)) {
    Serial.println("Checksum mismatch — data may be corrupted.");
    releaseTag();
    waitForTagRemoval();
    return;
  }

  Serial.println("Jumper Cable tag:");
  printTagData(data);

  releaseTag();
  waitForTagRemoval();
}

// =====================================================================
// SETUP & LOOP
// =====================================================================

void setup() {
  Serial.begin(115200);
  while (!Serial);
  Wire.begin();
  reader.PCD_Init();
  delay(100);

  printMenu();
}

void loop() {
  if (currentMode == Mode::MENU) {
    if (Serial.available()) {
      char choice = Serial.read();

      // ignore newline / carriage return from serial monitor
      if (choice == '\n' || choice == '\r') return;

      switch (choice) {
        case '1': currentMode = Mode::WRITE_POS1; break;
        case '2': currentMode = Mode::WRITE_POS2; break;
        case '3': currentMode = Mode::WRITE_NEG3; break;
        case '4': currentMode = Mode::WRITE_NEG4; break;
        case '5':
          Serial.println("Entering scan mode. Press 'm' to return to menu.");
          currentMode = Mode::SCAN;
          break;
        default:
          Serial.print("Unknown option: '");
          Serial.print(choice);
          Serial.println("'");
          printMenu();
          break;
      }
    }
  }

  else if (currentMode == Mode::WRITE_POS1) {
    writeSingleTag("POS", 1);
  } else if (currentMode == Mode::WRITE_POS2) {
    writeSingleTag("POS", 2);
  } else if (currentMode == Mode::WRITE_NEG3) {
    writeSingleTag("NEG", 3);
  } else if (currentMode == Mode::WRITE_NEG4) {
    writeSingleTag("NEG", 4);
  }

  else if (currentMode == Mode::SCAN) {
    scanMode();

    if (Serial.available()) {
      char c = Serial.read();
      if (c == 'm' || c == 'M') {
        currentMode = Mode::MENU;
        printMenu();
      }
    }
  }
}
