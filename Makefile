CXX      := g++
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -Isrc $(shell pkg-config --cflags opencv4 sdl2)
LDLIBS   := $(shell pkg-config --libs opencv4 sdl2 gl) -lpthread
SRC      := $(shell find src -name '*.cpp')
OBJ      := $(SRC:src/%.cpp=build/%.o)
DEP      := $(OBJ:.o=.d)

birds: $(OBJ)
	$(CXX) $(OBJ) -o $@ $(LDLIBS)

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

test: birds
	./birds --selftest

clean:
	rm -rf build birds

-include $(DEP)

.PHONY: test clean
