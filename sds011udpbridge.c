//
//  main.c
//  sds011udpbridge
//
//  Created by Rene Hexel on 11/1/17.
//  Copyright © 2017 Rene Hexel. All rights reserved.
//

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <math.h>
#include <sys/param.h>
#include <sys/termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include "sds011udpbridge.h"

#define MAXACCUM    8192        // maximum number of measurements to accumulate

uint32_t accum25 = 0;           // pm2.5 values accumulated
uint32_t accum10 = 0;           // pm10 values accumulated
uint16_t naccum = 0;            // number of values accumulated
uint16_t maxaccum = 60;         // number of values to average
uint16_t pm25vals[MAXACCUM];    // individual pm25 values
uint16_t pm10vals[MAXACCUM];    // individual pm10 values

static void usage(const char *cmd)
{
    fprintf(stderr, "Usage: %s [-b baud][-d serial_device][-h host][-p port][-n values][-v]\n", basename((char *)cmd));
    exit(EXIT_FAILURE);
}


static inline int setup_udp(struct sockaddr_in *dest)
{
    int s = socket(PF_INET, SOCK_DGRAM, 0);

    if (s <= 0) { perror("broadcast socket"); return s; }

    int yes = 1;
    if (setsockopt(s, SOL_SOCKET, SO_BROADCAST, &yes, sizeof(yes)) == -1)
        perror("broadcast setsockopt");

    if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1)
        perror("reuse setsockopt");

    memset(dest, 0, sizeof(*dest));

#ifdef BSD
    dest->sin_len = sizeof(*dest);
#endif
    dest->sin_family = AF_INET;
    dest->sin_port = (in_port_t) htons(SDS011_BROADCAST_PORT);
    dest->sin_addr.s_addr = htonl(INADDR_BROADCAST);

    return s;
}


static inline int broadcast_udp(int s, const void *data, size_t size, struct sockaddr_in *dest)
{
    if (sendto(s, data, size, 0, (const struct sockaddr *)dest, sizeof(*dest)) <= 0)
    {
        perror("broadcast sendto");
        return -1;
    }

    return s;
}

static void dump(const uint8_t *buffer, ssize_t n)
{
    int i = 0;
    while (i < n)
    {
        printf("%2.2x", *buffer++ & 0xff);
        if (++i % 26 == 0) putchar('\n');
        else putchar(' ');
    }
    if (i % 26) putchar('\n');
}

volatile static bool quit = false;
volatile static bool hangup = false;

static void terminate(int sig)
{
    quit = true;
    (void) sig;
}

static void trigger_hangup(int sig)
{
    hangup = true;
    (void) sig;
}


