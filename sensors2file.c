#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>
#include <assert.h>

const char targetpath[] = "/var/node_exporter/hwsensors.prom";
#define PATHBUFLEN 1024

/* Seconds between reading sensors. */
#define INTERVAL 15

/* This is specific to your hardware; use `ktrace sysctl hw.sensors; kdump` to determine values. */
#define NUM_MIBS 6
const int mibs[NUM_MIBS][5] = {
    {CTL_HW, HW_SENSORS, 0, 0, 0},
    {CTL_HW, HW_SENSORS, 6, 0, 0},
    {CTL_HW, HW_SENSORS, 7, 0, 0},
    {CTL_HW, HW_SENSORS, 8, 0, 0},
    {CTL_HW, HW_SENSORS, 9, 0, 0},
    {CTL_HW, HW_SENSORS, 10, 0, 0}
};

/* Round a number modulo another, e.g. roundmod(12, 5) == 10. */
time_t
roundmod(time_t x, int mod)
{
    return (x / mod) * mod;
}

/* Calculate the difference between two timespec structs.  Fail if a > b. */
int
timedelta(struct timespec *result, const struct timespec *a, const struct timespec *b)
{
    if (a->tv_sec <= b->tv_sec) {
        if (a->tv_nsec <= b->tv_nsec) {
            result->tv_sec = b->tv_sec - a->tv_sec;
            result->tv_nsec = b->tv_nsec - a->tv_nsec;
            return 0;
        }
        if (a->tv_sec < b->tv_sec) {
            /* Carry from tv_sec to tv_nsec. */
            result->tv_sec = b->tv_sec - a->tv_sec - 1;
            result->tv_nsec = 1000000000L + b->tv_nsec - a->tv_nsec;
            return 0;
        }
    }

    /* If we get here then a > bi, so fail. */
    return -1;
}

int
main(void)
{
    struct sensordev snsrdev;
    size_t sdlen = sizeof(snsrdev);
    struct sensor snsr;
    size_t slen = sizeof(snsr);

    struct timespec offset, now, nexttime, sleep_for, sleep_rem;
    int sleep_res;

    char tmppath[PATHBUFLEN];
    int tmpfd;
    FILE *tmpfile;

    /*
     * Calculate the offset from time modulo INTERVAL.  We use this to consistently
     * update INTERVAL seconds apart, regardless of the time we started.
     */
    if (clock_gettime(CLOCK_REALTIME, &offset) != 0) {
        perror("clock_gettime(CLOCK_REALTIME, ..) failed (pre-loop)");
        exit(EXIT_FAILURE);
    }
    offset.tv_sec %= INTERVAL;

    while(1) {
        /* Open the tempfile. */
        strlcpy(tmppath, targetpath, PATHBUFLEN);
        strlcat(tmppath, ".XXXXXXXX", PATHBUFLEN);
        tmpfd = mkstemp(tmppath);
        if (tmpfd == -1) {
            perror(tmppath);
            exit(EXIT_FAILURE);
        }
        tmpfile = fdopen(tmpfd, "w");
        if (!tmpfile) {
            perror(tmppath);
            close(tmpfd);
            unlink(tmppath);
            exit(EXIT_FAILURE);
        }

        /* Write type information. */
        fprintf(tmpfile, "#TYPE node_hwmon_temp_celsius gauge\n");

        for (int i = 0; i < NUM_MIBS; i++) {
            /* Determine the name of the sensor device (chip). */
            if (sysctl(mibs[i], 3, &snsrdev, &sdlen, NULL, 0) == -1) {
                perror("sysctl(2) failed to provide sensor device data");
                fclose(tmpfile);
                unlink(tmppath);
                exit(EXIT_FAILURE);
            }
            /* Determine the name and value of the sensor. */
            if (sysctl(mibs[i], 5, &snsr, &slen, NULL, 0) == -1) {
                perror("sysctl(2) failed to provide sensor data");
                fclose(tmpfile);
                unlink(tmppath);
                exit(EXIT_FAILURE);
            }

            /* Magic numbers lifted from /usr/src/sbin/sysctl/sysctl.c. */
            fprintf(tmpfile, "node_hwmon_temp_celsius{chip=\"%s\", sensor=\"%s%d\"} %.2f\n",
                    snsrdev.xname, sensor_type_s[snsr.type], snsr.numt, (snsr.value - 273150000) / 1000000.0);
        }

        /* Close and rename the tempfile. */
        fclose(tmpfile);
        chmod(tmppath, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (rename(tmppath, targetpath) == -1) {
            perror("rename(2)");
            exit(EXIT_FAILURE);
        }

        /* Sleep until the start of the next interval. */
        if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
            perror("clock_gettime(CLOCK_REALTIME, ..) failed (in loop)");
            exit(EXIT_FAILURE);
        }
        nexttime.tv_sec = roundmod(now.tv_sec + INTERVAL, INTERVAL) + offset.tv_sec;
        nexttime.tv_nsec = offset.tv_nsec;
        assert(timedelta(&sleep_for, &now, &nexttime) == 0);

        while(1) {
            sleep_res = nanosleep(&sleep_for, &sleep_rem);

            if (sleep_res == 0)
                break;

            if (errno != EINTR) {
                perror("nanosleep(2)");
                exit(EXIT_FAILURE);
            }

            sleep_for.tv_sec = sleep_rem.tv_sec;
            sleep_for.tv_nsec = sleep_rem.tv_nsec;
        }
    }

    printf("ok\n");
    exit(EXIT_SUCCESS);
}
