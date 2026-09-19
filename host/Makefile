# Desktop build of the fortune model. Nothing here ships to the board; the
# Arduino IDE ignores this folder.
#
#   make          build ./tinygpt
#   make test     build and run the self-test
#   make compare  check the engine against the Python reference (needs python3)
#   make clean

CXX      ?= g++
CXXFLAGS ?= -O2 -std=c++11 -Wall -Wextra -I..
PYTHON   ?= python3

tinygpt: main.cpp ../gpt.h ../model_weights.h
	$(CXX) $(CXXFLAGS) -o $@ main.cpp

.PHONY: test compare clean
test: tinygpt
	./tinygpt --selftest

compare: tinygpt
	$(PYTHON) compare.py

clean:
	rm -f tinygpt
