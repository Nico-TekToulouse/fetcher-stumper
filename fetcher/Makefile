NAME    = fetcher
SRC     = src/main.c src/args.c src/history.c src/network.c \
          src/heartbeat.c src/signal_handler.c src/watchdog.c src/stop_token.c \
          ../common/protocol.c
OBJ     = $(SRC:.c=.o)
CC      = cc
CFLAGS  = -Wall -Wextra -I include -I ../common
LDFLAGS = -lpthread

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(OBJ) -o $(NAME) $(LDFLAGS)

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
