/* 
* ----------------------------------------------
* PROJECT NAME: writing-to-rfid-tag
* Description: programming RFID tags with pos/neg cable identification for jumper cable interactive
* 
* Author: Isai Sanchez
* Date: 8-13-25
* Board Used: Arduino Nano
* Libraries:
*   - Wire.h (I2C communication library): https://docs.arduino.cc/language-reference/en/functions/communication/wire/
*   - MFRC522v2.h (Main RFID library): https://github.com/OSSLibraries/Arduino_MFRC522v2
* Hardware:
*   - Aruino Nano
*   - M5Stack RFID2 reader
* Notes:
*   - Version checking of RFID2 reader bypassed due to WS1850S/MFRC522 differences
* ----------------------------------------------
*/

#include <Wire.h>
#include <MFRC522v2.h>
#include <MFRC522DriverI2C.h>
#include <MFRC522Debug.h>

// ----- CONSTANTS -----
const uint8_t TAG_START_PAGE = 4;  // read/write to tag starting from this page
const uint8_t NUM_TAGS = 4;
bool scanModeActive = false;

// ----- RFID setup  -----
const uint8_t RFID2_WS1850S_ADDR = 0x28;

MFRC522DriverI2C driver{ RFID2_WS1850S_ADDR, Wire };
MFRC522 reader{ driver };

struct JumperCableData {
  char type[4];      // either "POS" or "NEG"
  uint8_t id;        // 1, 2, 3, or 4 (for the 4 cable ends)
  uint8_t checksum;  // simple validation
};

JumperCableData tags[] = {
  { "POS", 1, 0 },
  { "POS", 2, 0 },
  { "NEG", 3, 0 },
  { "NEG", 4, 0 },
};

// ===== UTILITY FUNCTIONS =====
uint8_t calculateChecksum(const uint8_t *data, uint8_t length) {
  uint8_t sum = 0;
  for (uint8_t i = 0; i < length; i++) {
    sum ^= data[i];  // XOR checksum
  }
  return sum;
}

// ===== READ DATA (STRUCT) FROM TAG =====
void readStructFromTag(uint8_t startPage) {
  // ==============================
  // MIFARE_Read(byte blockAddr, byte *buffer, byte *bufferSize)
  // -- byte blockAddr:   replace with page num for NTAG203 tags
  // -- byte *buffer:     a buffer is a temporary array in RAM holding the bytes we read from the tag,
  //                      pass in a pointer to the first element of this array, so the function can fill
  //                      it with the tag's data
  // -- byte *bufferSize: size of the buffer, passed as reference to allow func to update it and tell us
  //                      how many bites it wrote into our buffer array
  //
  // NOTE: function returns 16 bytes (+ 2 bytes CRC_A) from the active PICC
  // -  active PICC means tag must be in the selected state (awake and communicating)
  // -  since function reads 16 bytes, and pages on our NTAG203 tags have 4 bytes each, this will read
  // -    4 pages on one go and store the information in our buffer (make it at least 18 though for the CRC bytes)
  //
  // RETURNS: a status code if read was successful
  // ==============================
  byte buffer[18];  // must be at least 18
  byte bufferSize = sizeof(buffer);
  uint8_t rawData[sizeof(JumperCableData)];

  Serial.println("Reading Pages 4 and 5 to reconstruct data");
  if (reader.MIFARE_Read(startPage, buffer, &bufferSize) == MFRC522::StatusCode::STATUS_OK) {
    // only copy the first n = sizeof(rawData) bytes from buffer to rawData (MIFARE_Read returns 16 bytes, we might not use all of em)
    memcpy(rawData, buffer, sizeof(rawData));

    // cast reconstructed/raw data into JumperCableData struct
    JumperCableData data;
    memcpy(&data, rawData, sizeof(data));

    Serial.print("Type: ");
    Serial.println(data.type);
    Serial.print("ID: ");
    Serial.println(data.id);
    Serial.print("Checksum: ");
    Serial.println(data.checksum, HEX);

    // checksum validation
    uint8_t expectedChecksum = calculateChecksum((uint8_t *)&data, sizeof(data) - 1);
    if (expectedChecksum != data.checksum) {
      Serial.println("❌ Checksum mismatch! Data may be corrupted.");
    } else {
      Serial.println("✅ Checksum valid.");
    }
  } else {
    Serial.println("Failed to read from tag");
  }

  reader.PICC_HaltA();
  reader.PCD_StopCrypto1();
}

