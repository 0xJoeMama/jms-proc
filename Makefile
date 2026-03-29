CXX=g++
CC=gcc
INCLUDE=include
CXXFLAGS=-ggdb -std=c++17 -pedantic -Wall -Werror -Wextra -I$(INCLUDE) -MMD
CFLAGS=-ggdb -std=c11 -pedantic -Wall -Werror -Wextra -I$(INCLUDE)

BINS=jms_coord
TESTS=pool_tests fifo_tests store_tests poll_tests

vpath %.cpp src/
vpath %.cpp tests/
vpath %.hpp include/

all: $(BINS)

jms_coord: file_store.o pipe.o

pool_tests: pool.o pipe.o
store_tests: file_store.o pipe.o

test: $(TESTS)

clean:
	rm -rf $(BINS) *.o *.d $(TESTS)

.PHONY: clean all test

-include *.d
