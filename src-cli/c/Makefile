# trakcrate-helper and the C library of the trakcrate format (no Qt, no C++). Output goes to target/.
#
#   make            build target/trakcrate-helper
#   make test       the snip unit tests
#   make test-asan  the same with AddressSanitizer and UBSan
#   make install DESTDIR=/some/root     install the helper in $(DESTDIR)/usr/lib/trakcrate
#   make clean

CC      ?= gcc
CFLAGS  += -Wall -Werror -g -O2 $(shell pkg-config --cflags libavformat libavcodec libavutil libswresample libswscale)
LDLIBS  += $(shell pkg-config --libs libavformat libavcodec libavutil libswresample libswscale) -lcdj -lm
version := $(shell sed -n 's/^VERSION=//p' ../../version | head -n 1)
CPPFLAGS += -DTC_VERSION='"$(version)"'

LIB_SRC    = $(wildcard lib/*.c)
HELPER_SRC = $(wildcard helper/*.c)
OBJS       = $(patsubst %.c,target/%.o,$(LIB_SRC) $(HELPER_SRC))
LIBEXEC    = /usr/lib/trakcrate

SNIP_TESTS = test/dispatch_test.c.snip test/natsort_test.c.snip test/kv_test.c.snip test/fsutil_test.c.snip \
             test/sha256_test.c.snip test/format_test.c.snip test/analysis_test.c.snip test/rbformat_test.c.snip

all: target/trakcrate-helper

target/%.o: %.c $(wildcard lib/*.h helper/*.h)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(CPPFLAGS) -c $< -o $@

# the support files compiled in by lib/defaultfiles.c
target/lib/defaultfiles.o: CPPFLAGS += -DTC_USER_SETTINGS_DIR='"$(CURDIR)/data/user-settings"'
target/lib/defaultfiles.o: $(wildcard data/user-settings/*)

# the round trip checks on real rekordbox files (test/test_rekordbox_roundtrip.sh), not installed
target/rb_roundtrip: test/rb_roundtrip.c $(filter-out target/helper/%,$(OBJS))
	$(CC) $(CFLAGS) -o $@ test/rb_roundtrip.c $(filter-out target/helper/%,$(OBJS)) $(LDFLAGS) $(LDLIBS)

# the database editor driven by a plan file (test/test_append_oracle.sh), not installed
target/append_plan: test/append_plan.c $(filter-out target/helper/%,$(OBJS))
	$(CC) $(CFLAGS) -o $@ test/append_plan.c $(filter-out target/helper/%,$(OBJS)) $(LDFLAGS) $(LDLIBS)

target/trakcrate-helper: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS) $(LDLIBS)

test:
	@for t in $(SNIP_TESTS); do sniprun $$t || exit 1; done

# snip tests again, compiled with the sanitizers (any report fails)
test-asan:
	@for t in $(SNIP_TESTS); do \
	  b=$${t%.c.snip}; \
	  ( cd test && \
	    awk '/^.*$$/ {print $$0} /^\/\/SNIP_FILE.*/ {system("snipcat " $$2 " " $$3)}' $$(basename $$t) > $$(basename $$b).c && \
	    $(CC) -Wall -Werror -Wno-unused-function -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=all $$(basename $$b).c -o $$(basename $$b) -lm && \
	    ./$$(basename $$b); rc=$$?; rm -f $$(basename $$b) $$(basename $$b).c; exit $$rc ) || exit 1; \
	done

install: target/trakcrate-helper
	install -d $(DESTDIR)$(LIBEXEC)
	install -m 755 target/trakcrate-helper $(DESTDIR)$(LIBEXEC)/trakcrate-helper

clean:
	rm -rf target


.PHONY: all test test-asan install clean
