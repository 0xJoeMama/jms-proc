CXX=g++
INCLUDE=include
CXXFLAGS=-O0 -ggdb -std=c++17 -pedantic -Wall -Werror -Wextra -I$(INCLUDE)

vpath %.cpp src/
vpath %.hpp include/

all: jms_coord
