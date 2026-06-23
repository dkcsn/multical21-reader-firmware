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
    // check meterId
    for (uint8_t i = 0; i< 4; i++)
    {
        if (meterId[i] != payload[6-i])
        {
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
  waterData.alarms.burst = (data[pos_ic] & 0x01) != 0;
  waterData.alarms.leak = (data[pos_ic] & 0x02) != 0;
  waterData.alarms.dry = (data[pos_ic] & 0x04) != 0;
  waterData.alarms.reverse = (data[pos_ic] & 0x08) != 0;
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
  if (!isValid) return;

  uint8_t cipherLength = length - 2 - 16; // cipher starts at index 16, remove 2 crc bytes
  memcpy(cipher, &payload[16], cipherLength);

  memset(iv, 0, sizeof(iv));   // padding with 0
  memcpy(iv, &payload[1], 8);
  iv[8] = payload[10];
  memcpy(&iv[9], &payload[12], 4);

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
