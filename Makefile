SRC=$(wildcard *.c)

tte: $(SRC)
	$(CC) $^ -o tte -Wall -Wextra -pedantic -std=c99
