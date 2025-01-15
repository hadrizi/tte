#include "editor.h"

int main(int argc, char* argv[]) {
    enter_raw_mode();
    editor_init();

    if (argc >= 2) {
        editor_open(argv[1]);
    }

    editor_set_status_message("HELP: Ctrl-S = save | Ctrl-Q = quit | Ctrl-F = search");
    while (1) {
        editor_refresh_screen();
        editor_process_keypress();
    }

    editor_free();
    return 0;
}