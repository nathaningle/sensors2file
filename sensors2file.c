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


/*
 * Path to destination file.  It should end in '.prom', and node_exporter's
 * --collector.textfile.directory command-line argument should point to the
 *  containing directory.
 */
const char targetpath[] = "/var/node_exporter/hwsensors.prom";
#define PATHBUFLEN 1024

/* Seconds between reading sensors. */
#define INTERVAL 15


/* Round a number modulo another, e.g. roundmod(12, 5) == 10. */
time_t
roundmod(time_t x, int mod)
{
    return (x / mod) * mod;
}


/*
 * Calculate the difference between two timespec structs.  Fail if a > b.
 *
 * Return 0 on success or -1 on failure.
 */
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

    /* If we get here then a > b, so fail. */
    return -1;
}

/*
 * Walk the hardware sensor tree.  Read temperature sensors, write results to
 * FILE *fp.  See the HW_SENSORS section in sysctl(2).
 *
 * Return 0 on success or -1 on failure.
 */
int
walk_hwsensors(FILE *fp) {
    struct sensordev snsrdev;
    size_t sdlen = sizeof(snsrdev);
    struct sensor snsr;
    size_t slen = sizeof(snsr);

    int mib[5] = {CTL_HW, HW_SENSORS, 0, 0, 0};

    int i, j, k;

    /* Write type information. */
    fprintf(fp, "#TYPE node_hwmon_temp_celsius gauge\n");

    for (i = 0; ; i++) {
        /* Iterate over sensor devices. */
        mib[2] = i;

        /* Determine the name of the sensor device (chip). */
        if (sysctl(mib, 3, &snsrdev, &sdlen, NULL, 0) == -1) {
            if (errno == ENXIO)
                continue;
            if (errno == ENOENT)
                break;
            perror("sysctl(2) failed to provide sensor device data");
            return -1;
        }
        for (j = 0; j < SENSOR_MAX_TYPES; j++) {
            /* Iterate over sensor types. */
            mib[3] = j;

            for (k = 0; k < snsrdev.maxnumt[j]; k++) {
                /* Iterate over sensors. */
                mib[4] = k;

                /* Determine the name and value of the sensor. */
                if (sysctl(mib, 5, &snsr, &slen, NULL, 0) == -1) {
                    if (errno == ENXIO || errno == ENOENT)
                        continue;
                    perror("sysctl(2) failed to provide sensor data");
                    return -1;
                }

                if (snsr.type == SENSOR_TEMP && (snsr.flags & SENSOR_FINVALID) == 0) {
                    /* Magic numbers lifted from /usr/src/sbin/sysctl/sysctl.c. */
                    fprintf(fp, "node_hwmon_temp_celsius{chip=\"%s\", sensor=\"%s%d\"} %.2f\n",
                            snsrdev.xname, sensor_type_s[snsr.type], snsr.numt, (snsr.value - 273150000) / 1000000.0);
                }
            }
        }
    }

    return 0;
}


/*
 * Sleep until the start of the next interval, i.e. INTERVAL seconds after the
 * start of the current interval.
 *
 * Return 0 on success or -1 on failure.
 */
int
sleep_interval(const struct timespec *offset)
{
    struct timespec now, nexttime, sleep_for, sleep_rem;
    if (clock_gettime(CLOCK_REALTIME, &now) != 0) {
        perror("sleep_interval(): clock_gettime(CLOCK_REALTIME, ..) failed");
        return -1;
    }
    nexttime.tv_sec = roundmod(now.tv_sec + INTERVAL, INTERVAL) + offset->tv_sec;
    nexttime.tv_nsec = offset->tv_nsec;
    assert(timedelta(&sleep_for, &now, &nexttime) == 0);

    while(1) {
        if (nanosleep(&sleep_for, &sleep_rem) == 0)
            break;

        if (errno != EINTR) {
            perror("nanosleep(2)");
            return -1;
        }

        sleep_for.tv_sec = sleep_rem.tv_sec;
        sleep_for.tv_nsec = sleep_rem.tv_nsec;
    }

    return 0;
}


int
main(void)
{
    struct timespec offset;

    char tmppath[PATHBUFLEN];
    int tmpfd;
    FILE *tmpfile;

    /*
     * Calculate the offset from time modulo INTERVAL.  We use this to consistently
     * update INTERVAL seconds apart, regardless of the time we started.
     */
    if (clock_gettime(CLOCK_REALTIME, &offset) != 0) {
        perror("main(): clock_gettime(CLOCK_REALTIME, ..) failed");
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

        /* Walk the hardware sensor tree. */
        if (walk_hwsensors(tmpfile) != 0) {
            fclose(tmpfile);
            unlink(tmppath);
            exit(EXIT_FAILURE);
        }

        /* Close and rename the tempfile. */
        fclose(tmpfile);
        chmod(tmppath, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (rename(tmppath, targetpath) == -1) {
            perror("rename(2)");
            exit(EXIT_FAILURE);
        }

        /* Sleep until the start of the next interval. */
        if (sleep_interval(&offset) != 0)
            exit(EXIT_FAILURE);
    }

    /* We should never get here. */
    assert(0);
}
