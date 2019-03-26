#include <ncurses.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <iostream>
#include <sys/time.h>
#include <sys/socket.h>
#include <signal.h>
#include <netinet/in.h>
#include <netdb.h>
#include <mutex>

#define WIDTH 43
#define HEIGHT 21
#define PADLX 1
#define PADRX WIDTH - 2
#define MAX_LINE 2048
#define MAX_PENDING 5

using namespace std;

// Global variables recording the state of the game
// Position of ball
int ballX, ballY;
// Movement of ball
int dx, dy;
// Position of paddles
int padLY, padRY;
// Player scores
int scoreL, scoreR;
// ncurses window
WINDOW *win;
// host or not
bool ISHOST = false;
// sock
int FINALSOCK;
// log control
bool WRITELOG = false;

// handle declaration of locks

pthread_mutex_t boardItemsLock = PTHREAD_MUTEX_INITIALIZER;

// prototype functions
int recvInt(int);
void *networkInteraction(void*);
int connectAsHost(int);
int connectAsGeneral(char*,char*);
void handler(int);
void send_string(int, string);
void sendInt(int,int);
int recvWithCheck(int, string&);
void printLog(string);
void draw(int,int,int,int,int,int);
void reset();
void countdown(const char*);
void tock(int);
void *listenInput(void *);
void initNcurses();

int main(int argc, char *argv[]) {
    // Process args
    // refresh is clock rate in microseconds
    // This corresponds to the movement speed of the ball
    int refresh;
    int sockFinal;
    if(argc >= 3) {
        char *hostFlag = argv[1];
        if (strcmp(hostFlag, "--host") == 0){ // host user
            ISHOST = true;
            if (argc < 4){
                printf("Host Usage: ./netpong --host PORT DIFFICULTY\n");
                exit(1);
            }
            int port = atoi(argv[2]);
            char *difficulty = argv[3];
            if(strcmp(difficulty, "easy") == 0) refresh = 80000;
            else if(strcmp(difficulty, "medium") == 0) refresh = 40000;
            else if(strcmp(difficulty, "hard") == 0) refresh = 20000;
            else {
                printf("ERROR: Difficulty should be one of easy, medium, hard.\n");
                exit(1);
            }
            sockFinal = connectAsHost(port);
            string diff = difficulty;
            if (argc == 5) {
                char *str = argv[4];
                if (strcmp(str, "DEBUG") == 0) WRITELOG = true;
            }
            send_string(sockFinal, diff);
        }
        else { // general user
            sockFinal = connectAsGeneral(argv[1], argv[2]);
            string difficulty;
            recvWithCheck(sockFinal, difficulty);
            if (difficulty ==  "easy") refresh = 80000;
            else if (difficulty == "medium") refresh = 40000;
            else if (difficulty == "hard") refresh = 20000;
            else {
                printf("ERROR: user received weird difficulty setting\n");
                exit(1);
            }
            if (argc == 4) {
                char *str = argv[3];
                if (strcmp(str, "DEBUG") == 0) WRITELOG = true;
            }
        }
    } 
    else { // usage if arguments are not right
        printf("General Usage: ./netpong HOSTNAME PORT\n");
        printf("Host Usage: ./netpong --host PORT DIFFICULTY\n");
        exit(0);
    }
    FINALSOCK = sockFinal;
    printLog("CONNECTIONS WORKED");
    // Set up ncurses environment
    initNcurses();

    // Set starting game state and display a countdown
    reset();
    countdown("Starting Game");
    
    // Listen to keyboard input in a background thread
    pthread_t pth;
    if (pthread_create(&pth, NULL, listenInput, &sockFinal) < 0){
        perror("listenInput socket");
        exit(1);
    }
    
    // thread to handle game board updates from other user 
    pthread_t networkThread;
    if (pthread_create(&networkThread, NULL, networkInteraction, &sockFinal) < 0){
        perror("networkInteraction socket");
        exit(1);
    }

    // Main game loop executes tock() method every REFRESH microseconds
    struct timeval tv;
    while(1) {
        signal(SIGINT, handler);
        gettimeofday(&tv,NULL);
        unsigned long before = 1000000 * tv.tv_sec + tv.tv_usec;
        // lock function since that is where game board is updated
        pthread_mutex_lock(&boardItemsLock); 
        tock(sockFinal); // Update game state
        pthread_mutex_unlock(&boardItemsLock);
        gettimeofday(&tv,NULL);
        unsigned long after = 1000000 * tv.tv_sec + tv.tv_usec;
        unsigned long toSleep = refresh - (after - before);
        // toSleep can sometimes be > refresh, e.g. countdown() is called during tock()
        // In that case it's MUCH bigger because of overflow!
        if(toSleep > refresh) toSleep = refresh;
        usleep(toSleep); // Sleep exactly as much as is necessary
    }
    
    // Clean up
    pthread_join(pth, NULL);
    endwin();
    return 0;
}

