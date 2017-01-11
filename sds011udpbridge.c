//
//  main.c
//  sds011udpbridge
//
//  Created by Rene Hexel on 11/1/17.
//  Copyright © 2017 Rene Hexel. All rights reserved.
//

#include <string.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <fcntl.h>
#include <libgen.h>
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <poll.h>
#include <sys/param.h>
#include <sys/termios.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define BROADCAST_PORT  12345   // default broadcast port

static void usage(const char *cmd)
{
    fprintf(stderr, "Usage: %s [-b baud][-d serial_device][-h host][-p port][-v]\n", basename((char *)cmd));
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
    dest->sin_port = (in_port_t) htons(BROADCAST_PORT);
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

static void dump(const char *buffer, ssize_t n)
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
    uint16_t port = BROADCAST_PORT;

    int ch;
    while ((ch = getopt(argc, argv, "b:d:h:p:v")) != -1) switch(ch)
    {
        case 'b': baud = atoi(optarg);  break;
        case 'd': device = optarg;      break;
        case 'h': host = optarg;        break;
        case 'p': port = atoi(optarg);  break;
        case 'v': verbosity++;          break;
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
        struct pollfd pollfds[2] =
        {
            { fd, POLLERR | POLLHUP | POLLIN, 0 },
            { sock, POLLERR | POLLHUP | POLLIN, 0 }
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
            char buffer[4096];
            short revents = pollfds[0].revents;
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
                ssize_t bytes = read(fd, buffer, sizeof(buffer));
                if (verbosity > 1)
                {
                    printf("Read %ld bytes from '%s'.\n", bytes, device);
                    if (verbosity > 2) dump(buffer, bytes);
                }
                if (bytes <= 0) perror(device);
                else broadcast_udp(sock, buffer, bytes, &dest);
            }
            pollfds[0].revents = 0;
            revents = pollfds[1].revents;
            if (revents & POLLERR)
            {
                perror("UDP");
                quit = true;
                break;
            }
            if (revents & POLLHUP)
            {
                puts("Hangup received on UDP.");
            }
            if (revents & POLLIN)
            {
                ssize_t bytes = read(sock, buffer, sizeof(buffer));
                if (verbosity > 1)
                {
                    printf("Received %ld bytes via UDP.\n", bytes);
                    if (verbosity > 2) dump(buffer, bytes);
                }
                if (bytes <= 0) perror("read UDP");
                else if (write(fd, buffer, bytes) != bytes)
                {
                    perror("Write serial");
                }
            }
        }
    }

    if (verbosity) puts("Exiting.");

    shutdown(sock, SHUT_RDWR);
    close(sock);
    close(fd);

    return EXIT_SUCCESS;
}
