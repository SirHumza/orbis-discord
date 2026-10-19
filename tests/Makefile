CC ?= cc
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror
SRC := ../src/core
TESTS := test_main.c test_ws_streaming.c test_json_b64.c
TARGET := run_tests

.PHONY: test clean

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(TESTS) $(SRC)/b64.c $(SRC)/jsonlite.c $(SRC)/ws.h $(SRC)/ws_env.h
	$(CC) $(CFLAGS) -I$(SRC) -o $@ $(TESTS) $(SRC)/b64.c $(SRC)/jsonlite.c

clean:
	rm -f $(TARGET)
