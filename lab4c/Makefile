.SILENT:

default:
	gcc -o lab4c_tcp lab4c.c -g -lmraa -lm -Wall -Wextra -lpthread -L/usr/lib -lssl -lcrypto 
	gcc -o lab4c_tls lab4c.c -g -lmraa -lm -Wall -Wextra -lpthread -L/usr/lib -lssl -lcrypto

clean:
	rm -rf lab4c_tcp lab4c_tls lab4c-004749315.tar.gz *.txt

dist:
	tar -zcf lab4c-004749315.tar.gz lab4c.c README Makefile 
