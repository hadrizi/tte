#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <ctype.h>
#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <assert.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <stdarg.h>
#include <fcntl.h>
#include <stdbool.h>

#define TTE_VERSION "0.0.1"
#define TTE_TAB_STOP 4
#define TTE_UNSAVED_QUIT_ATTEMTPS 1

#define CTRL_KEY(k) ((k) & 0x1f)
#define ESC_SEQ(cmd) "\x1b["cmd

typedef enum {
    BACKSPACE = 127,
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
    size_t size;
    size_t rsize;
    char *chars;
    char *render;
} EditorRow;

typedef struct {
    int cx, cy;
    int rcx; // render cursor
    int row_offset;
    int screenrows, screencols;
    int numrows;
    EditorRow *rows;
    struct termios orig_ts;
    char *filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int dirty;
} EditorConfig;

typedef struct {
    char *b;
    ssize_t len;
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

int editor_cx_to_rcx(EditorRow *row, int cx) {
    int rcx = 0;
    for (int i = 0; i < cx; i++) {
        if (row->chars[i] == '\t')
            rcx += (TTE_TAB_STOP - 1) - (rcx % TTE_TAB_STOP);
        rcx++;
    }

    return rcx;
}

void editor_draw_rows(AppendBuf *ab) {
    int y;
    for(y = 0; y < cfg.screenrows; y++){
        int _y = y + cfg.row_offset;
        if (_y == cfg.screenrows / 3 && cfg.numrows == 0) {
            char splash[256];
            int splashlen = snprintf(splash, sizeof(splash), "T text editor -- version %s", TTE_VERSION);

            int padding = (cfg.screencols - splashlen) / 2;
            if (padding) { ab_append(ab, "~"); padding--; }
            while (padding--) ab_append(ab, " ");
            
            ab_append(ab, splash);
        }
        else if (_y < cfg.numrows && cfg.numrows != 0) ab_append(ab, cfg.rows[_y].render);
        else ab_append(ab, "~");

        ab_append(ab, ESC_SEQ("K"));
        ab_append(ab, "\r\n");;
    };
}

void editor_draw_status_bar(AppendBuf *ab) {
    ab_append(ab, ESC_SEQ("7m"));

    char lstatus[80], rstatus[80];
    int llen = snprintf(
        lstatus,
        sizeof(lstatus),
        " %.20s - %d lines %s",
        cfg.filename ? cfg.filename : "[No File]", cfg.numrows, cfg.dirty ? "(modified)" : ""
    );
    int rlen = snprintf(
        rstatus,
        sizeof(rstatus),
        "%d/%d ",
        cfg.cy + 1, cfg.numrows
    );

    ab_append(ab, lstatus);
    while (llen < cfg.screencols) {
        if (cfg.screencols - llen == rlen) {
            ab_append(ab, rstatus);
            break;
        }

        ab_append(ab, " ");
        llen++;
    }
    
    ab_append(ab, ESC_SEQ("m"));
    ab_append(ab, "\r\n");
}

void editor_draw_message_bar(AppendBuf *ab) {
    ab_append(ab, ESC_SEQ("K"));
    if (strlen(cfg.statusmsg) && time(NULL) - cfg.statusmsg_time < 5) ab_append(ab, cfg.statusmsg);
}

void editor_scroll() {
    if (cfg.cy < cfg.row_offset) cfg.row_offset = cfg.cy;
    if (cfg.cy >= cfg.row_offset + cfg.screenrows) cfg.row_offset = cfg.cy - cfg.screenrows + 1;
}

void editor_refresh_screen() {
    if (cfg.cy < cfg.numrows) cfg.rcx = editor_cx_to_rcx(&cfg.rows[cfg.cy], cfg.cx);
    editor_scroll();

    AppendBuf buf = {0};

    ab_append(&buf, ESC_SEQ("?25l"));
    ab_append(&buf, ESC_SEQ("H"));

    editor_draw_rows(&buf);
    editor_draw_status_bar(&buf);
    editor_draw_message_bar(&buf);

    char cmdbuf[32];
    snprintf(cmdbuf, sizeof(buf), ESC_SEQ("%d;%dH"), cfg.cy - cfg.row_offset + 1, cfg.rcx + 1);
    ab_append(&buf, cmdbuf);

    ab_append(&buf, ESC_SEQ("?25h"));

    write(STDOUT_FILENO, buf.b, buf.len);

    ab_free(&buf);
}

void editor_set_status_message(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cfg.statusmsg, sizeof(cfg.statusmsg), fmt, ap);
    va_end(ap);
    cfg.statusmsg_time = time(NULL);
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


    if (cfg.cx < 0 && cfg.cy > 0) {
        cfg.cy--;
        cfg.cx = cfg.rows[cfg.cy].size;
    }

    EditorRow *row = (cfg.cy >= cfg.numrows) ? NULL : &cfg.rows[cfg.cy];
    int rowlen = row ? row->size : 0;
    if (cfg.cx > rowlen && cfg.cy < cfg.numrows) {
        cfg.cy++;
        cfg.cx = 0;
    }
    
    if (cfg.cx < 0) cfg.cx = 0;
    if (cfg.cy < 0) cfg.cy = 0;
    if (cfg.cx > rowlen) cfg.cx = rowlen;
    if (cfg.cy > cfg.numrows) cfg.cy = cfg.numrows;
}

