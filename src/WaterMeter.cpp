/*
 Copyright (C) 2020 chester4444@wolke7.net
 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.
 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.
 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "WaterMeter.h"
#include "DebugLog.h"
#include "hwconfig.h"
#include <string.h>

volatile boolean packetAvailable = false;
void IRAM_ATTR GD0_ISR(void);

WaterMeter::WaterMeter()
{
}

static int16_t rssiToDbm(uint8_t rssi)
{
  return rssi >= 128 ? ((int16_t) rssi - 256) / 2 - 74 : rssi / 2 - 74;
}

static void setRadioStatus(WaterData& waterData, const char* status)
{
  strncpy(waterData.radioStatus, status, sizeof(waterData.radioStatus) - 1);
  waterData.radioStatus[sizeof(waterData.radioStatus) - 1] = '\0';
}

static bool cc1101VersionLooksValid(uint8_t version)
{
  return version != 0x00 && version != 0xFF;
}

static void debugHexPrefix(const char* label, const uint8_t* data, uint8_t len, uint8_t maxLen)
{
  Debug.print(label);
  Debug.print(" ");
  for (uint8_t i = 0; i < len && i < maxLen; i++) {
    Debug.printf("%02X", data[i]);
  }
  if (len > maxLen) {
    Debug.print("...");
  }
  Debug.println();
}

static int findWmbusSync(const uint8_t* data, uint8_t len)
{
  for (uint8_t i = 0; i + 2 < len; i++) {
    if (data[i] == 0x54 && data[i + 1] == 0x3D) {
      return i;
    }
  }
  return -1;
}

// ChipSelect assert
inline void WaterMeter::selectCC1101(void)
{
  digitalWrite(SS, LOW);
}

// ChipSelect deassert
inline void WaterMeter::deselectCC1101(void)
{
  digitalWrite(SS, HIGH);
}

// wait for MISO pulling down
inline void WaterMeter::waitMiso(void)
{
  const unsigned long started = micros();
  while (digitalRead(MISO) == HIGH) {
    if (micros() - started > 2000) {
      return;
    }
    delayMicroseconds(1);
  }
}

// write a single register of CC1101
void WaterMeter::writeReg(uint8_t regAddr, uint8_t value) 
{
  selectCC1101();                      // Select CC1101
  waitMiso();                          // Wait until MISO goes low
  SPI.transfer(regAddr);                // Send register address
  SPI.transfer(value);                  // Send value
  deselectCC1101();                    // Deselect CC1101
}

// send a strobe command to CC1101
void WaterMeter::cmdStrobe(uint8_t cmd) 
{
  selectCC1101();                      // Select CC1101
  delayMicroseconds(5);
  waitMiso();                          // Wait until MISO goes low
  SPI.transfer(cmd);                    // Send strobe command
  delayMicroseconds(5);
  deselectCC1101();                    // Deselect CC1101
}

// read CC1101 register (status or configuration)
uint8_t WaterMeter::readReg(uint8_t regAddr, uint8_t regType)
{
  uint8_t addr, val;

  addr = regAddr | regType;
  selectCC1101();                      // Select CC1101
  waitMiso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address
  val = SPI.transfer(0x00);             // Read result
  deselectCC1101();                    // Deselect CC1101

  return val;
}

// 
void WaterMeter::readBurstReg(uint8_t * buffer, uint8_t regAddr, uint8_t len) 
{
  uint8_t addr, i;
  
  addr = regAddr | READ_BURST;
  selectCC1101();                      // Select CC1101
  delayMicroseconds(5);
  waitMiso();                          // Wait until MISO goes low
  SPI.transfer(addr);                   // Send register address
  for(i=0 ; i<len ; i++)
    buffer[i] = SPI.transfer(0x00);     // Read result byte by byte
  delayMicroseconds(2);
  deselectCC1101();                    // Deselect CC1101
}

// power on reset
void WaterMeter::reset(void) 
{
  deselectCC1101();                    // Deselect CC1101
  delayMicroseconds(3);
  
  digitalWrite(MOSI, LOW);
  digitalWrite(SCK, HIGH);		// see CC1101 datasheet 11.3

  selectCC1101();                      // Select CC1101
  delayMicroseconds(3);
  deselectCC1101();                    // Deselect CC1101
  delayMicroseconds(45);		// at least 40 us

  selectCC1101();                      // Select CC1101

  waitMiso();                          // Wait until MISO goes low
  SPI.transfer(CC1101_SRES);            // Send reset command strobe
  waitMiso();                          // Wait until MISO goes low

  deselectCC1101();                    // Deselect CC1101
}

// set IDLE state, flush FIFO and (re)start receiver
void WaterMeter::startReceiver(void)
{
  uint8_t attempts = 0;
  cmdStrobe(CC1101_SIDLE);      // Enter IDLE state
  while (readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) != MARCSTATE_IDLE) {
    if (++attempts > 100) {
      Debug.println("CC1101 failed to enter IDLE");
      return;
    }
    delay(1);
  }
  
  cmdStrobe(CC1101_SFRX);              // flush receive queue
  delay(2);

  attempts = 0;
  cmdStrobe(CC1101_SRX);               // Enter RX state
  while (readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER) != MARCSTATE_RX) {
    if (++attempts > 100) {
      Debug.println("CC1101 failed to enter RX");
      return;
    }
    delay(1);
  }
}

void WaterMeter::restartRadio(void)
{
  Debug.println("Restarting CC1101 receiver");
  detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
  reset();
  initializeRegisters();
  cmdStrobe(CC1101_SCAL);
  delay(1);
  logRadioIdentity();
  packetAvailable = false;
  startReceiver();
  lastHealthCheckMillis = millis();
  attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), GD0_ISR, FALLING);
}

void WaterMeter::checkRadioHealth(WaterData& waterData)
{
  if (millis() - lastHealthCheckMillis < RADIO_HEALTH_INTERVAL_MS) {
    return;
  }
  lastHealthCheckMillis = millis();

  uint8_t marcState = readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER);
  uint8_t rxBytes = readReg(CC1101_RXBYTES, CC1101_STATUS_REGISTER);
  uint8_t rssi = readReg(CC1101_RSSI, CC1101_STATUS_REGISTER);
  uint8_t version = readReg(CC1101_VERSION, CC1101_STATUS_REGISTER);
  waterData.radioVersion = version;
  if (!cc1101VersionLooksValid(version)) {
    waterData.radioPresent = false;
    waterData.radioStarted = false;
    waterData.radioRssiValid = false;
    setRadioStatus(waterData, "CC1101 not detected");
    Debug.printf("CC1101 health failed: invalid VERSION 0x%02X\n\r", version);
    return;
  }
  waterData.radioPresent = true;
  waterData.radioStarted = true;
  setRadioStatus(waterData, "CC1101 receiver running");
  waterData.radioRssiDbm = rssiToDbm(rssi);
  waterData.radioRssiValid = true;

  Debug.printf("CC1101 health: MARC 0x%02X, RX bytes %u, RSSI %d dBm\n\r",
               marcState, rxBytes & 0x7F, waterData.radioRssiDbm);

  if ((rxBytes & 0x80) != 0 || marcState == MARCSTATE_RXFIFO_OVERFLOW) {
    Debug.println("CC1101 RX FIFO overflow");
    restartRadio();
    return;
  }

  if (marcState != MARCSTATE_RX) {
    Debug.printf("CC1101 not in RX state: 0x%02X\n\r", marcState);
    restartRadio();
    return;
  }

  if (lastFrameReceivedMillis > 0 && millis() - lastFrameReceivedMillis > RADIO_RECEIVE_TIMEOUT_MS) {
    Debug.println("CC1101 receive timeout");
    restartRadio();
  }
}

// initialize all the CC1101 registers
void WaterMeter::initializeRegisters(void) 
{
  writeReg(CC1101_IOCFG2, CC1101_DEFVAL_IOCFG2);
  writeReg(CC1101_IOCFG0, CC1101_DEFVAL_IOCFG0);
  writeReg(CC1101_FIFOTHR, CC1101_DEFVAL_FIFOTHR);
  writeReg(CC1101_PKTLEN, CC1101_DEFVAL_PKTLEN);
  writeReg(CC1101_PKTCTRL1, CC1101_DEFVAL_PKTCTRL1);
  writeReg(CC1101_PKTCTRL0, CC1101_DEFVAL_PKTCTRL0);
  writeReg(CC1101_SYNC1, CC1101_DEFVAL_SYNC1);
  writeReg(CC1101_SYNC0, CC1101_DEFVAL_SYNC0);
  writeReg(CC1101_ADDR, CC1101_DEFVAL_ADDR);
  writeReg(CC1101_CHANNR, CC1101_DEFVAL_CHANNR);
  writeReg(CC1101_FSCTRL1, CC1101_DEFVAL_FSCTRL1);
  writeReg(CC1101_FSCTRL0, CC1101_DEFVAL_FSCTRL0);
  writeReg(CC1101_FREQ2, CC1101_DEFVAL_FREQ2);
  writeReg(CC1101_FREQ1, CC1101_DEFVAL_FREQ1);
  writeReg(CC1101_FREQ0, CC1101_DEFVAL_FREQ0);
  writeReg(CC1101_MDMCFG4, CC1101_DEFVAL_MDMCFG4);
  writeReg(CC1101_MDMCFG3, CC1101_DEFVAL_MDMCFG3);
  writeReg(CC1101_MDMCFG2, CC1101_DEFVAL_MDMCFG2);
  writeReg(CC1101_MDMCFG1, CC1101_DEFVAL_MDMCFG1);
  writeReg(CC1101_MDMCFG0, CC1101_DEFVAL_MDMCFG0);
  writeReg(CC1101_DEVIATN, CC1101_DEFVAL_DEVIATN);
  writeReg(CC1101_MCSM1, CC1101_DEFVAL_MCSM1);
  writeReg(CC1101_MCSM0, CC1101_DEFVAL_MCSM0);
  writeReg(CC1101_FOCCFG, CC1101_DEFVAL_FOCCFG);
  writeReg(CC1101_BSCFG, CC1101_DEFVAL_BSCFG);
  writeReg(CC1101_AGCCTRL2, CC1101_DEFVAL_AGCCTRL2);
  writeReg(CC1101_AGCCTRL1, CC1101_DEFVAL_AGCCTRL1);
  writeReg(CC1101_AGCCTRL0, CC1101_DEFVAL_AGCCTRL0);
  writeReg(CC1101_FREND1, CC1101_DEFVAL_FREND1);
  writeReg(CC1101_FREND0, CC1101_DEFVAL_FREND0);
  writeReg(CC1101_FSCAL3, CC1101_DEFVAL_FSCAL3);
  writeReg(CC1101_FSCAL2, CC1101_DEFVAL_FSCAL2);
  writeReg(CC1101_FSCAL1, CC1101_DEFVAL_FSCAL1);
  writeReg(CC1101_FSCAL0, CC1101_DEFVAL_FSCAL0);
  writeReg(CC1101_FSTEST, CC1101_DEFVAL_FSTEST);
  writeReg(CC1101_TEST2, CC1101_DEFVAL_TEST2);
  writeReg(CC1101_TEST1, CC1101_DEFVAL_TEST1);
  writeReg(CC1101_TEST0, CC1101_DEFVAL_TEST0);
}

void WaterMeter::logRadioIdentity(void)
{
  uint8_t partnum = readReg(CC1101_PARTNUM, CC1101_STATUS_REGISTER);
  uint8_t version = readReg(CC1101_VERSION, CC1101_STATUS_REGISTER);
  uint8_t marcState = readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER);
  uint8_t pktStatus = readReg(CC1101_PKTSTATUS, CC1101_STATUS_REGISTER);
  Debug.printf("CC1101 identity: PARTNUM 0x%02X VERSION 0x%02X MARC 0x%02X PKTSTATUS 0x%02X\n\r",
               partnum, version, marcState, pktStatus);
}

bool WaterMeter::detectRadio(WaterData& waterData)
{
  uint8_t partnum = readReg(CC1101_PARTNUM, CC1101_STATUS_REGISTER);
  uint8_t version = readReg(CC1101_VERSION, CC1101_STATUS_REGISTER);
  uint8_t marcState = readReg(CC1101_MARCSTATE, CC1101_STATUS_REGISTER);
  uint8_t pktStatus = readReg(CC1101_PKTSTATUS, CC1101_STATUS_REGISTER);

  waterData.radioPartnum = partnum;
  waterData.radioVersion = version;

  Debug.printf("CC1101 identity: PARTNUM 0x%02X VERSION 0x%02X MARC 0x%02X PKTSTATUS 0x%02X\n\r",
               partnum, version, marcState, pktStatus);

  if (!cc1101VersionLooksValid(version)) {
    waterData.radioPresent = false;
    waterData.radioStarted = false;
    waterData.radioRssiValid = false;
    setRadioStatus(waterData, "CC1101 not detected");
    return false;
  }

  waterData.radioPresent = true;
  waterData.radioStarted = true;
  setRadioStatus(waterData, "CC1101 detected");
  return true;
}

// handle interrupt from CC1101 via GDO0
void GD0_ISR(void) {
  // set the flag that a package is available
  packetAvailable = true;
}

// should be called frequently, handles the ISR flag
// does the frame checking and decryption
bool WaterMeter::readFrame(WaterData& waterData, const AppConfigData& config)
{
  checkRadioHealth(waterData);

  if (packetAvailable)
  {
    Debug.println("CC1101 packet interrupt");
    // Disable wireless reception interrupt
    detachInterrupt(digitalPinToInterrupt(CC1101_GDO0));
 
    // clear the flag
    packetAvailable = false;
 
    WMBusFrame frame(config.meterId, config.encryptionKey);
 
    receive(&frame, waterData);

    // Enable wireless reception interrupt
    attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), GD0_ISR, FALLING);
    if (frame.isValid) {
      lastFrameReceivedMillis = millis();
      Debug.println("WMBus frame accepted");
    } else {
      Debug.println("WMBus frame rejected");
    }
    return frame.isValid;
  }
  return false;
}

// Initialize CC1101 to receive WMBus MODE C1 
bool WaterMeter::begin(WaterData& waterData)
{
  pinMode(SS, OUTPUT);	// SS Pin -> Output
  SPI.begin();                          // Initialize SPI interface
  pinMode(CC1101_GDO0, INPUT);          // Config GDO0 as input

  reset();                              // power on CC1101
  if (!detectRadio(waterData)) {
    Debug.println("CC1101 receiver not started: radio module not detected");
    return false;
  }

  //Serial.println("Setting CC1101 registers");
  initializeRegisters();                // init CC1101 registers

  cmdStrobe(CC1101_SCAL);
  delay(1);
  logRadioIdentity();

  attachInterrupt(digitalPinToInterrupt(CC1101_GDO0), GD0_ISR, FALLING);
  startReceiver();
  lastHealthCheckMillis = millis();
  lastFrameReceivedMillis = millis();
  waterData.radioStarted = true;
  setRadioStatus(waterData, "CC1101 receiver running");
  return true;
}

// reads a single byte from the RX fifo
uint8_t WaterMeter::readByteFromFifo(void)
{
  return readReg(CC1101_RXFIFO, CC1101_CONFIG_REGISTER);
}

// handles a received frame and restart the CC1101 receiver
void WaterMeter::receive(WMBusFrame * frame, WaterData& waterData)
{
  uint8_t rxBytesBefore = readReg(CC1101_RXBYTES, CC1101_STATUS_REGISTER);
  uint8_t rssi = readReg(CC1101_RSSI, CC1101_STATUS_REGISTER);
  waterData.radioPresent = true;
  waterData.radioStarted = true;
  setRadioStatus(waterData, "CC1101 receiving");
  waterData.radioRssiDbm = rssiToDbm(rssi);
  waterData.radioRssiValid = true;
  const bool fifoOverflow = (rxBytesBefore & 0x80) != 0;
  uint8_t fifoBytes = rxBytesBefore & 0x7F;
  if (fifoBytes > 64) {
    fifoBytes = 64;
  }
  Debug.printf("CC1101 RX FIFO before read: %u bytes%s\n\r",
               fifoBytes, fifoOverflow ? " overflow" : "");

  if (fifoBytes < 3) {
    Debug.println("WMBus rejected before decode: RX FIFO too short");
    startReceiver();
    return;
  }

  uint8_t raw[64];
  for (uint8_t i = 0; i < fifoBytes; i++) {
    raw[i] = readByteFromFifo();
  }
  debugHexPrefix("CC1101 FIFO prefix:", raw, fifoBytes, 32);

  int syncOffset = findWmbusSync(raw, fifoBytes);
  if (syncOffset < 0) {
    Debug.println("WMBus rejected before decode: sync 0x543D not found in FIFO");
    startReceiver();
    return;
  }

  if (syncOffset > 0) {
    Debug.printf("WMBus sync found at FIFO offset %d after noise/overflow\n\r", syncOffset);
  }

  uint8_t payloadLength = raw[syncOffset + 2];
  Debug.printf("WMBus raw header: preamble 0x%02X%02X length %u\n\r",
               raw[syncOffset], raw[syncOffset + 1], payloadLength);

  if (payloadLength >= WMBusFrame::MAX_LENGTH) {
    Debug.println("WMBus rejected before decode: payload too long");
    startReceiver();
    return;
  }

  if ((uint16_t) syncOffset + 3 + payloadLength > fifoBytes) {
    Debug.printf("WMBus rejected before decode: incomplete FIFO frame need %u bytes got %u\n\r",
                 (unsigned) (syncOffset + 3 + payloadLength), fifoBytes);
    startReceiver();
    return;
  }

  frame->length = payloadLength;
  memcpy(frame->payload, raw + syncOffset + 3, payloadLength);
  debugHexPrefix("WMBus payload prefix:", frame->payload, payloadLength, 24);
  frame->decode(waterData);

  // flush RX fifo and restart receiver
  startReceiver();
  //Serial.printf("rxStatus: 0x%02x\n\r", readStatusReg(CC1101_RXBYTES));
}
