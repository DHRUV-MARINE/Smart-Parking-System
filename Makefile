# SmartParking - makefile for Linux / macOS (and MinGW on Windows)
# ---------------------------------------------------------------------------
CC      = gcc
CXX     = g++
CXXFLAGS = -std=c++17 -O2 -Wall -Wextra -Ibackend/third_party
TARGET  = smartparking

SOURCES = backend/main.cpp backend/parking.cpp backend/graph.cpp \
          backend/booking.cpp backend/database.cpp backend/http.cpp

all: $(TARGET)

sqlite3.o: backend/third_party/sqlite3.c
	$(CC) -O2 -c backend/third_party/sqlite3.c -o sqlite3.o

$(TARGET): $(SOURCES) sqlite3.o
	$(CXX) $(CXXFLAGS) $(SOURCES) sqlite3.o -o $(TARGET)

run: $(TARGET)
	./$(TARGET) --port 8080

demo: $(TARGET)
	./$(TARGET) --demo

clean:
	rm -f $(TARGET) sqlite3.o

.PHONY: all run demo clean
