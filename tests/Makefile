CC ?= cc
CFLAGS += -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror
SRC := ../src/core
TESTS := test_main.c test_ws_streaming.c test_json_b64.c test_store.c test_gateway.c test_rest.c
TARGET := run_tests

.PHONY: test clean

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(TESTS) $(SRC)/b64.c $(SRC)/jsonlite.c ../src/model/store.c ../src/model/snowflake.c ../src/core/gateway.c ../src/core/events.c ../src/core/http.c ../src/core/rest.c $(SRC)/ws.h $(SRC)/ws_env.h
	$(CC) $(CFLAGS) -I$(SRC) -I../src/model -o $@ $(TESTS) $(SRC)/b64.c $(SRC)/jsonlite.c ../src/model/store.c ../src/model/snowflake.c ../src/core/gateway.c ../src/core/events.c ../src/core/http.c ../src/core/rest.c

clean:
	rm -f $(TARGET)
