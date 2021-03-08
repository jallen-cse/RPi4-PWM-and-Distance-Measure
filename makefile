make: assignment2.c
	gcc assignment2.c -o assignment2 -lpthread -lgpiod -lrt -Wall

example: gpio_char.c
	gcc gpio_char.c -lgpiod -o example

test: test.c
	gcc test.c -lgpiod -o test
