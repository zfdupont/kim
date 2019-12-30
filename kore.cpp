
/*** includes ***/ 
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstring>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>

/*** defines ***/ 
#define KORE_VERSION "0.1"
#define CTRL_KEY(k) ((k) & 0x1f)

#define ARROW_LEFT 1000
#define ARROW_RIGHT 1001
#define ARROW_UP 1002
#define ARROW_DOWN 1003
#define HOME_KEY 1004
#define PAGE_UP 1005
#define PAGE_DOWN 1006
#define END_KEY 1007
#define DELETE_KEY 1008

#pragma region DATA
/*** data ***/
struct ERow {
    int size;
    int rsize;
    std::string str;
    std::string render;
};

struct EditorConfig
{
    int cx, cy;
    int row_off;
    int col_off;
    int screen_rows;
    int screen_cols;
    int num_rows;
    std::vector<ERow> row;
    termios og_termios;
};

EditorConfig E;
#pragma endregion DATA


#pragma region TERMINAL
/*** terminal ***/ 

//exit function
void die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s); // set errno to indicate what happened
    exit(1); // exit
}

//enable echoing
void disable_raw_mode(){
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.og_termios) == -1)
        die("tcsetarr");
}

//disable echoing
void enable_raw_mode(){
    if(tcgetattr(STDIN_FILENO, &E.og_termios) == -1)
        die("tcsetarr");
    atexit(disable_raw_mode); // trigger disable_raw_mode() on exit
    
    termios raw = E.og_termios;
    
    raw.c_cflag |= CS8; // character size to 8 bits 
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | ISTRIP | INPCK); // ctrl-m, ctrl-s+q
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); //echo, canonical mode, ctrl-v+o, ctrl-c+z 
    raw.c_oflag &= ~(OPOST); //output processing

    raw.c_cc[VMIN] = 0; //min bytes for read() to return
    raw.c_cc[VTIME] = 1; // 0.1 second wait time before read() returns

    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) == -1)
        die("tcsetarr");
}

