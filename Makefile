CC      := gcc
CFLAGS  ?= -std=c11 -O3 -Wall -Wextra -Werror -pedantic -Iinclude -pthread -D_POSIX_C_SOURCE=200809L
LDFLAGS ?= -pthread

SRC     := $(wildcard src/*.c)
OBJ     := $(SRC:src/%.c=obj/%.o)
BIN     := bin/task_engine

TEST_SRC := $(wildcard tests/*.c)
# Filter out stress_test.sh or any non-C files just in case
TEST_SRC := $(filter %.c, $(TEST_SRC))
TEST_OBJ := $(TEST_SRC:tests/%.c=obj/%.o)
TEST_BIN := bin/test_suite

.PHONY: all debug asan tsan test valgrind clean

all: $(BIN)

debug: clean
	$(MAKE) CFLAGS="-std=c11 -g3 -O0 -DDEBUG -Wall -Wextra -Werror -pedantic -Iinclude -pthread -D_POSIX_C_SOURCE=200809L" all $(if $(TEST_SRC),test)

asan: clean
	$(MAKE) CFLAGS="-std=c11 -fsanitize=address,undefined -g -Wall -Wextra -Werror -pedantic -Iinclude -pthread -D_POSIX_C_SOURCE=200809L" LDFLAGS="-pthread -fsanitize=address,undefined" all $(if $(TEST_SRC),test)

tsan: clean
	$(MAKE) CFLAGS="-std=c11 -fsanitize=thread -g -Wall -Wextra -Werror -pedantic -Iinclude -pthread -D_POSIX_C_SOURCE=200809L" LDFLAGS="-pthread -fsanitize=thread" all $(if $(TEST_SRC),test)

test: $(if $(TEST_SRC),$(TEST_BIN))
	@if [ -f "$(TEST_BIN)" ]; then ./$(TEST_BIN); fi

valgrind: $(if $(TEST_SRC),$(TEST_BIN))
	@if [ -f "$(TEST_BIN)" ]; then valgrind --leak-check=full --show-leak-kinds=all --track-origins=yes --error-exitcode=1 ./$(TEST_BIN); fi

$(BIN): $(OBJ) | bin
	$(CC) $(OBJ) -o $@ $(LDFLAGS)

$(TEST_BIN): $(filter-out obj/main.o, $(OBJ)) $(TEST_OBJ) | bin
	$(CC) $(filter-out obj/main.o, $(OBJ)) $(TEST_OBJ) -o $@ $(LDFLAGS)

obj/%.o: src/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

obj/%.o: tests/%.c | obj
	$(CC) $(CFLAGS) -c $< -o $@

bin:
	mkdir -p bin

obj:
	mkdir -p obj

clean:
	rm -rf bin obj loom.log

dist: all
	strip $(BIN)
	tar -czvf task_engine-v1.0.0-linux-amd64.tar.gz $(BIN) web/dashboard.html README.md task_engine.service LICENSE
	sha256sum task_engine-v1.0.0-linux-amd64.tar.gz > task_engine-v1.0.0-linux-amd64.tar.gz.sha256
