NAME = xhttpd

BIN_DIR = bin
SRC_DIR = src
INCLUDE = include

FILES += $(SRC_DIR)/http.cpp
FILES += $(SRC_DIR)/main.cpp
FILES += $(SRC_DIR)/mutex.cpp

all: dependency build

dependency:
	mkdir -p $(BIN_DIR)

build:
	g++ -o $(BIN_DIR)/$(NAME) $(FILES) -I$(INCLUDE) -lpthread -std=c++11

clean:
	rm $(BIN_DIR)/*
