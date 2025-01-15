#define _DEFAULT_SOURCE
#define _BSD_SOURCE
#define _GNU_SOURCE

#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <assert.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/ioctl.h>

#include "editor.h"

EditorConfig cfg;

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

void get_cursor_pos(size_t* rows, size_t* cols) {
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

    if (sscanf(&buf[2], "%zu;%zu", rows, cols) != 2) die("get_cursor_pos");
}

void get_win_size(size_t* rows, size_t* cols) {
    struct winsize ws;

    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) {
        if (write(STDOUT_FILENO, ESC_SEQ("999C")ESC_SEQ("999B"), 12) != 12) die("get_win_size");
        get_cursor_pos(rows, cols);
    }

    *cols = ws.ws_col;
    *rows = ws.ws_row;
}

size_t editor_row_cx_to_rcx(EditorRow* row, size_t cx) {
    size_t rcx = 0;
    for (size_t i = 0; i < cx; i++) {
        if (row->chars[i] == '\t')
            rcx += (TTE_TAB_STOP - 1) - (rcx % TTE_TAB_STOP);
        rcx++;
    }

    return rcx;
}

size_t editor_row_rcx_to_cx(EditorRow* row, size_t rcx) {
    size_t cur_rcx = 0;
    size_t cx;
    for (cx = 0; cx < row->size; cx++) {
        if (row->chars[cx] == '\t') cur_rcx += (TTE_TAB_STOP - 1) - (cur_rcx % TTE_TAB_STOP);
        cur_rcx++;

        if (cur_rcx > rcx) return cx;
    }

    return cx;
}

void editor_row_make_render(EditorRow* row) {
    int tabs = 0;
    for (size_t i = 0; i < row->size; i++) if (row->chars[i] == '\t') tabs++;

    free(row->render);
    row->render = malloc(row->size + tabs * (TTE_TAB_STOP - 1) + 1);

    size_t rsize = 0;
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

    editor_update_highlight(row);
}

void editor_row_append_string(EditorRow* row, char* s) {
    size_t len = strlen(s);
    row->chars = realloc(row->chars, row->size + len + 1);
    memcpy(&row->chars[row->size], s, len);
    row->size += len;
    row->chars[row->size] = '\0';
    editor_row_make_render(row);
    cfg.dirty++;
}

void editor_row_insert_char(EditorRow* row, size_t at, int c) {
    if (at > row->size) at = row->size;
    row->chars = realloc(row->chars, row->size + 2);

    memmove(&row->chars[at + 1], &row->chars[at], row->size - at + 1);
    row->size++;
    row->chars[at] = c;
    editor_row_make_render(row);

    cfg.dirty++;
}

void editor_row_del_char(EditorRow* row, size_t at) {
    if (at >= row->size) return;
    memmove(&row->chars[at], &row->chars[at + 1], row->size - at);
    row->size--;
    editor_row_make_render(row);
    cfg.dirty++;
}

void editor_update_highlight(EditorRow* row) {
    row->hl = realloc(row->hl, row->rsize);
    memset(row->hl, HL_NORMAL, row->rsize);

    for (size_t i = 0; i < row->rsize; i++) {
        if (isdigit(row->render[i])) row->hl[i] = HL_NUMBER;
    }
}

void editor_row_free(EditorRow* row) {
    free(row->render);
    free(row->chars);
    free(row->hl);
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

void editor_free() {
    for (size_t i = 0; i < cfg.numrows; i++) { editor_row_free(&cfg.rows[i]); };

    free(cfg.rows);
    free(cfg.filename);
}

void editor_refresh_screen() {
    if ((size_t)cfg.cy < cfg.numrows) cfg.rcx = editor_row_cx_to_rcx(&cfg.rows[cfg.cy], cfg.cx);
    editor_scroll();

    AppendBuf buf = {0};

    ab_append(&buf, ESC_SEQ("?25l"), 6);
    ab_append(&buf, ESC_SEQ("H"), 3);

    editor_draw_rows(&buf);
    editor_draw_status_bar(&buf);
    editor_draw_message_bar(&buf);

    char cursorcmd_buf[32];
    snprintf(cursorcmd_buf, sizeof(buf), ESC_SEQ("%d;%dH"), cfg.cy - cfg.row_offset + 1, cfg.rcx + 1);
    ab_append(&buf, cursorcmd_buf, strlen(cursorcmd_buf));

    ab_append(&buf, ESC_SEQ("?25h"), 6);

    write(STDOUT_FILENO, buf.b, buf.len);

    ab_free(&buf);
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
        if ((size_t)cfg.cy < cfg.numrows) cfg.cx = cfg.rows[cfg.cy].size;
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
    case CTRL_KEY('f'):
      editor_search();
      break;
    default:
        editor_insert_char(c);
        break;
    }

    unsaved_quit_attempts = TTE_UNSAVED_QUIT_ATTEMTPS;
}

