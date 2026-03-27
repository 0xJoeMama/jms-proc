CXX=g++
CC=gcc
INCLUDE=include
CXXFLAGS=-ggdb -std=c++17 -pedantic -Wall -Werror -Wextra -I$(INCLUDE)
CFLAGS=-ggdb -std=c11 -pedantic -Wall -Werror -Wextra -I$(INCLUDE)

BINS=jms_coord
TESTS=pool_tests

vpath %.cpp src/
vpath %.cpp tests/
vpath %.hpp include/

all: $(BINS)

pool_tests: pool.o pipe.o

test: $(TESTS)

clean:
	rm -rf $(BINS) *.o

.PHONY: clean all test
