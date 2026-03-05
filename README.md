# Reading/Writing data to NTAG213s for Jumper Cable Interactive

## Overview

This project contains a simple Arduino sketch used to program and verify [Adafruit's NTAG213](https://www.adafruit.com/product/5458) RFID tags for the Jumper Cable interactive for the Auto Shop in Kidopolis at Thanksgiving Point's Museum of Natural Curiosity.

The sketch writes structured data to RFID tags representing the positive and negative jumper cable ends used in the interactive. Each tag stores:

- Cable type (POS or NEG)
- A unique ID for each cable end
- A simple checksum for data validation

The firmware operates in two phases:

1. Write Mode

- Prompts the user to place tags sequentially
- Writes the appropriate cable identification data to each tag

2. Scan Mode

- Continuously scans tags
- Reads the stored struct from tag memory
- Validates the checksum
- Prints the decoded information to Serial

This utility was designed to ensure all exhibit RFID tags are programmed consistently before deployment.

## Hardware

- **Microcontroller**: Arduino Nano
- **RFID Reader**: M5Stack RFID2 module (Uses the WS1850S chip which is more or less compatible with MFRC522 libraries)
- **RFID Tags**: [Adafruit NTAG213](https://www.adafruit.com/product/5458) tags

## Software Architecture

The key point here is the data struct we are writing to the tags

```cpp
struct JumperCableData {
  char type[4];      // either "POS" or "NEG"
  uint8_t id;        // 1, 2, 3, or 4 (for the 4 cable ends)
  uint8_t checksum;  // simple validation
};
```
