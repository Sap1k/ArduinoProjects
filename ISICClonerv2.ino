#include <Wire.h>
#include <SPI.h>
#include <Adafruit_PN532.h>

// If using the breakout or shield with I2C, define just the pins connected
// to the IRQ and reset lines.  Use the values below (2, 3) for the shield!
#define PN532_IRQ   (2)
#define PN532_RESET (3)  // Not connected by default on the NFC Shield

uint8_t keyA[]  = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF }; // Peak security

Adafruit_PN532 nfc(PN532_IRQ, PN532_RESET);

bool readUid(uint8_t *uid, uint8_t *uid_length) {  
  Serial.println("Waiting for card...");

  bool success;
  success = nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, uid, uid_length);

  if (!success) {
    return false;
  }

  if (*uid_length != 4) {
    Serial.println("Only 4B UID tags have been tested to work, proceed with caution!");
  }

  Serial.print("UID: ");
  for (uint8_t i = 0; i < *uid_length; i++) {
    if (uid[i] < 0x10) Serial.print("0");  // leading zero
    Serial.print(uid[i], HEX);
    Serial.print(" ");
  }
  Serial.println();

  return success;
}

char readChoice() {
  // Clear any existing garbage
  while (Serial.available()) {
    Serial.read();
  }

  // Wait for real input
  while (true) {
    while (!Serial.available()) {
      // wait
    }

    char c = Serial.read();

    // Ignore CR / LF
    if (c == '\r' || c == '\n') {
      continue;
    }

    // Flush rest of line
    while (Serial.available()) {
      Serial.read();
    }

    return c;
  }
}

void waitForCard() {
  uint8_t tempUid[7];
  uint8_t tempUidLength;

  Serial.println("Remove card from reader...");
  while(nfc.readPassiveTargetID(PN532_MIFARE_ISO14443A, tempUid, tempUidLength, 100)) {
    delay(100);
  }
}

void setup(void) {
  Serial.begin(115200);
  Serial.println("Hello!");

  nfc.begin();

  uint32_t versiondata = nfc.getFirmwareVersion();
  if (!versiondata) {
    Serial.print("Didn't find PN53x board");
    while (1); // halt
  }
  // Got ok data, print it out!
  Serial.print("Found chip PN5"); Serial.println((versiondata>>24) & 0xFF, HEX);
  Serial.print("Firmware ver. "); Serial.print((versiondata>>16) & 0xFF, DEC);
  Serial.print('.'); Serial.println((versiondata>>8) & 0xFF, DEC);

  Serial.println("PN532 init done...");
}


void loop(void) {
  uint8_t uid[2][7];
  uint8_t uidLength[2];
  uint8_t block0[16];
  uint8_t verBlock0[16];
  char choice;
  bool success;

  waitForCard();
  Serial.println("=== CARD A ===");
  // Read CardA UID
  success = readUid(uid[0], &uidLength[0]);
  if (!success) {
    Serial.println("No card found/Read failure");
    return;
  }

  // Authenticate CardA reads
  success = nfc.mifareclassic_AuthenticateBlock(uid[0], uidLength[0], 0, 0, keyA);
  if (!success) {
    Serial.println("B0 auth failed!");
    return;
  }

  // Read CardA B0
  success = nfc.mifareclassic_ReadDataBlock(0, block0);
  if (!success) { 
    Serial.println("B0 read failed!"); 
    return;
  }

  // Print B0
  Serial.println("Block 0: ");
  for (uint8_t i = 0; i < sizeof(block0); i++) {
    if (block0[i] < 0x10) Serial.print("0");  // leading zero
    Serial.print(block0[i], HEX);
    Serial.print(" ");
  }
  Serial.println();
  Serial.println();

  waitForCard();

  Serial.print("Do you want to clone this B0 to a CUIDv2 tag? (Y/N): ");
  choice = readChoice();
  Serial.println(choice);

  if (choice != 'Y' && choice != 'y') {
    return;
  }
  
  Serial.println("=== CARD B (MagicTag CUID v2) ===");
  // Read CardB UID
  success = readUid(uid[1], &uidLength[1]);
  if (!success) {
    Serial.println("No card found/Read failure");
    return;
  }

  // Check if CardA and CardB share UID lengths
  if (uidLength[0] != uidLength[1]) {
    Serial.println("You cannot clone to a tag with a different UID length!");
    return;
  }

  // Check if CardA and CardB UIDs already match
  if (memcmp(uid[0], uid[1], uidLength[0]) == 0) {
    Serial.print("The UIDs of both your presented tags match! Are you sure you want to continue? (Y/N): ");
    choice = readChoice();
    Serial.println(choice);

    if (choice != 'Y' && choice != 'y') {
      return;
    }
  }

  // Authenticate CardB
  success = nfc.mifareclassic_AuthenticateBlock(uid[1], uidLength[1], 0, 0, keyA);
  if (!success) {
    Serial.println("B0 Stage1 auth failed!");
    return;
  }

  // Ensure BCC integrity if 4B UID
  if (uidLength[0] == 4) {
    block0[4] = block0[0] ^ block0[1] ^ block0[2] ^ block0[3];
  }

  // Try writing B0 to CardB
  success = nfc.mifareclassic_WriteDataBlock(0, block0);
  if (!success) { 
    Serial.println("B0 write failed! Is this a DirectWrite MagicTag?"); 
    return;
  }

  // Verify that B0 actually updated
  success = nfc.mifareclassic_ReadDataBlock(0, verBlock0);
  if (!success) { 
    Serial.println("B0 Stage2 read failed! The chip has likely crashed due to the write - is this a DirectWrite MagicTag?"); 
    return;
  }

  if (memcmp(block0, verBlock0, sizeof(block0)) != 0) {
    Serial.println("The block on the card did not update! Is this a DirectWrite MagicTag?");
    return;
  }

  Serial.println("Tag written successfully!");
  Serial.println("Ready to clone another card!");
}
