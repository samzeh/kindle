# Builds the desktop simulator from the same reader code the ESP32 runs.
#   make run    build and start it
# Requires SDL2 (brew install sdl2) and the Adafruit GFX Arduino library.

GFX ?= $(HOME)/Documents/Arduino/libraries/Adafruit_GFX_Library
READER := ../reader

# ARDUINO=100 makes Adafruit GFX include the Arduino.h stand-in in shim/.
CXXFLAGS := -std=c++17 -O2 -DARDUINO=100 -Ishim -I$(READER) -I$(GFX) $(shell sdl2-config --cflags)
LDLIBS := $(shell sdl2-config --libs)

# Every .cpp in reader/ is included automatically, like the Arduino IDE does,
# except the hardware drivers: main.cpp stands in for those. A new reader file
# that talks to hardware (e.g. an SD card driver) belongs on this list too,
# with a stand-in added to main.cpp.
HARDWARE_ONLY := $(READER)/epd.cpp $(READER)/touch.cpp
READER_SOURCES := $(filter-out $(HARDWARE_ONLY),$(wildcard $(READER)/*.cpp))

SOURCES := main.cpp $(READER_SOURCES) $(GFX)/Adafruit_GFX.cpp
HEADERS := $(wildcard shim/*.h) $(wildcard $(READER)/*.h)

reader-sim: $(SOURCES) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $@ $(LDLIBS)

run: reader-sim
	./reader-sim

clean:
	rm -f reader-sim

.PHONY: run clean
