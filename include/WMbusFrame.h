#ifndef __WMBUS_FRAME__
#define __WMBUS_FRAME__

#include <Arduino.h>
#if defined(ESP32)
  #include <mbedtls/aes.h>
#else
  #include <Crypto.h>
  #include <AES.h>
  #include <CTR.h>
#endif
#include "WaterData.h"

class WMBusFrame
{
  public:
    static const uint8_t MAX_LENGTH = 64;
  private:
#if defined(ESP32)
    uint8_t key[16];
#else
    CTR<AESSmall128> aes128;
#endif
    uint8_t meterId[4];
    uint8_t cipher[MAX_LENGTH];
    uint8_t plaintext[MAX_LENGTH];
    uint8_t iv[16];
    void check(void);
    void parseMeterInfo(uint8_t *data, size_t len, WaterData& waterData);

  public:
    // check frame and decrypt it
    void decode(WaterData& waterData);

    // true, if meter information is valid for the last received frame
    bool isValid = false;

    // payload length
    uint8_t length = 0;

    // payload data
    uint8_t payload[MAX_LENGTH];

    // constructor
    WMBusFrame(const uint8_t meterId[4], const uint8_t key[16]);
};

#endif // __WMBUS_FRAME__
