CC = gcc
CFLAGS = -Wall -Iinclude

SRC = src/main.c \
      src/chat/chat.c \
      src/network/network.c \
      src/network/discovery.c \
      src/tui/tui.c

OBJ = $(SRC:.c=.o)
TARGET = termchan

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ -lncurses

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJ) $(TARGET)
