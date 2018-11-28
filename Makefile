all: http_proxy

http_proxy: http_proxy.c
	gcc -pthread -o http_proxy http_proxy.c

clean:
	rm http_proxy

