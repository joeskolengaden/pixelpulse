# Makefile for the FPP "pixelpulse" plugin (live audio-reactive lighting).
#
#   make / make clean   build / remove the plugin shared library
# Override FPPDIR if FPP is not at /opt/fpp:  make FPPDIR=/path/to/fpp

PLUGIN  := pixelpulse
FPPDIR  ?= /opt/fpp
SRCDIR  ?= $(FPPDIR)/src

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
  SHLIB_EXT   := .dylib
  SHLIB_FLAGS := -dynamiclib -undefined dynamic_lookup
  CXX         ?= clang++
  CC          ?= clang
  AUDIO_LIBS  :=
else
  SHLIB_EXT   := .so
  SHLIB_FLAGS := -shared
  CXX         ?= g++
  CC          ?= gcc
  AUDIO_LIBS  := -lasound
endif

TARGET  := lib$(PLUGIN)$(SHLIB_EXT)
CXXOBJS := src/AudioFxPlugin.o src/AudioAnalyzer.o src/AlsaCapture.o
COBJS   := src/kissfft/kiss_fft.o src/kissfft/kiss_fftr.o

CXXFLAGS += -std=gnu++2a -fPIC -O2 -Wall -fvisibility=default -I$(SRCDIR)
CXXFLAGS += $(shell pkg-config --cflags jsoncpp 2>/dev/null)
CFLAGS   += -fPIC -O2

.PHONY: all clean
all: $(TARGET)

$(TARGET): $(CXXOBJS) $(COBJS)
	$(CXX) $(SHLIB_FLAGS) -o $@ $(CXXOBJS) $(COBJS) $(AUDIO_LIBS) -lpthread

src/%.o: src/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

src/kissfft/%.o: src/kissfft/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(CXXOBJS) $(COBJS) $(TARGET)
