CFLAGS = -O2 -std=c99 -pedantic -Wall -Wextra -Werror

all: sensors2file

.PHONY: clean

clean:
	rm -f sensors2file *.o
