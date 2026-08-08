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

# Packaging-only: PREFIX/DESTDIR/LIBDIR/INCLUDEDIR/SOVERSION are unused by
# `all`/`test` above (those keep building the plain unversioned .so they
# always have) -- only `make install` reads these, so the existing test
# flow this repo's CI already depends on (c-unit, conformance) is
# untouched.
PREFIX ?= /usr
DESTDIR ?=
LIBDIR ?= $(PREFIX)/lib
INCLUDEDIR ?= $(PREFIX)/include
SOVERSION := 0

.PHONY: all test clean install install-runtime install-devel

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

# Split runtime (.so + SONAME symlink) vs devel (headers + unversioned
# .so symlink for linking) so a package build can ship two separate
# packages from one `make install` -- see ../packaging/README.md.
install: install-runtime install-devel

install-runtime: $(OBJ) src/multilang.o
	install -d $(DESTDIR)$(LIBDIR)
	$(CC) -shared -Wl,-soname,libmultilang.so.$(SOVERSION) \
	    -o $(DESTDIR)$(LIBDIR)/libmultilang.so.$(SOVERSION).0.0 \
	    $(OBJ) $(PKG_LIBS) $(LDFLAGS)
	ln -sf libmultilang.so.$(SOVERSION).0.0 $(DESTDIR)$(LIBDIR)/libmultilang.so.$(SOVERSION)
	$(CXX) -shared -Wl,-soname,libmultilangxx.so.$(SOVERSION) \
	    -o $(DESTDIR)$(LIBDIR)/libmultilangxx.so.$(SOVERSION).0.0 \
	    src/multilang.o $(DESTDIR)$(LIBDIR)/libmultilang.so.$(SOVERSION).0.0 $(LDFLAGS)
	ln -sf libmultilangxx.so.$(SOVERSION).0.0 $(DESTDIR)$(LIBDIR)/libmultilangxx.so.$(SOVERSION)

install-devel: install-runtime
	install -d $(DESTDIR)$(INCLUDEDIR)
	install -m 644 include/multilang.h include/multilang.hpp $(DESTDIR)$(INCLUDEDIR)/
	ln -sf libmultilang.so.$(SOVERSION) $(DESTDIR)$(LIBDIR)/libmultilang.so
	ln -sf libmultilangxx.so.$(SOVERSION) $(DESTDIR)$(LIBDIR)/libmultilangxx.so
