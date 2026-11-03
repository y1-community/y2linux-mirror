/* SPDX-License-Identifier: GPL-2.0-only */
/* Bounded observer; never grabs devices, injects events, or accesses I2C. */
#define _POSIX_C_SOURCE 200809L
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/input.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

static volatile sig_atomic_t stopped;

static void stop_capture(int signal)
{
    (void)signal;
    stopped = 1;
}

static uint64_t mono_us(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) exit(2);
    return (uint64_t)t.tv_sec * 1000000 + (uint64_t)t.tv_nsec / 1000;
}

int main(int argc, char **argv)
{
    char *end = NULL;
    long seconds = argc == 2 ? strtol(argv[1], &end, 10) : 0;
    if (!end || *end || seconds < 1 || seconds > 600) return 2;
    setvbuf(stdout, NULL, _IOLBF, 0);
    struct sigaction stop_action = {.sa_handler = stop_capture};
    sigemptyset(&stop_action.sa_mask);
    if (sigaction(SIGINT, &stop_action, NULL) || sigaction(SIGTERM, &stop_action, NULL))
        return 2;
    struct pollfd fds[32];
    int numbers[32], clocks[32], count = 0;
    for (int n = 0; n < 32; ++n) {
        char path[64], name[256] = {0};
        snprintf(path, sizeof(path), "/dev/input/event%d", n);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0 ||
            (strcmp(name, "APT32F click-wheel") && strcmp(name, "Y2 navigation buttons") &&
             strcmp(name, "mtk-pmic-keys") && strcmp(name, "mt6582-keypad"))) {
            close(fd);
            continue;
        }
        int clock = CLOCK_MONOTONIC;
        clocks[count] = ioctl(fd, EVIOCSCLOCKID, &clock) == 0;
        numbers[count] = n;
        fds[count++] = (struct pollfd){.fd = fd, .events = POLLIN};
        printf("{\"device\":\"%s\",\"event\":%d,\"monotonic\":%s}\n", name, n,
               clocks[count-1] ? "true" : "false");
    }
    if (!count) return 3;
    uint64_t start = mono_us(), stop = start + (uint64_t)seconds * 1000000;
    unsigned records = 0;
    int failure = 0;
    printf("{\"capture_start_us\":%" PRIu64 ",\"seconds\":%ld}\n", start, seconds);
    while (!stopped && mono_us() < stop && records < 8192 && !failure) {
        uint64_t remaining = stop - mono_us();
        if (remaining > (uint64_t)seconds * 1000000) break;
        int ready = poll(fds, (nfds_t)count, (int)((remaining + 999) / 1000));
        if (ready < 0) { if (errno == EINTR) continue; failure = 1; break; }
        for (int i = 0; i < count && records < 8192; ++i) {
            if (fds[i].revents & (POLLERR | POLLHUP | POLLNVAL)) { failure = 1; break; }
            if (!(fds[i].revents & POLLIN)) continue;
            struct input_event events[64];
            ssize_t n = read(fds[i].fd, events, sizeof(events));
            uint64_t received = mono_us();
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            if (n <= 0 || n % sizeof(events[0])) { failure = 1; break; }
            for (size_t j = 0; j < (size_t)n / sizeof(events[0]) && records < 8192; ++j) {
                struct input_event *e = &events[j];
                uint64_t stamp = (uint64_t)e->input_event_sec * 1000000 + e->input_event_usec;
                printf("{\"event\":%d,\"kernel_us\":%" PRIu64 ",\"received_us\":%" PRIu64
                       ",\"type\":%u,\"code\":%u,\"value\":%d}\n",
                       numbers[i], stamp, received, e->type, e->code, e->value);
                ++records;
            }
        }
    }
    for (int i = 0; i < count; ++i) close(fds[i].fd);
    printf("{\"capture_end_us\":%" PRIu64 ",\"records\":%u,\"failed\":%s,\"truncated\":%s}\n",
           mono_us(), records, failure ? "true" : "false", records >= 8192 ? "true" : "false");
    return failure ? 4 : records >= 8192 ? 5 : 0;
}
