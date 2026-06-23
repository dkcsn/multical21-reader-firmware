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

void WMBusFrame::parseMeterInfo(uint8_t *data, size_t len, WaterData& waterData)
{
  (void) len;

  // init positions for compact frame
  int pos_tt = 9; // total consumption
  int pos_tg = 13; // target consumption
  int pos_ic = 7; // info codes
  int pos_ft = 17; // flow temp
  int pos_at = 18; // ambient temp

  if (data[2] == 0x78) // long frame
  {
    // overwrite it with long frame positions
    pos_tt = 10;
    pos_tg = 16;
    pos_ic = 6;
    pos_ft = 22;
    pos_at = 25;
  }

  uint32_t tt = data[pos_tt]
              + (data[pos_tt+1] << 8)
              + (data[pos_tt+2] << 16)
              + (data[pos_tt+3] << 24);

  uint32_t tg = data[pos_tg]
              + (data[pos_tg+1] << 8)
              + (data[pos_tg+2] << 16)
              + (data[pos_tg+3] << 24);

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

  Serial.printf("CurrentValue: %d.%03d m3 - ", tt/1000, tt%1000);
  Serial.printf("MonthStartValue: %d.%03d m3 - ", tg/1000, tg%1000);
  Serial.printf("WaterTemp: %d C - ", waterData.waterTemperatureC);
  Serial.printf("RoomTemp: %d C\n\r", waterData.ambientTemperatureC);
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
