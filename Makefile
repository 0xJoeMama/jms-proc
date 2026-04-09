INCLUDE=include
CXXFLAGS=-Wall -Werror -Wextra -pedantic -std=c++17 -Iinclude -MMD -MP -g3 $(EXTRAFLAGS)
LDLIBS += -lstdc++

BINS=jms_coord jms_console
TESTS=pool_tests fifo_tests store_tests poll_tests fd_tests command_tests

vpath %.cpp src/
vpath %.cpp tests/
vpath %.hpp include/

all: $(BINS)

jms_coord: jms_coord.o file_store.o signals.o command.o
jms_console: jms_console.o file_store.o signals.o

pool_tests: pool_tests.o pool.o
store_tests: store_tests.o file_store.o
fifo_tests: fifo_tests.o
poll_tests: poll_tests.o
fd_tests: fd_tests.o

test: $(TESTS)

clean:
	rm -rf $(BINS) *.o *.d $(TESTS)

.PHONY: clean all test

-include *.d
