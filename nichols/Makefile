CC=g++

CFLAGS=-Wall -W -g


all: client server

client: client.c raw.c duckchat.h client.h utils.h
	$(CC) client.c raw.c duckchat.h client.h utils.h $(CFLAGS) -o client

server: server.c raw.c duckchat.h server.h utils.h topology.h channelList.h
	$(CC) server.c raw.c duckchat.h server.h utils.h topology.h channelList.h $(CFLAGS) -o server

clean:
	rm -f client server *.o

