#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <string.h>

#define TTE_VERSION "0.0.1"

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESC_SEQ(cmd) "\x1b["cmd

typedef enum {
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    PAGE_UP,
    PAGE_DOWN,
    HOME_KEY,
    END_KEY,
    DEL_KEY
} EditorKey;
 
typedef struct {
    int cx, cy;
    int screenrows, screencols;
    struct termios orig_ts;
} EditorConfig;

typedef struct {
    char *b;
    size_t len;
} AppendBuf;

EditorConfig cfg;

void ab_append(AppendBuf *ab, const char *s) {
    size_t len = strlen(s);
    char *new = realloc(ab->b, ab->len + len);

    if (new == NULL) return;
    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

void ab_free(AppendBuf *ab) {
    free(ab->b);
}

void die(const char *msg) {
    write(STDOUT_FILENO, ESC_SEQ("2J"), 4);
    write(STDOUT_FILENO, ESC_SEQ("H"), 3);

    perror(msg);
    printf("\r");
    exit(1);
}

void exit_raw_mode() {
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &cfg.orig_ts) == -1) die("exit_raw_mode");
}

void enter_raw_mode() {
    if (tcgetattr(STDIN_FILENO, &cfg.orig_ts) == -1) die("enter_raw_mode");
    atexit(exit_raw_mode);

    struct termios tte_ts = cfg.orig_ts;
    
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

void editor_draw_rows(AppendBuf *ab) {
    int y;
    for(y = 0; y < cfg.screenrows; y++){
        if (y == cfg.screenrows / 3) {
            char splash[256];
            int splashlen = snprintf(splash, sizeof(splash), "T text editor -- version %s", TTE_VERSION);

            int padding = (cfg.screencols - splashlen) / 2;
            if (padding) { ab_append(ab, "~"); padding--; }
            while (padding--) ab_append(ab, " ");
            
            ab_append(ab, splash);
        } else ab_append(ab, "~");

        ab_append(ab, ESC_SEQ("K"));
        if (y < cfg.screenrows - 1) {
            ab_append(ab, "\r\n");;
        }
    };
}

void editor_refresh_screen() {
    AppendBuf buf = {0};

    ab_append(&buf, ESC_SEQ("?25l"));
    ab_append(&buf, ESC_SEQ("H"));

    editor_draw_rows(&buf);

    char cmdbuf[32];
    snprintf(cmdbuf, sizeof(buf), ESC_SEQ("%d;%dH"), cfg.cy + 1, cfg.cx + 1);
    ab_append(&buf, cmdbuf);

    ab_append(&buf, ESC_SEQ("?25h"));

    write(STDOUT_FILENO, buf.b, buf.len);
}

int editor_read_key() {
    int nread;
    char c;
    while ((nread = read(STDIN_FILENO, &c, 1)) != 1) {
        if (nread == -1) die("editor_read_key");
    }

    if (c != '\x1b') return c;
    
    char seq[3];
    if (read(STDIN_FILENO, &seq[0], 1) != 1) return '\x1b';
    if (read(STDIN_FILENO, &seq[1], 1) != 1) return '\x1b';

    if (seq[0] == '[') {
        if (seq[1] >= '0' && seq[1] <= '9'){
            if (read(STDIN_FILENO, &seq[2], 1) != 1) return '\x1b';
            if (seq[2] != '~') goto editor_read_key_main_esc_switch;

            switch (seq[1]) {
                case '1':
                case '7':
                    return HOME_KEY;
                case '4':
                case '8':
                    return END_KEY;
                case '5': return PAGE_UP;
                case '6': return PAGE_DOWN;
                case '3': return DEL_KEY;
            }
        }

editor_read_key_main_esc_switch:
        switch (seq[1]) {
            case 'A': return ARROW_UP;
            case 'B': return ARROW_DOWN;
            case 'C': return ARROW_RIGHT;
            case 'D': return ARROW_LEFT;
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
    }

    if (seq[0] == 'O') {
        switch (seq[1]) {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
        }
    }

    return '\x1b';
}

void editor_move_cursor(int k) {
    switch (k)
    {
    case ARROW_LEFT:
        cfg.cx--;
        break;
    case ARROW_RIGHT:
        cfg.cx++;
        break;
    case ARROW_UP:
        cfg.cy--;
        break;
    case ARROW_DOWN:
        cfg.cy++;
        break;
    }

    if (cfg.cx < 0) cfg.cx = 0;
    if (cfg.cy < 0) cfg.cy = 0;
    if (cfg.cx > cfg.screencols - 1) cfg.cx = cfg.screencols - 1;
    if (cfg.cy > cfg.screenrows - 1) cfg.cy = cfg.screenrows - 1;
}

void editor_process_keypress() {
    int c = editor_read_key();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, ESC_SEQ("2J"), 4);
        write(STDOUT_FILENO, ESC_SEQ("H"), 3);
        exit(0);
        break;
    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT:
        editor_move_cursor(c);
        break;
    case PAGE_DOWN:
    case PAGE_UP:
        cfg.cy = c == PAGE_UP ? 0 : cfg.screenrows - 1;
        break;
    case HOME_KEY:
        cfg.cx = 0;
        break;
    case END_KEY:
        cfg.cx = cfg.screencols - 1;
        break;
    }
}

void get_cursor_pos(int *rows, int *cols) {
    if (write(STDOUT_FILENO, ESC_SEQ("6n"), 4) != 4) die("get_cursor_pos");

    char buf[32];
    unsigned int i = 0;
    while (i < sizeof(buf) - 1) {
        if (read(STDIN_FILENO, &buf[i], 1) != 1) break;
        if (buf[i] == 'R') break;

        i++;
    }
    buf[i] = '\0';
    
    assert(buf[0] == '\x1b' && buf[1] == '[');

    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2) die("get_cursor_pos");
}

void get_win_size(int *rows, int *cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, ESC_SEQ("999C")ESC_SEQ("999B"), 12) != 12) die("get_win_size");
        get_cursor_pos(rows, cols);
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
}

void editor_init() {
    cfg.cx = 0;
    cfg.cy = 0;
    get_win_size(&cfg.screenrows, &cfg.screencols);
}

int main() {
    enter_raw_mode();
    editor_init();

    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }
    return 0;
}