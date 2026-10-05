all:
	$(MAKE) -C fetcher
	$(MAKE) -C server

clean:
	$(MAKE) -C fetcher clean
	$(MAKE) -C server clean

fclean:
	$(MAKE) -C fetcher fclean
	$(MAKE) -C server fclean

re: fclean all

.PHONY: all clean fclean re
