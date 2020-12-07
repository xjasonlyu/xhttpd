BIN_DIR = bin
NAME = xhttpd

FILES += main.cpp
FILES += http_conn.cpp

BUILD = g++ -o $(BIN_DIR)/$(NAME) $(FILES) -lpthread -std=c++11

all:
	$(BUILD)

clean:
	rm $(BIN_DIR)/*
