CC := xcrun clang
CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -O2
FRAMEWORKS := -framework CoreFoundation

.PHONY: all clean run test

all: build/mac-capture-probe build/mac-touch-agent

build/mac-capture-probe: mac/Probe/main.c mac/Probe/MultitouchSupportABI.h
	@mkdir -p build
	$(CC) $(CFLAGS) mac/Probe/main.c -o $@ $(FRAMEWORKS)

build/mac-touch-agent: mac/Agent/main.c mac/Probe/MultitouchSupportABI.h protocol/TouchFrame.c protocol/TouchFrame.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Iprotocol -Imac/Probe mac/Agent/main.c protocol/TouchFrame.c -o $@ $(FRAMEWORKS) -lpthread

build/protocol-test: tests/protocol_test.c protocol/TouchFrame.c protocol/TouchFrame.h
	@mkdir -p build
	$(CC) $(CFLAGS) -Iprotocol tests/protocol_test.c protocol/TouchFrame.c -o $@

run: build/mac-capture-probe
	./build/mac-capture-probe

test: build/protocol-test
	./build/protocol-test

clean:
	rm -f build/mac-capture-probe build/mac-touch-agent build/protocol-test
