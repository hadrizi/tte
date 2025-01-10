#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>

struct termios orig_ts;

void die(const char *msg) {
    perror(msg);
    exit(1);
}

void exit_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_ts) == -1) die("exit_raw_mode");
}

void enter_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &orig_ts) == -1) die("enter_raw_mode");
    atexit(exit_raw_mode);

    struct termios tte_ts = orig_ts;
    
    // raw mode flags
    tte_ts.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP
                           | INLCR | IGNCR | ICRNL | IXON);
    tte_ts.c_oflag &= ~OPOST;
    tte_ts.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
    tte_ts.c_cflag &= ~(CSIZE | PARENB);
    tte_ts.c_cflag |= CS8;

    // control characters
    tte_ts.c_cc[VMIN] = 0;
    tte_ts.c_cc[VTIME] = 1;

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &tte_ts);
}

int main() {
    enter_raw_mode();

    while (1) {
        char c = '\0';
        if (read(STDIN_FILENO, &c, 1) == -1) die("main");

        if (iscntrl(c)) printf("%d\r\n", c);
        else printf("%d ('%c')\r\n", c, c);

        if (c == 'q') break;
    }
    return 0;
}