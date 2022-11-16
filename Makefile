CFLAGS = -O2 -std=c99 -pedantic -Wall -Wextra -Werror

all: sensors2file

.PHONY: clean install

clean:
	rm -f sensors2file *.o

install: sensors2file rc.d/sensors2file
	install -m 0755 -o root -g bin sensors2file /usr/local/bin/
	install -m 0755 -o root -g wheel rc.d/sensors2file /etc/rc.d/
	install -m 0775 -o root -g _nodeexporter -d /var/node_exporter/
