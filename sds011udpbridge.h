//
//  sds011udpbridge.h
//  sds011udpbridge
//
//  Created by Rene Hexel on 12/1/17.
//  Copyright © 2017 Rene Hexel. All rights reserved.
//

#ifndef sds011udpbridge_h
#define sds011udpbridge_h

#include <stdbool.h>
#include <sys/types.h>

/// default broadcast port
#define SDS011_BROADCAST_PORT   14441

#define SDS011_HEADER       0xAA    // header byte
#define SDS011_COMMANDER    0xC0    // commander byte
#define SDS011_TAIL         0xAB    // trailing byte

/**
 Data sent by the SDS011 sensor over serial at 9600 Baud 8n1
 */
struct SDS011Data
{
    uint8_t message_header; // always 0xAA
    uint8_t commander_no;   // always 0xC0
    uint8_t pm25_lo;        // DATA1: PM2.5 low byte
    uint8_t pm25_hi;        // DATA2: PM2.5 high byte
    uint8_t pm10_lo;        // DATA3: PM10 low byte
    uint8_t pm10_hi;        // DATA4: PM10 high byte
    uint8_t id0;            // DATA5: ID byte 1
    uint8_t id1;            // DATA6: ID byte 2
    uint8_t checksum;       // sum(DATA1:DATA6)
    uint8_t tail;           // always 0xAB
};

/**
 Serial receive buffer
 */
union SDS011Buffer
{
    struct SDS011Data data; // actual data
    uint8_t bytes[32];      // buffer space for discarding extra data
};

/**
 Check that an SDS011 data packet is correct

 @param sds011 pointer to the packet to validate
 @return `true` iff valid
 */
static inline bool validate_sds011_data(const struct SDS011Data *sds011)
{
    if (sds011->message_header != SDS011_HEADER     ||
        sds011->commander_no   != SDS011_COMMANDER  ||
        sds011->tail           != SDS011_TAIL)
        return false;

    const uint8_t sum = sds011->pm25_lo + sds011->pm25_hi +
                        sds011->pm10_lo + sds011->pm10_hi +
                        sds011->id0     + sds011->id1;

    return sds011->checksum == sum;
}

/**
 Return the PM2.5 value stored in the packet

 @param sds011 pointer to the packet to analyse
 @return PM2.5 value
 */
static inline uint16_t pm25(const struct SDS011Data *sds011)
{
    return sds011->pm25_lo + (sds011->pm25_hi << 8);
}

/**
 Return the PM10 value stored in the packet

 @param sds011 pointer to the packet to analyse
 @return PM10 value
 */
static inline uint16_t pm10(const struct SDS011Data *sds011)
{
    return sds011->pm10_lo + (sds011->pm10_hi << 8);
}

/**
 Average data sent via UDP (all fields in network byte order)
 */
struct SDS011Avg
{
    uint16_t mean25;        // arithmetic average of PM2.5 measurements
    uint16_t mean10;        // arithmetic average of PM10 measurements
    uint16_t variance25;    // variance of PM2.5 measurements (nyi)
    uint16_t variance10;    // variance of PM10 measurements (nyi)
    uint16_t measurements;  // number of measurements averaged
    uint8_t  id0;           // sensor ID byte 0
    uint8_t  id1;           // sensor ID byte 1
};

#endif /* sds011udpbridge_h */