void editor_render_row(EditorRow *row) {
    int tabs = 0;
    for (size_t i = 0; i < row->size; i++) if (row->chars[i] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (TTE_TAB_STOP - 1) + 1);

    int rsize = 0;
    for (size_t i = 0; i < row->size; i++) {
        if (row->chars[i] != '\t') {
            row->render[rsize++] = row->chars[i];
            continue;
        }

        row->render[rsize++] = ' ';
        while(rsize % TTE_TAB_STOP != 0) row->render[rsize++] = ' ';
    }
    row->render[rsize] = '\0';
    row->rsize = rsize;
}

void editor_insert_row(int at, char *s, size_t len) {
    if (at > cfg.numrows) return;

    cfg.rows = realloc(cfg.rows, sizeof(EditorRow) * (cfg.numrows + 1));
    memmove(&cfg.rows[at + 1], &cfg.rows[at], sizeof(EditorRow) * (cfg.numrows - at));

    cfg.rows = realloc(cfg.rows, sizeof(EditorRow) * (cfg.numrows + 1));

    cfg.rows[at].size = len;
    cfg.rows[at].chars = malloc(len + 1);
    memcpy(cfg.rows[at].chars, s, len);
    cfg.rows[at].chars[len] = '\0';

    cfg.rows[at].rsize = 0;
    cfg.rows[at].render = NULL;
    editor_render_row(&cfg.rows[at]);

    cfg.numrows++;
    cfg.dirty++;
}

void editor_insert_new_line() {
    if (cfg.cx == 0) editor_insert_row(cfg.cy, "", 0);
    else {
        EditorRow *cur_row = &cfg.rows[cfg.cy];
        editor_insert_row(cfg.cy + 1, &cur_row->chars[cfg.cx], cur_row->size - cfg.cx);
        
        cur_row = &cfg.rows[cfg.cy];
        cur_row->size = cfg.cx;
        cur_row->chars[cur_row->size] = '\0';
        editor_render_row(cur_row);
    }
    cfg.cy++;
    cfg.cx = 0;
}

void editor_row_free(EditorRow *row) {
    free(row->render);
    free(row->chars);
}

void editor_del_row(int at) {
    if (at >= cfg.numrows) return;
    editor_row_free(&cfg.rows[at]);
    memmove(&cfg.rows[at], &cfg.rows[at + 1], sizeof(EditorRow) * (cfg.numrows - at - 1));
    cfg.numrows--;
    cfg.dirty++;
}

void editor_row_append_string(EditorRow *row, char *s) {
    size_t len = strlen(s);
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_render_row(row);
    cfg.dirty++;
}

void editor_row_insert_char(EditorRow *row, size_t at, int c) {
    if (at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);

    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_render_row(row);

    cfg.dirty++;
}

void editor_insert_char(int c) {
    if (cfg.cy == cfg.numrows) editor_insert_row(cfg.numrows, "", 0);
    editor_row_insert_char(&cfg.rows[cfg.cy], cfg.cx, c);
    cfg.cx++;
}

