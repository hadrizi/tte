#ifndef EDITOR_H
#define EDITOR_H

#include <termios.h>
#include <time.h>

#include "utils.h"

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

typedef enum {
    HL_NORMAL = 0,
    HL_NUMBER,
    HL_MATCH
} EditorHighlight;
 
typedef struct {
    size_t size;
    size_t rsize;
    char* chars;
    char* render;
    unsigned char* hl;
} EditorRow;

typedef struct {
    int cx, cy;
    int rcx; // render cursor
    unsigned int row_offset;
    size_t screenrows, screencols;
    size_t numrows;
    EditorRow* rows;
    struct termios orig_ts;
    char* filename;
    char statusmsg[80];
    time_t statusmsg_time;
    int dirty;
} EditorConfig;

size_t editor_row_cx_to_rcx(EditorRow*, size_t);
size_t editor_row_rcx_to_cx(EditorRow*, size_t);
void editor_row_make_render(EditorRow*);
void editor_row_append_string(EditorRow*, char*);
void editor_row_insert_char(EditorRow*, size_t, int);
void editor_row_del_char(EditorRow*, size_t);
void editor_update_highlight(EditorRow*);
void editor_row_free(EditorRow*);

void exit_raw_mode();
void enter_raw_mode();
void get_cursor_pos(size_t* rows, size_t* cols);
void get_win_size(size_t* rows, size_t* cols);

void editor_init();
void editor_free();
void editor_refresh_screen();
void editor_process_keypress();
void editor_save();
void editor_open(char*);
char* editor_get_rows_as_str(size_t*);

void editor_draw_rows(AppendBuf*);
void editor_draw_status_bar(AppendBuf*);
void editor_draw_message_bar(AppendBuf*);

void editor_set_status_message(const char*, ...);
char* editor_prompt(char*, void (*callback)(char* , int));

void editor_scroll();
void editor_move_cursor(int);

int editor_read_key();

void editor_insert_row(size_t at, char* s, size_t);
void editor_insert_new_line();
void editor_del_row(size_t);
void editor_insert_char(int);
void editor_del_char();

void editor_search();
void editor_search_callback(char*, int);

int editor_highlight_to_color(int);

#endif /* EDITOR_H */