// recv a single int
int recvInt(int sockFd) {
    int numBytesRec;
    int num;
    int flags = 0;
    
    if ((numBytesRec=recv(sockFd, &num, sizeof(num) , flags)) == -1){
        perror("receive error");
        close(sockFd);
        exit(1);
    }
    if (numBytesRec == 0){
        cout << "recvWithCheck: zero bytes received" << endl;
        close(sockFd);
        exit(1);
    }
    return ntohl(num);
}

// used for thread to update game board given info from other user
void *networkInteraction(void *sock){
    int sockFd = *(int*)sock;
    int read_size;
    char buf[MAX_LINE];
    string msgFromOtherUser;
    while(1){
        recvWithCheck(sockFd, msgFromOtherUser);
        // update when ball hits paddle
        if (msgFromOtherUser == "ball"){
            pthread_mutex_lock(&boardItemsLock);
            dx = recvInt(sockFd);
            dy = recvInt(sockFd);
            ballX = recvInt(sockFd);
            ballY = recvInt(sockFd);
            pthread_mutex_unlock(&boardItemsLock);
        }
        // update when ball hits top
        else if (msgFromOtherUser == "ballY"){
            pthread_mutex_lock(&boardItemsLock);
            dy = recvInt(sockFd);
            pthread_mutex_unlock(&boardItemsLock);
        }
        // update pad if other user moves
        else if (msgFromOtherUser == "padR"){
            pthread_mutex_lock(&boardItemsLock);
            padRY = recvInt(sockFd);
            pthread_mutex_unlock(&boardItemsLock);
        }
        // update pad if other user moves
        else if (msgFromOtherUser == "padL"){
            pthread_mutex_lock(&boardItemsLock);
            padLY = recvInt(sockFd);
            pthread_mutex_unlock(&boardItemsLock);
        }
        // update score for left player
        else if (msgFromOtherUser == "scoreL"){
            pthread_mutex_lock(&boardItemsLock);
            scoreL = recvInt(sockFd);
            reset();
            countdown("<-- SCORE");
            pthread_mutex_unlock(&boardItemsLock);
        }
        // update score for right player
        else if (msgFromOtherUser == "scoreR") {
            pthread_mutex_lock(&boardItemsLock);
            scoreR = recvInt(sockFd);
            reset();
            countdown("SCORE -->");
            pthread_mutex_unlock(&boardItemsLock);
        }
        // quit and cleanup
        else if (msgFromOtherUser == "quit"){
            printLog("received SIGINT from other player");
            close(sockFd);
            endwin();
            exit(1);
        }
    }
}

