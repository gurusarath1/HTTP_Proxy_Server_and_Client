all:
	gcc proxy.c -o proxy
	gcc client.c -o client
clean:
	rm proxy
	rm client