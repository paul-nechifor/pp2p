all: client server

client:
	cd client && qmake && make

.PHONY: clean client server

clean:
	rm -f */*.o */Makefile /client/pp2p /server/server
