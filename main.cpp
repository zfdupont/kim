#include "kore.h"
#include <list>

termios og_termios;

//enable echoing
void disable_raw_mode(Kore window){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &og_termios) == -1)
        window.die("tcsetarr");
}

//disable echoing
void enable_raw_mode(Kore window){
    if(tcgetattr(STDIN_FILENO, &og_termios) == -1)
        window.die("tcsetarr");

    termios raw = og_termios;
    
    raw.c_cflag |= CS8; // character size to 8 bits 
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | ISTRIP | INPCK); // ctrl-m, ctrl-s+q
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); //echo, canonical mode, ctrl-v+o, ctrl-c+z 
    raw.c_oflag &= ~(OPOST); //output processing

    raw.c_cc[VMIN] = 0; //min bytes for read() to return
    raw.c_cc[VTIME] = 1; // 0.1 second wait time before read() returns

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        window.die("tcsetarr");
}

int main(int argc, char *argv[]){
    Kore kore;
    if(argc >= 2)
        kore.editor_open(argv[1]);

    enable_raw_mode(kore);
    
    kore.editor_set_status("HELP: CTRL-Q to quit");;
    
    while(kore.dead()){
        kore.editor_refresh_screen();
        kore.editor_process_keypress();
    }

    disable_raw_mode(kore);
    return 0;
}