int editor_read_key(){
    int nread;
    char c;
    while((nread = read(STDIN_FILENO, &c, 1)) != 1)
        if(nread == -1 && errno != EAGAIN)
            die("read");
    
    //arrow key sends '\x1b'+'['+{A|B|C|D} as input
    //page up/down sends '\x1b'+'['+{5|6} + '~'
    if(c == '\x1b'){
        char seq[3];

        if(read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if(read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';
        
        if(seq[0] == '['){
            if(seq[1] >= '0' && seq[1] <= '9'){
                if(read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if(seq[2] == '~'){
                    switch(seq[1]){
                        case '3':
                            return DELETE_KEY;
                        case '4':
                            return END_KEY;
                        case '5':
                            return PAGE_UP;
                        case '6':
                            return PAGE_DOWN;
                    }
                }
            } else {
                switch(seq[1]){
                    case 'A':
                        return ARROW_UP;
                    case 'B':
                        return ARROW_DOWN;
                    case 'C':
                        return ARROW_RIGHT;
                    case 'D':
                        return ARROW_LEFT;
                    case 'H':
                        return HOME_KEY;
                    case 'F':
                        return END_KEY;
                }
            }   
        }
        return '\x1b';
    } else {
        return c;
    }
}

int get_window_size(int *rows, int *cols){
    winsize ws;
    //TODO: fallback for no ioctl support
    if(ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0){
        return -1;
    } else {
        *cols = ws.ws_col;
        *rows = ws.ws_row;    
    }
    return 0;
}
#pragma endregion TERMINAL


#pragma region ROW_OPERATIONS

void editor_update_row(ERow row){
    row.render = std::string(row.str);
    row.rsize = row.render.size();
}


void editor_append_row(std::string str){
    E.row.resize(E.num_rows+1);
    int index = E.num_rows;
    E.row[index].str = std::string(str);
    E.row[index].size = str.size();
    
    editor_update_row(E.row[index]);
    E.num_rows++;
}
#pragma endregion ROW_OPERATIONS


#pragma region FILEIO

void editor_open(char *file_name){
    std::ifstream in_file(file_name);
    if(!in_file)
        die("std::ifstream::open");
    
    std::string line;
    while( std::getline(in_file, line) ){
        int line_len = line.length();
        while(line.length() > 0 && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        editor_append_row(line);
    } 
    

    in_file.close();
}

#pragma endregion FILEIO


#pragma region OUTPUT
/*** output ***/

void editor_scroll(){
    if(E.cy < E.row_off){
        E.row_off = E.cy;
    }
    if(E.cy >= E.row_off + E.screen_rows){
        E.row_off = E.cy - E.screen_rows + 1;
    }
    if(E.cx < E.col_off){
        E.col_off = E.cx;
    }
    if(E.cx >= E.col_off + E.screen_cols){
        E.col_off = E.cx - E.screen_cols + 1;
    }
}

void editor_draw_rows(std::string *buff){
    for(int y = 0; y < E.screen_rows-1; y++){
        int file_row = y + E.row_off; //account for row_offset
        if(file_row >= E.num_rows){ // check if row is part of the text buffer
            if (E.num_rows == 0 && y == E.screen_rows / 3) { // print welcome message 1/3 down the screen
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kore editor -- ver %s", KORE_VERSION);
                if(welcomelen > E.screen_cols){
                    welcomelen = E.screen_cols;
                }
                //center welcome
                int pad = (E.screen_cols - welcomelen)/2;
                if(pad){
                    buff->append(">", 1);
                    pad--;
                }
                while(pad--){
                    buff->append(" ", 1);
                }    
                buff->append(welcome, welcomelen);
            } else {
                buff->append(">", 1);
            }
        } else {
            buff->append(E.row[file_row].str.substr(E.col_off));
        }
        buff->append("\x1b[K", 3); //erase line to right of cursor
        if(y < E.screen_rows-1){
            buff->append("\r\n", 2);
        }
    }
}

void editor_refresh_screen(){
    //J - erase in display
    //2 - clear whole terminal
    // \x1b is escape character
    //H - realign cursor
    
    editor_scroll();

    std::string buff;

    buff.append("\x1b[?25l", 6); // hide cursor
    buff.append("\x1b[H", 3);

    editor_draw_rows(&buff);

    char tempbuff[32];
    snprintf(tempbuff, sizeof(tempbuff), "\x1b[%d;%dH", (E.cy-E.row_off)+1, (E.cx-E.col_off)+1);
    buff.append(tempbuff);

    buff.append("\x1b[?25h", 6);

    char *cbuff = new char[buff.length()+1];
    strcpy(cbuff, buff.c_str());

    write(STDOUT_FILENO, cbuff, buff.length()); 
}
#pragma endregion OUTPUT


#pragma region INPUT
/*** input ***/ 

void editor_move_cursor(int key){
    switch (key)
    {
        case ARROW_UP:
            if(E.cy != 0)
                E.cy--;
            break;
        case ARROW_LEFT:
            if(E.cx != 0)
                E.cx--;
            else if(E.cy > 0)
                E.cx = E.row[--E.cy].size;
            break;
        case ARROW_DOWN:
            if(E.cy != E.num_rows - 1)
                E.cy++;
            break;
        case ARROW_RIGHT:
            if(E.cy < E.num_rows && E.cx < E.row[E.cy].size)
                E.cx++;
            else if(E.cy < E.num_rows && E.cx == E.row[E.cy].size)
                E.cy++, E.cx = 0;
            break;
    }
    //snap to end of next line
    int len = (E.cy < E.num_rows) ? E.row[E.cy].size : 0;
    if(E.cx > len){
        E.cx = len;
    }
}

void editor_process_keypress(){
    int c = editor_read_key();
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            exit(0);
            break;
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                int x = E.screen_rows;
                while(x--)
                    editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            E.cx = E.screen_cols - 1;
            break;
    }
}
#pragma endregion INPUT


#pragma region INIT
/*** init ***/ 
void init_editor(){
    E.cx = 0;
    E.cy = 0;
    E.row_off = 0;
    E.col_off = 0;
    E.num_rows = 0;
    E.row = std::vector<ERow>();
    if(get_window_size(&E.screen_rows, &E.screen_cols) == -1)
        die("get_window_size");
    
}

int main(int argc, char *argv[]){
    enable_raw_mode();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }

    while(true){
        editor_refresh_screen();
        editor_process_keypress();
    }
    return 0;
}
#pragma endregion INIT