int main(int argc, char * const argv[])
{
    const char *device = "/dev/ttyUSB0";
    const char *host = NULL;
    int verbosity = 0;
    speed_t baud = B9600;
    uint16_t port = SDS011_BROADCAST_PORT;

    int ch;
    while ((ch = getopt(argc, argv, "b:d:h:p:v")) != -1) switch(ch)
    {
        case 'b': baud = atoi(optarg);      break;
        case 'd': device = optarg;          break;
        case 'h': host = optarg;            break;
        case 'n': maxaccum = atoi(optarg);  break;
        case 'p': port = atoi(optarg);      break;
        case 'v': verbosity++;              break;
        case '?':
        default: usage(argv[0]);
    }

    signal(SIGQUIT, terminate);
    signal(SIGTERM, terminate);
    signal(SIGINT, terminate);
    signal(SIGHUP, trigger_hangup);

    if (verbosity) printf("Opening '%s'.\n", device);

    int fd = open(device, O_RDWR);
    if (fd < 0)
    {
        perror(device);
        return EXIT_FAILURE;
    }

    struct termios tios;

    if (verbosity) printf("Setting serial speed to '%d' baud.\n", (int)baud);

    if (tcgetattr(fd, &tios) >= 0)
    {
        cfsetspeed(&tios, baud);
        if (tcsetattr(fd, TCSANOW, &tios) < 0)
            perror("Cannot set speed");
    }
    else perror("Cannot get speed");

    if (verbosity) printf("Setting up socket for '%s' port %d.\n", host ? host : "broadcast address", (int)port);

    struct sockaddr_in dest;
    int sock = setup_udp(&dest);

    while (!quit)
    {
        struct pollfd pollfds[1] =
        {
            { fd, POLLERR | POLLHUP | POLLIN, 0 },
//            { sock, POLLERR | POLLHUP | POLLIN, 0 }
        };
        int n = poll(pollfds, sizeof(pollfds)/sizeof(pollfds[0]), -1);
        if (n <= 0)
        {
            if (errno != EAGAIN && errno != EINTR)
            {
                perror("poll");
                shutdown(sock, SHUT_RDWR);
                close(sock);
                close(fd);
                exit(EXIT_FAILURE);
            }
            else if (verbosity)
            {
                perror("poll");
            }
            if (hangup)
            {
                if (verbosity) puts("Sending a break.");
                tcdrain(fd);
                if (tcsendbreak(fd, 0) != 0)
                {
                    perror("send break");
                }
            }
            continue;
        }
        else
        {
            union SDS011Buffer buffer;
            const short revents = pollfds[0].revents;
            if (revents & POLLERR)
            {
                perror(device);
                quit = true;
                break;
            }
            if (revents & POLLHUP)
            {
                printf("Hangup received on '%s'.\n", device);
            }
            if (revents & POLLIN)
            {
                ssize_t nbytes = read(fd, &buffer, sizeof(buffer));
                if (verbosity > 1)
                {
                    printf("Read %ld bytes from '%s'.\n", nbytes, device);
                    if (verbosity > 2) dump(buffer.bytes, nbytes);
                }
                if (nbytes <= 0) perror(device);
                else if (nbytes != sizeof(struct SDS011Data))
                {
                    fprintf(stderr, "Expected %lu bytes but received %ld.\n", sizeof(struct SDS011Data), nbytes);
                }
                else if (!validate_sds011_data(&buffer.data))
                {
                    fputs("SDS011 data did not validate.", stderr);
                    if (verbosity && verbosity <= 2) dump(buffer.bytes, nbytes);
                }
                else
                {
                    const uint16_t pm25val = pm25(&buffer.data);
                    const uint16_t pm10val = pm10(&buffer.data);
                    accum25 += pm25val;
                    accum10 += pm10val;
                    pm25vals[naccum] = pm25val;
                    pm10vals[naccum] = pm10val;
                    if (verbosity > 1)
                        printf("PM2.5 = %u, PM10 = %u\n", (int)pm25val, (int)pm10val);
                    if (++naccum >= maxaccum)
                    {
                        const uint16_t mean25 = accum25 / naccum;
                        const uint16_t mean10 = accum10 / naccum;
                        uint32_t sd25 = 0;
                        uint32_t sd10 = 0;
                        for (int i = 0; i < naccum; i++)
                        {
                            const int16_t d25 = (int16_t)pm25vals[i] - (int16_t)mean25;
                            const int16_t d10 = (int16_t)pm10vals[i] - (int16_t)mean10;
                            const int32_t sq25 = d25 * d25;
                            const int32_t sq10 = d10 * d10;
                            sd25 += sq25;
                            sd10 += sq10;
                        }
                        const uint16_t var25 = sd25 / (naccum-1);
                        const uint16_t var10 = sd10 / (naccum-1);
                        struct SDS011Avg packet =
                        {
                            htons(mean25), htons(mean10),
                            htons(var25), htons(var10),
                            htons(naccum),
                            htons(0)
                        };
                        if (verbosity)
                            printf("avg2.5 = %u +/- %u (var: %u), avg10 = %u +/- %u (var: %u)\n", (int)mean25, (int)sqrt(var25), (int)var25, (int)mean10, (int)sqrt(var10), (int)var10);

                        if (broadcast_udp(sock, &packet, sizeof(packet), &dest) >= 0 ||
                            naccum >= MAXACCUM)
                        {
                            accum25 = 0;
                            accum10 = 0;
                            naccum = 0;
                        }
                    }
                }
            }
            pollfds[0].revents = 0;
        }
    }

    if (verbosity) puts("Exiting.");

    shutdown(sock, SHUT_RDWR);
    close(sock);
    close(fd);

    return EXIT_SUCCESS;
}
