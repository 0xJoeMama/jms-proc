CXX=g++
INCLUDE=include
CXXFLAGS=-ggdb -std=c++17 -pedantic -Wall -Werror -Wextra -I$(INCLUDE)

BINS=jms_coord

vpath %.cpp src/
vpath %.hpp include/

all: $(BINS)

clean:
	rm -rf $(BINS) *.o

