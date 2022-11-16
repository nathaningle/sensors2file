# sensors2file

A small OpenBSD program that uses [sysctl(2)] to walk the available hw.sensors, then writes the
results to a file in Prometheus' [text-based format] so that node_exporter can pick it up.


## Installation

This is probably useless without node_exporter, so install that first.  Failure to do so will make
our installation fail, since we use the `_nodeexporter` group created by that package.

```
$ doas pkg_add node_exporter
quirks-6.42 signed on 2022-10-30T18:56:25Z
node_exporter-1.3.1: ok
The following new rcscripts were installed: /etc/rc.d/node_exporter
See rcctl(8) for details.
```

Then our install:

```
$ make
cc -O2 -std=c99 -pedantic -Wall -Wextra -Werror   -o sensors2file sensors2file.c
$ doas make install
install -m 0755 -o root -g bin sensors2file /usr/local/bin/
install -m 0755 -o root -g wheel rc.d/sensors2file /etc/rc.d/
install -m 0775 -o root -g _nodeexporter -d /var/node_exporter/
```

Start the services:

```
$ doas rcctl enable sensors2file
$ doas rcctl start sensors2file
sensors2file(ok)
$ doas rcctl enable node_exporter
$ doas rcctl set node_exporter flags '--web.disable-exporter-metrics --collector.textfile.directory="/var/node_exporter"'
$ doas rcctl start node_exporter
node_exporter(ok)
```

Test:

```
$ ls -l /var/node_exporter/hwsensors.prom
-rw-r--r--  1 _nodeexporter  _nodeexporter  405 Nov 16 22:45 /var/node_exporter/hwsensors.prom
$ curl -sS http://localhost:9100/metrics | grep node_hwmon_temp_celsius
# HELP node_hwmon_temp_celsius Metric read from /var/node_exporter/hwsensors.prom
# TYPE node_hwmon_temp_celsius gauge
node_hwmon_temp_celsius{chip="acpitz0",sensor="temp0"} 75
node_hwmon_temp_celsius{chip="acpitz1",sensor="temp0"} 70
node_hwmon_temp_celsius{chip="acpitz2",sensor="temp0"} 52
node_hwmon_temp_celsius{chip="acpitz3",sensor="temp0"} 24.4
node_hwmon_temp_celsius{chip="acpitz4",sensor="temp0"} 80
node_hwmon_temp_celsius{chip="cpu0",sensor="temp0"} 70
```


[sysctl(2)]: https://man.openbsd.org/sysctl.2
[text-based format]: https://prometheus.io/docs/instrumenting/exposition_formats/#text-based-format
