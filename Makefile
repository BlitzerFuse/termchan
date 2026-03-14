CC=gcc
CFLAGS=-Wall -Wextra

SRC=main.c network.c chat.c
OBJ=$(SRC:.c=.o)

termchat: $(OBJ)
	$(CC) $(CFLAGS) -o termchat $(OBJ)

clean:
	rm -f *.o termchat
