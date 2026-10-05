NAME    = server
SRC     = src/main.c src/network.c src/client_registry.c \
          src/heartbeat_monitor.c src/command_buffer.c src/ncurses_ui.c \
          src/ncurses_input.c src/parser.c src/whitelist.c \
          ../common/protocol.c
OBJ     = $(SRC:.c=.o)
CC      = cc
CFLAGS  = -Wall -Wextra -I include -I ../common $(shell pkg-config --cflags ncurses)
LDFLAGS = -lpthread $(shell pkg-config --libs ncurses)

all: $(NAME)

$(NAME): $(OBJ)
	$(CC) $(OBJ) -o $(NAME) $(LDFLAGS)

clean:
	rm -f $(OBJ)

fclean: clean
	rm -f $(NAME)

re: fclean all

.PHONY: all clean fclean re
