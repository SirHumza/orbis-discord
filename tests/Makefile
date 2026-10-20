CC ?= cc
BASE_CFLAGS := -std=c11 -D_POSIX_C_SOURCE=200809L -D_DEFAULT_SOURCE -Wall -Wextra -Werror
SAN_FLAGS := -fsanitize=address,undefined -fno-omit-frame-pointer -g
CFLAGS += $(BASE_CFLAGS)
SRC := ../src/core
MODEL := ../src/model
MBEDTLS := ../third_party/mbedtls
TESTS := test_main.c fixtures.c test_ws_streaming.c test_json_b64.c test_store.c test_msgjson.c test_tenor.c test_tone.c test_cdnurl.c test_gateway.c test_rest.c test_remoteauth.c test_gateway_loop.c test_ui.c test_image.c test_nav.c test_msgview.c test_ime_utf8.c test_perm.c test_voice.c test_multipart.c
APP_SRCS := $(SRC)/b64.c $(SRC)/jsonlite.c $(SRC)/gateway.c $(SRC)/events.c \
            $(SRC)/compat.c \
            $(SRC)/http.c $(SRC)/rest.c $(SRC)/remoteauth.c ../src/image.c \
            ../src/platform/ime_utf8.c ../src/ui/composer.c \
            $(MODEL)/store.c $(MODEL)/snowflake.c $(MODEL)/msgjson.c $(MODEL)/cdnurl.c \
            $(MODEL)/tenor.c $(MODEL)/tone.c $(MODEL)/perm.c $(MODEL)/voice.c $(MODEL)/multipart.c
MBEDTLS_SRCS := $(filter-out $(MBEDTLS)/library/net_sockets.c $(MBEDTLS)/library/timing.c $(MBEDTLS)/library/entropy_poll.c $(wildcard $(MBEDTLS)/library/ssl_*.c),$(wildcard $(MBEDTLS)/library/*.c))
TARGET := run_tests

.PHONY: test clean asan

asan:
	$(MAKE) clean
	$(MAKE) test CFLAGS="$(BASE_CFLAGS) $(SAN_FLAGS)"

test: $(TARGET)
	./$(TARGET)

$(TARGET): $(TESTS) $(APP_SRCS) $(MBEDTLS_SRCS)
	$(CC) $(CFLAGS) -I$(SRC) -I$(MODEL) -I$(MBEDTLS)/include -o $@ \
		$(TESTS) $(APP_SRCS) $(MBEDTLS_SRCS) -lm

clean:
	rm -f $(TARGET)