void editor_row_del_char(EditorRow *row, size_t at) {
    if (at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_render_row(row);
    cfg.dirty++;
}

void editor_del_char() {
    if (cfg.cy == cfg.numrows) return;
    if (cfg.cx == 0 && cfg.cy == 0) return;

    EditorRow *cur_row = &cfg.rows[cfg.cy];
    if (cfg.cx > 0) {
        editor_row_del_char(cur_row, cfg.cx - 1);
        cfg.cx--;
    } else {
        cfg.cx = cfg.rows[cfg.cy - 1].size;
        editor_row_append_string(&cfg.rows[cfg.cy - 1], cur_row->chars);
        editor_del_row(cfg.cy);
        cfg.cy--;
    }
}

char *editor_get_rows_as_str(size_t *buflen) {
    size_t len = 0;
    for (int i = 0; i < cfg.numrows; i++) len += cfg.rows[i].size + 1;
    *buflen = len;

    char *buf = malloc(len);
    char *p = buf;

    for(int i = 0; i < cfg.numrows; i++) {
        memcpy(p, cfg.rows[i].chars, cfg.rows[i].size);
        p += cfg.rows[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editor_save() {
    if (!cfg.filename) return;

    size_t len;
    char *buf = editor_get_rows_as_str(&len);

    int fd = open(cfg.filename, O_RDWR | O_CREAT, 0644);
    if (fd == -1) {
        editor_set_status_message("Can't save! I/O error: %s", strerror(errno));
        goto editor_save_end;
    }
    if (ftruncate(fd, len) == -1) {
        editor_set_status_message("Can't save! I/O error: %s", strerror(errno));
        goto editor_save_end;
    }
    
    ssize_t written_len = write(fd, buf, len);
    editor_set_status_message("%d bytes written to disk", written_len);

    cfg.dirty = 0;

editor_save_end:
    close(fd);
    free(buf);
}

void editor_process_keypress() {
    static int unsaved_quit_attempts = TTE_UNSAVED_QUIT_ATTEMTPS;
    int c = editor_read_key();

    switch (c)
    {
    case '\r':
      editor_insert_new_line();
      break;
    case CTRL_KEY('q'):
        if (cfg.dirty && unsaved_quit_attempts) {
            editor_set_status_message("File has unsaved changes. Press Ctrl-Q %d more times to quit", unsaved_quit_attempts);
            unsaved_quit_attempts--;
            return;
        }
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
        if (c == PAGE_UP) cfg.cy = cfg.row_offset;
        if (c == PAGE_DOWN) cfg.cy = cfg.row_offset + cfg.screenrows - 1;

        int t = cfg.screenrows;
        while (t--) {
            editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
        }
        break;
    case HOME_KEY:
        cfg.cx = 0;
        break;
    case END_KEY:
        if (cfg.cy < cfg.numrows) cfg.cx = cfg.rows[cfg.cy].size;
        break;
    case BACKSPACE:
    case CTRL_KEY('h'):
    case DEL_KEY:
      if (c == DEL_KEY) editor_move_cursor(ARROW_RIGHT);
      editor_del_char();
      break;
    case CTRL_KEY('l'):
    case '\x1b':
      break;
    case CTRL_KEY('s'):
        editor_save();
        break;
    default:
        editor_insert_char(c);
        break;
    }

    unsaved_quit_attempts = TTE_UNSAVED_QUIT_ATTEMTPS;
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
    cfg.rcx = cfg.cx;
    cfg.numrows = 0;
    cfg.row_offset = 0;
    cfg.rows = NULL;
    cfg.filename = NULL;
    cfg.statusmsg[0] = '\0';
    cfg.statusmsg_time = 0;
    cfg.dirty = 0;
    
    get_win_size(&cfg.screenrows, &cfg.screencols);
    cfg.screenrows -= 2;
}

void editor_open(char *filename) {
    free(cfg.filename);
    cfg.filename = strdup(filename);

    FILE *fp = fopen(filename, "r");
    if (!fp) die("editor_open");
    
    char *line = NULL;
    size_t linecap = 0;
    ssize_t linelen;

    while((linelen = getline(&line, &linecap, fp)) != -1){
        while (linelen > 0 && (line[linelen - 1] == '\n' || line[linelen - 1] == '\r')) linelen--;
        editor_insert_row(cfg.numrows, line, linelen);
    }

    cfg.dirty = 0;

    free(line);
    fclose(fp);
}

int main(int argc, char *argv[]) {
    enter_raw_mode();
    editor_init();

    if (argc >= 2) {
        editor_open(argv[1]);
    }

    editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit");
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    for (int i = 0; i < cfg.numrows; i++) { free(cfg.rows[i].chars); free(cfg.rows[i].render); };
    free(cfg.rows);
    free(cfg.filename);
    return 0;
}