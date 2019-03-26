# tcp-pong-game
This a full implementation of the game 'Pong'. This specific implementation of the game is played between two people over a tcp socket network.

One of the two people playing will have to be the host. 

## Getting Started
First we want to build the binary on both machines intending to play pong with eachother. To do this we use the following commands from:
```
$ cd ./pong
$ make
g++ -std=c++11 -c -o netpong.o netpong.cpp
g++ -o netpong -std=c++11 -lncurses -lpthread -lz netpong.o
```

With the binaries now created we have to first run this on the host machine (DIFFICULTY = 'easy','medium', or 'hard':
```
$ ./netpong --host [PORT NUMBER] [DIFFICULTY]
```
Then, on the other machine we connect to the host by using the following:
```
$ ./netpong [HOST NAME] [PORT NUMBER]
```
## Example
### Host
```
$ ./netpong --host 4000 easy
Waiting for challengers on port 4000
```
### Challenger (2nd player besides host)
```
$ ./netpong student02.cse.nd.edu 4000
```
The pong game will then begin on both the Host and Challenger machine
