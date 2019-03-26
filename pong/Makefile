all: netpong

netpong.o: netpong.cpp
	g++ -std=c++11 -c -o netpong.o netpong.cpp

netpong: netpong.o
	g++ -o netpong -std=c++11 -lncurses -lpthread -lz netpong.o

clean:
	@echo Cleaning...
	@rm netpong
	@rm netpong.o

