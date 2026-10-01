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
HARDWARE_ONLY := $(READER)/epd.cpp $(READER)/touch.cpp $(READER)/store.cpp $(READER)/storage_sd.cpp
READER_SOURCES := $(filter-out $(HARDWARE_ONLY),$(wildcard $(READER)/*.cpp))

# C files in reader/ (the JPEG decoder) are compiled as C, into obj/.
READER_C_OBJECTS := $(patsubst $(READER)/%.c,obj/%.o,$(wildcard $(READER)/*.c))

SOURCES := main.cpp host_platform.cpp $(READER_SOURCES) $(GFX)/Adafruit_GFX.cpp
HEADERS := $(wildcard *.h) $(wildcard shim/*.h) $(wildcard $(READER)/*.h) $(wildcard $(READER)/*/*.h)

obj/%.o: $(READER)/%.c $(HEADERS)
	@mkdir -p obj
	$(CC) -O2 -I$(READER) -c $< -o $@

reader-sim: $(SOURCES) $(READER_C_OBJECTS) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(SOURCES) $(READER_C_OBJECTS) -o $@ $(LDLIBS)

run: reader-sim
	./reader-sim

# Host tests for the pure layout and hit-testing arithmetic. No SDL, no
# main.cpp, so tests.cpp stands in for the screen driver and Serial.
TESTFLAGS := -std=c++17 -O1 -DARDUINO=100 -Ishim -I$(READER) -I$(GFX)

reader-tests: tests.cpp test_epub.cpp host_platform.cpp $(READER_SOURCES) $(READER_C_OBJECTS) $(GFX)/Adafruit_GFX.cpp $(HEADERS)
	$(CXX) $(TESTFLAGS) tests.cpp test_epub.cpp host_platform.cpp $(READER_SOURCES) $(READER_C_OBJECTS) $(GFX)/Adafruit_GFX.cpp -o $@

# Prints what the reader makes of an EPUB: ./epub-dump book.epub [--text]
EPUB_SOURCES := $(READER)/zip.cpp $(READER)/xml.cpp $(READER)/ascii.cpp $(READER)/epub.cpp $(READER)/convert.cpp

epub-dump: epubdump.cpp host_platform.cpp $(EPUB_SOURCES) $(READER_C_OBJECTS) $(HEADERS)
	$(CXX) $(TESTFLAGS) epubdump.cpp host_platform.cpp $(EPUB_SOURCES) $(READER_C_OBJECTS) -o $@

test: reader-tests
	./reader-tests

clean:
	rm -rf reader-sim reader-tests epub-dump obj

.PHONY: run test clean