// ===== WRITE STRUCT TO TAG =====
void writeStructToTag(const JumperCableData &data, uint8_t startPage) {
  // copy struct into byte array
  uint8_t byte_array[sizeof(data)];
  memcpy(byte_array, &data, sizeof(data));

  // calculate how many pages we will need to write to
  uint8_t totalPages = (sizeof(data) + 3) / 4;  // round up to nearest 4 bytes

  for (uint8_t i = 0; i < totalPages; i++) {
    uint8_t pageData[4] = { 0x00, 0x00, 0x00, 0x00 };

    // copy 4 bytes from struct into pageData
    for (uint8_t j = 0; j < 4; j++) {
      uint8_t byte_array_index = (i * 4) + j;
      if (byte_array_index < sizeof(data)) {
        pageData[j] = byte_array[byte_array_index];
      }
    }

    // write the 4 bytes to the tag
    // ==============================
    // MIFARE_Ultralight_Write(byte page, byte *buffer, byte bufferSize)
    // writes a 4 byte page to the active MIFARE Ultralight PICC
    // -- byte page:        the page (2-15) to write to
    // -- byte *buffer:     the 4 bytes to write to the PICC
    // -- byte *bufferSize: size of the buffer, must be at least 4 bytes
    //
    // RETURNS: a status code if read was successful
    // ==============================
    MFRC522::StatusCode status = reader.MIFARE_Ultralight_Write(startPage + i, pageData, sizeof(pageData));
    if (status != MFRC522::StatusCode::STATUS_OK) {
      Serial.print("Failed to write page ");
      Serial.println(startPage + i);
    } else {
      Serial.print("Successfully wrote to page ");
      Serial.println(startPage + i);
    }
  }
}

// ===== FUNCTION FOR WRITING MODE =====
// prompts user to place each tag in order to write to
void writeAllTags() {
  for (uint8_t i = 0; i < NUM_TAGS; i++) {
    // Calculate checksum for this tag
    tags[i].checksum = calculateChecksum((uint8_t *)&tags[i], sizeof(tags[i]) - 1);

    Serial.print("\nPlace tag for: ");
    Serial.print(tags[i].type);
    Serial.print(" ");
    Serial.println(tags[i].id);

    // Wait for card
    while (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
      delay(50);
    }

    Serial.println("Tag detected, writing...");
    writeStructToTag(tags[i], TAG_START_PAGE);

    // halt communication with current tag
    reader.PICC_HaltA();
    reader.PCD_StopCrypto1();

    // wait for card removal before moving to the next
    while (reader.PICC_IsNewCardPresent()) {
      delay(5000);
    }
  }
  Serial.println("\n✅ All tags written! Switching to scan mode.");
}

// ===== FUNCTION FOR SCAN MODE =====
// will just keep reading every card it sees
void scanMode() {
  // if no card OR can't read card, skip everything else (return early)
  if (!reader.PICC_IsNewCardPresent() || !reader.PICC_ReadCardSerial()) {
    return;  // no card
  }

  Serial.println("Card detected:");
  readStructFromTag(TAG_START_PAGE);

  // Wait for removal before scanning again
  while (reader.PICC_IsNewCardPresent()) {
    // just wait
  }
}


void setup() {
  Serial.begin(115200);
  Wire.begin();       // Initialize I2C with default SDA and SCL pins.
  reader.PCD_Init();  // Init MFRC522 board.
  delay(100);

  writeAllTags();
  scanModeActive = true;
}

void loop() {
  if (scanModeActive) {
    scanMode();
  }
}
