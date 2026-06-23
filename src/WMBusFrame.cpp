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


#include "WMbusFrame.h"
#include "DebugLog.h"

#define CRC16_EN_13757 0x3D65

static void debugHexBytes(const char* label, const uint8_t* data, size_t len, size_t maxLen)
{
  Debug.print(label);
  Debug.print(" ");
  for (size_t i = 0; i < len && i < maxLen; i++) {
    Debug.printf("%02X", data[i]);
  }
  if (len > maxLen) {
    Debug.print("...");
  }
  Debug.println();
}

WMBusFrame::WMBusFrame(const uint8_t meterId[4], const uint8_t key[16])
{
  memcpy(this->meterId, meterId, sizeof(this->meterId));
#if defined(ESP32)
  memcpy(this->key, key, sizeof(this->key));
#else
  aes128.setKey(key, 16);
#endif
}

void WMBusFrame::check()
{
    if (length < 18) {
      Debug.printf("WMBus frame too short for encrypted Multical payload: %u bytes\n\r", length);
      isValid = false;
      return;
    }

    // check meterId
    for (uint8_t i = 0; i< 4; i++)
    {
        if (meterId[i] != payload[6-i])
        {
          Debug.printf("Meter serial mismatch at byte %u: expected 0x%02X got 0x%02X\n\r",
                       i, meterId[i], payload[6-i]);
          Debug.printf("Configured meter serial: %02X%02X%02X%02X, frame meter serial: %02X%02X%02X%02X\n\r",
                       meterId[0], meterId[1], meterId[2], meterId[3],
                       payload[6], payload[5], payload[4], payload[3]);
          isValid = false;
          return;
        }
    }

    // TBD: check crc
    isValid = true;
}

uint16_t WMBusFrame::crc16EN13757PerByte(uint16_t crc, uint8_t b)
{
  for (uint8_t i = 0; i < 8; i++) {
    if (((crc & 0x8000) >> 8) ^ (b & 0x80)) {
      crc = (crc << 1) ^ CRC16_EN_13757;
    } else {
      crc = crc << 1;
    }
    b <<= 1;
  }
  return crc;
}

uint16_t WMBusFrame::crc16EN13757(uint8_t *data, size_t len)
{
  uint16_t crc = 0x0000;
  for (size_t i = 0; i < len; i++) {
    crc = crc16EN13757PerByte(crc, data[i]);
  }
  return ~crc;
}

bool WMBusFrame::validatePlaintextCrc(uint8_t *data, size_t len)
{
  if (len < 4) {
    Debug.println("Plaintext too short for EN13757 CRC");
    return false;
  }

  uint16_t readCrc = ((uint16_t) data[1] << 8) | data[0];
  uint16_t calcCrc = crc16EN13757(data + 2, len - 2);
  if (readCrc != calcCrc) {
    Debug.printf("Plaintext CRC mismatch: read 0x%04X calc 0x%04X\n\r", readCrc, calcCrc);
    return false;
  }
  return true;
}

static uint32_t readLe32(const uint8_t *data, int pos)
{
  return (uint32_t) data[pos]
       | ((uint32_t) data[pos + 1] << 8)
       | ((uint32_t) data[pos + 2] << 16)
       | ((uint32_t) data[pos + 3] << 24);
}

static uint16_t readLe16(const uint8_t *data, int pos)
{
  return (uint16_t) data[pos]
       | ((uint16_t) data[pos + 1] << 8);
}

void WMBusFrame::applyStatus(uint16_t status, WaterData& waterData)
{
  waterData.alarms.dry = (status & 0x01) != 0;
  waterData.alarms.reverse = (status & 0x02) != 0;
  waterData.alarms.leak = (status & 0x04) != 0;
  waterData.alarms.burst = (status & 0x08) != 0;
}