// host the connection (bind, listen, accept)
int connectAsHost(int port){
    printLog("HOST: Attempt Connection");
    
    // build address data structure
    struct sockaddr_in sin;
    bzero((char*)&sin, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(port);

    // passive open
    int listenSockFd;
    if ((listenSockFd = socket(PF_INET, SOCK_STREAM, 0)) < 0) {
        perror("host: socket");
        exit(1);
    }
    
    // set the socket options
    int opt = 1;
    if ((setsockopt(listenSockFd, SOL_SOCKET, SO_REUSEADDR, (char*)&opt, sizeof(int))) < 0){
        perror("host: setsocket");
        close(listenSockFd);
        exit(1);
    }
    if ((bind(listenSockFd, (struct sockaddr *)&sin, sizeof(sin))) < 0){
        perror("host: bind");
        close(listenSockFd);
        exit(1);
    }
    if ((listen(listenSockFd, MAX_PENDING)) < 0){
        perror("host: listen");
        close(listenSockFd);
        exit(1);
    }

    cout << "Waiting for challengers on port " << port << endl;
    int hostSockFd, addr_len;
    struct sockaddr_in client_addr;
    while ((hostSockFd = accept(listenSockFd, (struct sockaddr*)&client_addr, (socklen_t*)&addr_len)) >= 0){
        printLog("HOST: Accepted connection");
        close(listenSockFd);
        return hostSockFd;
    }
    
}

// client (general) connection
int connectAsGeneral(char *hostname, char * port){
    printLog("GENERAL: Attempt Connection");
    // load address structure
    struct addrinfo hints, *clientInfo, *ptr;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    
    int returnVal;
    if ((returnVal = getaddrinfo(hostname, port, &hints, &clientInfo)) != 0){
        perror("getaddrinfo");
        exit(1);
    }
    cout << "Connecting to " << hostname << " on port " << port << endl;
    int clientSockFd;
    for (ptr = clientInfo; ptr != NULL; ptr = ptr->ai_next) {
        if ((clientSockFd = socket(ptr->ai_family, ptr->ai_socktype, ptr->ai_protocol)) < 0){
            perror("Client: bad socket");
            continue;
        }
        if (connect(clientSockFd, ptr->ai_addr, ptr->ai_addrlen) < 0){
            perror("client: bad connect");
            close(clientSockFd);
            continue;
        }
    }
    cout << "Connected to " << hostname << endl;
    return clientSockFd;
}

// handles signal (SIGINT specifically)
void handler(int signal){
    printLog("SIGINT");
    send_string(FINALSOCK, "quit");
    endwin();
    exit(0);
}

// send string to other user
void send_string(int sock, string toSend){
    printLog("sending: ");
    printLog(toSend.c_str());
    int len = toSend.length();
    int flags = 0;
    if (send(sock, toSend.c_str(), len, flags) == -1){
        perror("client send error!\n");
        exit(1);
    }
}

// receive int from other user
void sendInt(int sock, int toSend){
    toSend = htonl(toSend);
    if (send(sock, &toSend, sizeof(toSend), 0) == -1){
        perror("sendInt error");
        exit(1);
    }
}

// handle recieving messages
int recvWithCheck(int sockFd, string &outMsg) {
    int numBytesRec;
    char buf[MAX_LINE];
    int len = sizeof(buf);
    int flags = 0;

    memset(buf, 0, len);
    
    if ((numBytesRec=recv(sockFd, buf, len, flags)) == -1){
        perror("receive error");
        close(sockFd);
        exit(1);
    }
    if (numBytesRec == 0){
        cout << "recvWithCheck: zero bytes received" << endl;
        close(sockFd);
        exit(1);
    }
    outMsg = buf;
    printLog("receiiving: " + outMsg);
    return numBytesRec;
}

// need to print logs to file because of ncurses
void printLog(string msg){
    if (WRITELOG){
        FILE *f = fopen("log", "a");
        fprintf(f, "%s \n", msg.c_str());
        fclose(f);
    }
}

/* Draw the current game state to the screen
 * ballX: X position of the ball
 * ballY: Y position of the ball
 * padLY: Y position of the left paddle
        pthread_mutex_unlock(&boardItemsLock);
 * padRY: Y position of the right paddle
 * scoreL: Score of the left player
 * scoreR: Score of the right player
 */
void draw(int ballX, int ballY, int padLY, int padRY, int scoreL, int scoreR) {
    // Center line
    int y;
    for(y = 1; y < HEIGHT-1; y++) {
        mvwaddch(win, y, WIDTH / 2, ACS_VLINE);
    }
    // Score
    mvwprintw(win, 1, WIDTH / 2 - 3, "%2d", scoreL);
    mvwprintw(win, 1, WIDTH / 2 + 2, "%d", scoreR);
    // Ball
    mvwaddch(win, ballY, ballX, ACS_BLOCK);
    // Left paddle
    for(y = 1; y < HEIGHT - 1; y++) {
	int ch = (y >= padLY - 2 && y <= padLY + 2)? ACS_BLOCK : ' ';
        mvwaddch(win, y, PADLX, ch);
    }
    // Right paddle
    for(y = 1; y < HEIGHT - 1; y++) {
	int ch = (y >= padRY - 2 && y <= padRY + 2)? ACS_BLOCK : ' ';
        mvwaddch(win, y, PADRX, ch);
    }
    // Print the virtual window (win) to the screen
    wrefresh(win);
    // Finally erase ball for next time (allows ball to move before next refresh)
    mvwaddch(win, ballY, ballX, ' ');
}

/* Return ball and paddles to starting positions
 * Horizontal direction of the ball is randomized
 */
void reset() {
    ballX = WIDTH / 2;
    padLY = padRY = ballY = HEIGHT / 2;
    // dx is randomly either -1 or 1
    dx = (rand() % 2) * 2 - 1;
    dy = 0;
    // Draw to reset everything visually
    draw(ballX, ballY, padLY, padRY, scoreL, scoreR);
}

/* Display a message with a 3 second countdown
 * This method blocks for the duration of the countdown
 * message: The text to display during the countdown
 */
void countdown(const char *message) {
    printLog("TEST COUNTDOWN");
    int h = 4;
    int w = strlen(message) + 4;
    WINDOW *popup = newwin(h, w, (LINES - h) / 2, (COLS - w) / 2);
    box(popup, 0, 0);
    mvwprintw(popup, 1, 2, message);
    int countdown;
    for(countdown = 3; countdown > 0; countdown--) {
        mvwprintw(popup, 2, w / 2, "%d", countdown);
        wrefresh(popup);
        sleep(1);
    }
    wclear(popup);
    wrefresh(popup);
    delwin(popup);
    padLY = padRY = HEIGHT / 2; // Wipe out any input that accumulated during the delay
}

/* Perform periodic game functions:
 * 1. Move the ball
 * 2. Detect collisions
 * 3. Detect scored points and react accordingly
 * 4. Draw updated game state to the screen
 */
void tock(int sockFd) {
    // Move the ball
    ballX += dx;
    ballY += dy;
    
    // Check for paddle collisions
    // padY is y value of closest paddle to ball
    int padY = (ballX < WIDTH / 2) ? padLY : padRY;
    // colX is x value of ball for a paddle collision
    int colX = (ballX < WIDTH / 2) ? PADLX + 1 : PADRX - 1;
    if(ballX == colX && abs(ballY - padY) <= 2) {
        // Collision detected!
        dx *= -1;
        // Determine bounce angle
        if(ballY < padY){
            dy = -1;
        }
        else if(ballY > padY) {
            dy = 1;
        }
        else {
            dy = 0;
        }
        send_string(sockFd, "ball");
        sendInt(sockFd, dx);
        sendInt(sockFd, dy);
        sendInt(sockFd, ballX);
        sendInt(sockFd, ballY);
    }

    // Check for top/bottom boundary collisions
    if(ballY == 1) {
        dy = 1;
        send_string(sockFd, "ballY");
        sendInt(sockFd, dy);
    }
    else if(ballY == HEIGHT - 2) {
        dy = -1;
        send_string(sockFd, "ballY");
        sendInt(sockFd, dy);
    }
    
    // Score points
    if(ballX == 0) { // ball goes past left paddle, right side scores
        if (!ISHOST) { // if you are not the host (you play on left side)
            scoreR = (scoreR + 1) % 100;
            pthread_mutex_unlock(&boardItemsLock);
            send_string(sockFd, "scoreR");
            sendInt(sockFd, scoreR);
            pthread_mutex_lock(&boardItemsLock);
            reset();
	        countdown("SCORE -->");
        }
    } else if(ballX == WIDTH - 1) { // ball goes past right paddle, left side scores
        if (ISHOST) { // you are the host (you play on the right side)
            scoreL = (scoreL + 1) % 100;
            pthread_mutex_unlock(&boardItemsLock);
            send_string(sockFd, "scoreL");
            sendInt(sockFd, scoreL);
            pthread_mutex_lock(&boardItemsLock);
	        reset();
	        countdown("<-- SCORE");
        }
    }
    // Finally, redraw the current state
    draw(ballX, ballY, padLY, padRY, scoreL, scoreR);
}

/* Listen to keyboard input
 * Updates global pad positions
 */
void *listenInput(void *sock) {
    int sockFd = *(int*)sock;
    while(1) {
        switch(getch()) {
            case KEY_UP:
             pthread_mutex_lock(&boardItemsLock);
             if (ISHOST) {
                padRY--;
                send_string(sockFd, "padR");
                sendInt(sockFd, padRY);
             }
             pthread_mutex_unlock(&boardItemsLock);
			 break;
            case KEY_DOWN: 
             pthread_mutex_lock(&boardItemsLock);
             if (ISHOST) {
                padRY++;
                send_string(sockFd, "padR");
                sendInt(sockFd, padRY);
             }
             pthread_mutex_unlock(&boardItemsLock);
			 break;
            case 'w': 
             pthread_mutex_lock(&boardItemsLock);
             if (!ISHOST) {
                padLY--;
                send_string(sockFd, "padL");
                sendInt(sockFd, padLY);
             }
             pthread_mutex_unlock(&boardItemsLock);
			 break;
            case 's': 
             pthread_mutex_lock(&boardItemsLock);
             if (!ISHOST) {
                padLY++;
                send_string(sockFd, "padR");
                sendInt(sockFd, padRY);
             }
             pthread_mutex_unlock(&boardItemsLock);
			 break;
            default: break;
	}
	
    }	    
    return NULL;
}

void initNcurses() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    refresh();
    win = newwin(HEIGHT, WIDTH, (LINES - HEIGHT) / 2, (COLS - WIDTH) / 2);
    box(win, 0, 0);
    mvwaddch(win, 0, WIDTH / 2, ACS_TTEE);
    mvwaddch(win, HEIGHT-1, WIDTH / 2, ACS_BTEE);
}


