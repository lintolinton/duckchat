CC=g++
#CFLAGS=-Wall -W -g -Werror 
# Uncomment the line below if you're running this on macos
#CFLAGS= -g  -DGRAD=1 -std=c++0x -stdlib=libc++

#Comment this line if you're running on macos
CFLAGS= -g  -DGRAD=1

#Comment this line if you're on macos
LOADLIBES = -lnsl

all: client server

client: client.c raw.c
	$(CC) client.c raw.c $(LOADLIBES) $(CFLAGS) -o client

server: server.c 
	$(CC) server.c $(LOADLIBES) $(CFLAGS) -o server

clean:
	rm -f client server *.o
