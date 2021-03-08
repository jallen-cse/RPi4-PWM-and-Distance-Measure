make: source.c
	gcc source.c -o main -lpthread -lgpiod -lrt -Wall
