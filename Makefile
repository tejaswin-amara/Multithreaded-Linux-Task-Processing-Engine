CC      := gcc
CFLAGS  := -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Iinclude -pthread
LDFLAGS := -pthread
SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=build/%.o)
BIN     := loom

.PHONY: all run tsan clean

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

build/%.o: src/%.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

# Rebuilds with ThreadSanitizer to catch data races / lock-order issues.
# See docs/03-test-plan.md for how this is used to verify thread-safety.
tsan: clean
	$(MAKE) CFLAGS="$(CFLAGS) -fsanitize=thread -g -O1" LDFLAGS="$(LDFLAGS) -fsanitize=thread" $(BIN)

run: $(BIN)
	./$(BIN)

clean:
	rm -rf build $(BIN) loom.log
