CC ?= gcc
CXX ?= g++
CFLAGS ?= -std=c11 -D_POSIX_C_SOURCE=200809L -Wall -Wextra -Wno-unused-parameter -fPIC -O2
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wno-unused-parameter -fPIC -O2
LDFLAGS ?=

PKG_CFLAGS := $(shell pkg-config --cflags libpq sqlite3 openssl libmariadb 2>/dev/null)
PKG_LIBS   := $(shell pkg-config --libs libpq sqlite3 openssl libmariadb 2>/dev/null)

SRC := src/validation.c src/connector.c src/strings.c \
       src/sqlite_backend.c src/postgres_backend.c src/mysql_backend.c \
       src/filesystem_backend.c
OBJ := $(SRC:.c=.o)

LIB := libmultilang.so
LIBXX := libmultilangxx.so

.PHONY: all test clean

all: $(LIB) $(LIBXX)

$(LIB): $(OBJ)
	$(CC) -shared -o $@ $(OBJ) $(PKG_LIBS) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -Iinclude -c -o $@ $<

$(LIBXX): src/multilang.o $(LIB)
	$(CXX) -shared -o $@ src/multilang.o -L. -lmultilang $(LDFLAGS)

src/multilang.o: src/multilang.cpp
	$(CXX) $(CXXFLAGS) -Iinclude -c -o $@ $<

test: test/test_validation test/test_strings test/test_conformance test/test_cpp
	LD_LIBRARY_PATH=. ./test/test_validation
	LD_LIBRARY_PATH=. ./test/test_strings
	LD_LIBRARY_PATH=. ./test/test_conformance
	LD_LIBRARY_PATH=. ./test/test_cpp

test/test_validation: test/test_validation.c $(LIB)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -Iinclude -o $@ $< -L. -lmultilang

test/test_strings: test/test_strings.c $(LIB)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -Iinclude -o $@ $< -L. -lmultilang -lsqlite3

test/json_mini.o: test/json_mini.c test/json_mini.h
	$(CC) $(CFLAGS) -Iinclude -c -o $@ test/json_mini.c

test/test_conformance: test/test_conformance.c test/json_mini.o $(LIB)
	$(CC) $(CFLAGS) $(PKG_CFLAGS) -Iinclude -o $@ test/test_conformance.c test/json_mini.o -L. -lmultilang

test/test_cpp: test/test_cpp.cpp test/json_mini.o $(LIBXX)
	$(CXX) $(CXXFLAGS) -Iinclude -o $@ test/test_cpp.cpp test/json_mini.o -L. -lmultilangxx -lmultilang

clean:
	rm -f $(OBJ) src/multilang.o test/json_mini.o $(LIB) $(LIBXX) \
	      test/test_validation test/test_strings test/test_conformance test/test_cpp
