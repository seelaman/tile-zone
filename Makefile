CXX      = g++
CXXFLAGS = -O2 -std=c++17 $(shell pkg-config --cflags Qt6Widgets)
LDFLAGS  = $(shell pkg-config --libs Qt6Widgets)

cursor-warp: cursor-warp.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

tile-zone-picker: tile-zone-picker.cpp
	$(CXX) $(CXXFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f cursor-warp tile-zone-picker

.PHONY: clean