void editor_save() {
    // Looks weird but editor_prompt may return NULL so this case should still be handled
    if (!cfg.filename) cfg.filename = editor_prompt("Save as: %s", NULL);
    if (!cfg.filename) return;

    size_t len;
    char* buf = editor_get_rows_as_str(&len);

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

void editor_open(char* filename) {
    free(cfg.filename);
    cfg.filename = strdup(filename);

    FILE* fp = fopen(filename, "r");
    if (!fp) die("editor_open");
    
    char* line = NULL;
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

char* editor_get_rows_as_str(size_t* buflen) {
    size_t len = 0;
    for (size_t i = 0; i < cfg.numrows; i++) len += cfg.rows[i].size + 1;
    *buflen = len;

    char* buf = malloc(len);
    char* p = buf;

    for(size_t i = 0; i < cfg.numrows; i++) {
        memcpy(p, cfg.rows[i].chars, cfg.rows[i].size);
        p += cfg.rows[i].size;
        *p = '\n';
        p++;
    }

    return buf;
}

void editor_draw_rows(AppendBuf* ab) {
    size_t y;
    for(y = 0; y < cfg.screenrows; y++){
        size_t _y = y + cfg.row_offset;
        if (_y == cfg.screenrows / 3 && cfg.numrows == 0) {
            char splash[256];
            int splashlen = snprintf(splash, sizeof(splash), "T text editor -- version %s", TTE_VERSION);

            int padding = (cfg.screencols - splashlen) / 2;
            if (padding) { ab_append(ab, "~", 1); padding--; }
            while (padding--) ab_append(ab, " ", 1);
            
            ab_append(ab, splash, splashlen);
        }
        else if (_y < cfg.numrows && cfg.numrows != 0) {
            char* s = cfg.rows[_y].render;
            unsigned char* hl = cfg.rows[_y].hl;
            int cur_col = -1;
            for(size_t i = 0; i < cfg.rows[_y].rsize; i++) {
                if (hl[i] == HL_NORMAL) {
                    if (cur_col != -1) ab_append(ab, ESC_SEQ("39m"), 5);
                    cur_col = -1;
                    ab_append(ab, &s[i], 1);
                    continue;
                }

                int col = editor_highlight_to_color(hl[i]);

                if (col != cur_col) {
                    char buf[16];
                    int clen = snprintf(buf, sizeof(buf), ESC_SEQ("%dm"), col);
                    ab_append(ab, buf, clen);
                    cur_col = col;
                }
                
                ab_append(ab, &s[i], 1);
            }
            ab_append(ab, ESC_SEQ("39m"), 5);
        }
        else ab_append(ab, "~", 1);

        ab_append(ab, ESC_SEQ("K"), 3);
        ab_append(ab, "\r\n", 2);;
    };
}

void editor_draw_status_bar(AppendBuf* ab) {
    ab_append(ab, ESC_SEQ("7m"), 4);

    char lstatus[80], rstatus[80];
    size_t llen = snprintf(
        lstatus,
        sizeof(lstatus),
        " %.20s - %zu lines %s",
        cfg.filename ? cfg.filename : "[No File]", cfg.numrows, cfg.dirty ? "(modified)" : ""
    );
    size_t rlen = snprintf(
        rstatus,
        sizeof(rstatus),
        "%d/%zu ",
        cfg.cy + 1, cfg.numrows
    );

    ab_append(ab, lstatus, llen);
    while (llen < cfg.screencols) {
        if (cfg.screencols - llen == rlen) {
            ab_append(ab, rstatus, rlen);
            break;
        }

        ab_append(ab, " ", 1);
        llen++;
    }
    
    ab_append(ab, ESC_SEQ("m"), 3);
    ab_append(ab, "\r\n", 2);
}

void editor_draw_message_bar(AppendBuf* ab) {
    ab_append(ab, ESC_SEQ("K"), 3);
    size_t len = strlen(cfg.statusmsg);
    if (len && time(NULL) - cfg.statusmsg_time < 5) ab_append(ab, cfg.statusmsg, len);
}

void editor_set_status_message(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(cfg.statusmsg, sizeof(cfg.statusmsg), fmt, ap);
    va_end(ap);
    cfg.statusmsg_time = time(NULL);
}

char* editor_prompt(char* prompt, void (*callback)(char* , int)) {
    // XXX: replace buf with EditorRow for easier navigation?
    size_t bufsize = 128;
    char* buf = malloc(bufsize);

    size_t buflen = 0;
    buf[0] = '\0';

    while(1) {
        editor_set_status_message(prompt, buf);
        editor_refresh_screen();

        int c = editor_read_key();
        if (c == DEL_KEY || c == CTRL_KEY('h') || c == BACKSPACE) {
            if (buflen != 0) buf[--buflen] = '\0';
        } else if (c == '\x1b') {
            editor_set_status_message("");
            if (callback) callback(buf, c);
            free(buf);
            return NULL;
        } else if (c == '\r' && buflen != 0) {
            editor_set_status_message("");
            if (callback) callback(buf, c);
            return buf;
        } else if (!iscntrl(c) && c < 128) {
            if (buflen != bufsize - 1) {
                bufsize *= 2;
                buf = realloc(buf, bufsize);
            }
            buf[buflen++] = c;
            buf[buflen] = '\0';
        }

        if (callback) callback(buf, c);
    }
}

void editor_scroll() {
    if (cfg.cy < (int)cfg.row_offset) cfg.row_offset = cfg.cy;
    if ((size_t)cfg.cy >= cfg.row_offset + cfg.screenrows) cfg.row_offset = cfg.cy - cfg.screenrows + 1;
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

    EditorRow* row = ((size_t)cfg.cy >= cfg.numrows) ? NULL : &cfg.rows[cfg.cy];
    size_t rowlen = row ? row->size : 0;
    if (cfg.cx > (int)rowlen && cfg.cy < (int)cfg.numrows) {
        cfg.cy++;
        cfg.cx = 0;
    }
    
    if (cfg.cx < 0) cfg.cx = 0;
    if (cfg.cy < 0) cfg.cy = 0;
    if (cfg.cx > (int)rowlen) cfg.cx = rowlen;
    if (cfg.cy > (int)cfg.numrows) cfg.cy = cfg.numrows;
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

void editor_insert_row(size_t at, char* s, size_t len) {
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
    cfg.rows[at].hl = 0;
    editor_row_make_render(&cfg.rows[at]);

    cfg.numrows++;
    cfg.dirty++;
}

void editor_insert_new_line() {
    if (cfg.cx == 0) editor_insert_row(cfg.cy, "", 0);
    else {
        EditorRow* cur_row = &cfg.rows[cfg.cy];
        editor_insert_row(cfg.cy + 1, &cur_row->chars[cfg.cx], cur_row->size - cfg.cx);
        
        cur_row = &cfg.rows[cfg.cy];
        cur_row->size = cfg.cx;
        cur_row->chars[cur_row->size] = '\0';
        editor_row_make_render(cur_row);
    }
    cfg.cy++;
    cfg.cx = 0;
}

void editor_del_row(size_t at) {
    if (at >= cfg.numrows) return;
    editor_row_free(&cfg.rows[at]);
    memmove(&cfg.rows[at], &cfg.rows[at + 1], sizeof(EditorRow) * (cfg.numrows - at - 1));
    cfg.numrows--;
    cfg.dirty++;
}

void editor_insert_char(int c) {
    if ((size_t)cfg.cy == cfg.numrows) editor_insert_row(cfg.numrows, "", 0);
    editor_row_insert_char(&cfg.rows[cfg.cy], cfg.cx, c);
    cfg.cx++;
}

void editor_del_char() {
    if ((size_t)cfg.cy == cfg.numrows) return;
    if (cfg.cx == 0 && cfg.cy == 0) return;

    EditorRow* cur_row = &cfg.rows[cfg.cy];
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

void editor_search() {
    int prev_cx = cfg.cx;
    int prev_cy = cfg.cy;
    int prev_row_offset = cfg.row_offset;

    char* query = editor_prompt("Search: %s", editor_search_callback);
    
    if (query) free(query);
    else {
        cfg.cx = prev_cx;
        cfg.cy = prev_cy;
        cfg.row_offset = prev_row_offset;
    }
}

void editor_search_callback(char* query, int key) {
    static int last_match = -1;
    static int direction = 1;

    static int saved_hl_line;
    static char* saved_hl = NULL;
    if (saved_hl) {
        memcpy(cfg.rows[saved_hl_line].hl, saved_hl, cfg.rows[saved_hl_line].rsize);
        free(saved_hl);
        saved_hl = NULL;
    }

    switch (key)
    {
    case '\r':
    case '\x1b':
        last_match = -1;
        direction = 1;
        return;
    case ARROW_RIGHT:
    case ARROW_DOWN:
        direction = 1;
        break;
    case ARROW_LEFT:
    case ARROW_UP:
        direction = -1;
        break;
    default:
        last_match = -1;
        direction = 1;
        break;
    }
    
    int cur = last_match;
    for (size_t i = 0; i < cfg.numrows; i++) {
        cur += direction;
        if (cur == -1) cur = cfg.numrows - 1;
        else if ((size_t)cur == cfg.numrows) cur = 0;
        
        EditorRow* cur_row = &cfg.rows[cur];
        char* match = strstr(cur_row->render, query);

        if (match) {
            last_match = cur;
            cfg.cy = cur;
            cfg.cx = editor_row_rcx_to_cx(cur_row, match - cur_row->render);
            cfg.row_offset = cfg.numrows;

            saved_hl_line = cur;
            saved_hl = malloc(cur_row->rsize);
            memcpy(saved_hl, cur_row->hl, cur_row->rsize);
            memset(&cur_row->hl[match - cur_row->render], HL_MATCH, strlen(query));
            break;
        }
    }
}

int editor_highlight_to_color(int hl) {
    switch (hl)
    {
    case HL_NUMBER: return 31;
    case HL_MATCH: return 34;
    default: return 37;
    }
}