bool WMBusFrame::parseKamwaterDifVif(uint8_t *data, size_t len, WaterData& waterData)
{
  bool hasTotal = false;
  bool hasStatus = false;
  bool hasWaterTemp = false;
  bool hasAmbientTemp = false;
  uint16_t status = 0;

  for (size_t i = 0; i < len; i++) {
    if (i + 4 < len && data[i] == 0x02 && data[i + 1] == 0xFF && data[i + 2] == 0x20) {
      status = readLe16(data, i + 3);
      hasStatus = true;
      i += 4;
      continue;
    }

    if (i + 6 < len && data[i] == 0x04 && data[i + 1] == 0xFF && data[i + 2] == 0x23) {
      status = readLe32(data, i + 3);
      hasStatus = true;
      i += 6;
      continue;
    }

    if (i + 5 < len && data[i] == 0x04 && data[i + 1] == 0x13) {
      waterData.totalMilliM3 = readLe32(data, i + 2);
      hasTotal = true;
      i += 5;
      continue;
    }

    if (i + 5 < len && data[i] == 0x44 && data[i + 1] == 0x13) {
      waterData.monthStartMilliM3 = readLe32(data, i + 2);
      i += 5;
      continue;
    }

    if (i + 2 < len && data[i] == 0x61 && data[i + 1] == 0x5B) {
      waterData.waterTemperatureC = (int8_t) data[i + 2];
      hasWaterTemp = true;
      i += 2;
      continue;
    }

    if (i + 3 < len && data[i + 1] == 0x01 && data[i + 2] == 0x5B &&
        (data[i] == 0x81 || data[i] == 0x91 || data[i] == 0xA1)) {
      if (!hasWaterTemp) {
        waterData.waterTemperatureC = (int8_t) data[i + 3];
        hasWaterTemp = true;
      }
      i += 3;
      continue;
    }

    if (i + 2 < len && (data[i] == 0x51 || data[i] == 0x61) && data[i + 1] == 0x67) {
      waterData.ambientTemperatureC = (int8_t) data[i + 2];
      hasAmbientTemp = true;
      i += 2;
      continue;
    }

    if (i + 3 < len && data[i + 1] == 0x01 && data[i + 2] == 0x67 &&
        (data[i] == 0x81 || data[i] == 0x91 || data[i] == 0xA1)) {
      if (!hasAmbientTemp) {
        waterData.ambientTemperatureC = (int8_t) data[i + 3];
        hasAmbientTemp = true;
      }
      i += 3;
      continue;
    }
  }

  if (!hasTotal) {
    return false;
  }

  if (hasStatus) {
    applyStatus(status, waterData);
  }
  waterData.lastFrameMillis = millis();
  waterData.valid = true;

  Debug.printf("Kamwater DIF/VIF: CurrentValue: %d.%03d m3 - ",
               waterData.totalMilliM3 / 1000, waterData.totalMilliM3 % 1000);
  Debug.printf("MonthStartValue: %d.%03d m3 - ",
               waterData.monthStartMilliM3 / 1000, waterData.monthStartMilliM3 % 1000);
  Debug.printf("WaterTemp: %d C - ", waterData.waterTemperatureC);
  Debug.printf("RoomTemp: %d C\n\r", waterData.ambientTemperatureC);
  return true;
}

