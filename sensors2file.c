#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/sysctl.h>
#include <sys/sensors.h>

/* This is specific to your hardware; use `ktrace hw.sensors` to determine values. */
#define NUM_MIBS 6
const int mibs[NUM_MIBS][5] = {
    {CTL_HW, HW_SENSORS, 0, 0, 0},
    {CTL_HW, HW_SENSORS, 6, 0, 0},
    {CTL_HW, HW_SENSORS, 7, 0, 0},
    {CTL_HW, HW_SENSORS, 8, 0, 0},
    {CTL_HW, HW_SENSORS, 9, 0, 0},
    {CTL_HW, HW_SENSORS, 10, 0, 0}
};

int
main(void)
{
    struct sensordev snsrdev;
    size_t sdlen = sizeof(snsrdev);
    struct sensor snsr;
    size_t slen = sizeof(snsr);

    for (int i = 0; i < NUM_MIBS; i++) {
        /* Determine the name of the sensor device (chip). */
        if (sysctl(mibs[i], 3, &snsrdev, &sdlen, NULL, 0) == -1) {
            (void)fprintf(stderr, "sysctl(2) failed to provide sensor device data\n");
            exit(EXIT_FAILURE);
        }
        /* Determine the name of the sensor. */
        if (sysctl(mibs[i], 5, &snsr, &slen, NULL, 0) == -1) {
            (void)fprintf(stderr, "sysctl(2) failed to provide sensor data\n");
            exit(EXIT_FAILURE);
        }

        /* Magic numbers lifted from /usr/src/sbin/sysctl/sysctl.c. */
        printf("node_hwmon_temp_celsius{chip=\"%s\", sensor=\"%s%d\"} %.2f\n",
                snsrdev.xname, sensor_type_s[snsr.type], snsr.numt, (snsr.value - 273150000) / 1000000.0);
    }

    printf("ok\n");
    exit(EXIT_SUCCESS);
}