void WMBusFrame::parseMeterInfo(uint8_t *data, size_t len, WaterData& waterData)
{
  // init positions for compact frame
  int pos_tt = 9; // total consumption
  int pos_tg = 13; // target consumption
  int pos_ic = 7; // info codes
  int pos_ft = 17; // flow temp
  int pos_at = 18; // ambient temp

  if (!validatePlaintextCrc(data, len)) {
    isValid = false;
    Debug.println("Multical decrypt failed or wrong AES key: plaintext CRC invalid");
    return;
  }
  Debug.println("Multical plaintext CRC OK");

  if (parseKamwaterDifVif(data + 2, len - 2, waterData)) {
    return;
  }

  if (data[2] == 0x79 && len >= 17 && len < 19) {
    uint32_t tt = readLe32(data, 9);
    waterData.totalMilliM3 = tt;
    if (waterData.monthStartMilliM3 == 0 || waterData.monthStartMilliM3 > tt) {
      waterData.monthStartMilliM3 = tt;
    }
    waterData.waterTemperatureC = (int8_t) data[15];
    waterData.ambientTemperatureC = (int8_t) data[16];
    applyStatus(data[7], waterData);
    waterData.lastFrameMillis = millis();
    waterData.valid = true;

    Debug.printf("Compact 0x79 short: CurrentValue: %d.%03d m3 - ", tt/1000, tt%1000);
    Debug.printf("WaterTemp: %d C - ", waterData.waterTemperatureC);
    Debug.printf("RoomTemp: %d C\n\r", waterData.ambientTemperatureC);
    return;
  }

  if (data[2] == 0x79) // compact frame
  {
    pos_tt = 9;
    pos_tg = 13;
    pos_ic = 7;
    pos_ft = 17;
    pos_at = 18;
  }
  else if (data[2] == 0x78) // long frame
  {
    pos_tt = 10;
    pos_tg = 16;
    pos_ic = 6;
    pos_ft = 23;
    pos_at = 29;
  }
  else
  {
    Debug.printf("Unsupported Multical payload format: 0x%02X\n\r", data[2]);
    isValid = false;
    return;
  }

  const int maxPos = max(max(pos_tt + 3, pos_tg + 3), max(pos_ft, pos_at));
  if ((size_t) maxPos >= len) {
    Debug.printf("Multical payload too short for format 0x%02X: %u bytes\n\r", data[2], (unsigned) len);
    isValid = false;
    return;
  }

  uint32_t tt = readLe32(data, pos_tt);
  uint32_t tg = readLe32(data, pos_tg);

  waterData.totalMilliM3 = tt;
  waterData.monthStartMilliM3 = tg;
  waterData.waterTemperatureC = (int8_t) data[pos_ft];
  waterData.ambientTemperatureC = (int8_t) data[pos_at];
  applyStatus(data[pos_ic], waterData);
  waterData.lastFrameMillis = millis();
  waterData.valid = true;

  Debug.printf("CurrentValue: %d.%03d m3 - ", tt/1000, tt%1000);
  Debug.printf("MonthStartValue: %d.%03d m3 - ", tg/1000, tg%1000);
  Debug.printf("WaterTemp: %d C - ", waterData.waterTemperatureC);
  Debug.printf("RoomTemp: %d C\n\r", waterData.ambientTemperatureC);
}

void WMBusFrame::decode(WaterData& waterData)
{
  // check meterId, CRC
  check();
  if (!isValid) {
    Debug.println("WMBus check failed before decrypt");
    return;
  }

  uint8_t cipherLength = length - 2 - 16; // cipher starts at index 16, remove 2 crc bytes
  Debug.printf("WMBus decrypt: payload length %u, cipher length %u\n\r", length, cipherLength);
  memcpy(cipher, &payload[16], cipherLength);

  memset(iv, 0, sizeof(iv));   // padding with 0
  memcpy(iv, &payload[1], 8);
  iv[8] = payload[10];
  memcpy(&iv[9], &payload[12], 4);
  debugHexBytes("WMBus AES IV:", iv, sizeof(iv), sizeof(iv));
  debugHexBytes("WMBus cipher prefix:", cipher, cipherLength, 24);

#if defined(ESP32)
  mbedtls_aes_context ctx;
  size_t ncOff = 0;
  uint8_t streamBlock[16] = { 0 };
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ctr(&ctx, cipherLength, &ncOff, iv, streamBlock, cipher, plaintext);
  mbedtls_aes_free(&ctx);
#else
  aes128.setIV(iv, sizeof(iv));
  aes128.decrypt(plaintext, (const uint8_t *) cipher, cipherLength);
#endif

  debugHexBytes("WMBus plaintext prefix:", plaintext, cipherLength, 32);

/*
  Serial.printf("C:     ");
  for (size_t i = 0; i < cipherLength; i++)
  {
    Serial.printf("%02X", cipher[i]);
  }
  Serial.println();
  Serial.printf("P(%d): ", cipherLength);
  for (size_t i = 0; i < cipherLength; i++)
  {
    Serial.printf("%02X", plaintext[i]);
  }
  Serial.println();
*/

  parseMeterInfo(plaintext, cipherLength, waterData);
}
