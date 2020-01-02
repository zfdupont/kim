
/*** includes ***/ 
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#include <cstdlib>
#include <cctype>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <cstdarg>

#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <chrono>
#include <sstream>
#include <iterator>

/*** defines ***/ 
#define KORE_VERSION "0.1"

#define TAB_SIZE 4

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
#define BACKSPACE 127

void editor_set_status(const char*, ...);
std::string editor_prompt(std::string);

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
    int cx, cy, rx;
    int row_off;
    int col_off;
    int screen_rows;
    int screen_cols;
    int max_rows;
    bool edited;
    std::vector<ERow> row;
    std::string file_name;
    std::string status_msg;
    std::chrono::steady_clock::time_point status_time;
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
    
    //arrow key sends '\x1b'+'['+{A|B|      3|D} as input
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

int editor_cx_to_rx(ERow row, int cx){
    //int rx = cx + std::count(row.str.begin(), row.str.end(), '\t')*((TAB_SIZE-1)-(rx%TAB_SIZE));
    int rx = 0;
    for(int i = 0; i < cx; i++){
        if(row.str[i] == '\t')
            rx += (TAB_SIZE-1)-(rx%TAB_SIZE);
        rx++;
    }
    return rx;
}
int editor_rx_to_cx(ERow row, int rx){
    //int rx = cx + std::count(row.str.begin(), row.str.end(), '\t')*((TAB_SIZE-1)-(rx%TAB_SIZE));
    int temp = 0;
    int i = 0;
    for(; i < row.size, temp <= rx; i++){
        if(row.str[i] == '\t')
            temp += (TAB_SIZE-1)-(temp%TAB_SIZE);
        temp++;
    }
    return i;
}

void editor_update_row(ERow *row){
    row->render = std::string(row->str);
    for (auto pos = row->render.find('\t'); pos != std::string::npos;){
        row->render.replace(pos, std::string::npos, TAB_SIZE, ' ');
        pos = row->render.find('\t', pos+TAB_SIZE);
    }
    //row->render.replace(row->render.begin(), row->render.end(), TAB_SIZE, ' ');
    row->rsize = row->render.size();
}

void editor_insert_row(int at, std::string str){
    if(at < 0 || at > E.max_rows)
        return;
    E.row.resize(++E.max_rows);
    E.row.insert(E.row.begin()+at, (ERow){(int)str.size(), 0, std::string(str), ""});
    editor_update_row(&E.row[at]);
    E.edited = true;
}

void editor_append_row(std::string str){
    editor_insert_row(E.max_rows, str);
}

void editor_row_append_string(ERow *row, std::string s){
    row->str.append(s);
    row->size = row->str.size();
    editor_update_row(row);
    E.edited = true;
}

void editor_row_insert_char(ERow *row, int at, char c){
    if(at < 0 || at > row->size)
        at = row->size;
    row->str.insert(at, 1, c);
    row->size++;
    editor_update_row(row);
    E.edited = true;
}

void editor_row_delete_char(ERow *row, int at){
    if(at < 0 || at >= row->size)
        return;
    row->str.erase(at, 1);
    row->size--;
    editor_update_row(row);
    E.edited = true;
}

void editor_delete_row(int at){
    if(at < 0 || at >= E.max_rows)
        return;
    E.row.erase(E.row.begin()+at);
    E.max_rows--;
}
#pragma endregion ROW_OPERATIONS


#pragma region EDITOR_OPERATIONS
void editor_insert_char(int c){
    if(E.cy == E.max_rows)
        editor_append_row("");
    editor_row_insert_char(&E.row[E.cy], E.cx++, c);
}

void editor_delete_char(){
    if(E.cy == E.max_rows || E.cx == 0 && E.cy == 0)
        return;
    if(E.cx > 0){
        editor_row_delete_char(&E.row[E.cy], --E.cx);
    } else {
        E.cx = E.row[E.cy-1].size;
        editor_row_append_string(&E.row[E.cy-1], E.row[E.cy].str);
        editor_delete_row(E.cy--);
    }
}

void editor_insert_newline(){
    if(E.cx == 0){
        editor_insert_row(E.cy, "");
    } else {
        editor_insert_row(E.cy+1, E.row[E.cy].str.substr(E.cx));
        E.row[E.cy].str.erase(E.cx);
        E.row[E.cy].size = E.cx;
        editor_update_row(&E.row[E.cy]);
    }
    E.cx = 0;
    E.cy++;
}
#pragma endregion EDITOR_OPERATIONS


#pragma region FILEIO

std::string editor_rows_to_string(){
    std::stringstream ss;
    for(auto row : E.row){
        ss << row.str << "\n";
    }
    return ss.str();
}

void editor_open(char *file_name){
    E.file_name = std::string(file_name);

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
    E.edited = false;
}

void editor_save(){ //TODO: make saving more secure
    if(E.file_name.size() == 0)
        if((E.file_name = editor_prompt("Save as: %s")).size() == 0){
            editor_set_status("Save exited");    
            return;
        }
    std::string buff = editor_rows_to_string();
    std::ofstream out_file(E.file_name, std::ofstream::out);
    if(!out_file.is_open())
        editor_set_status("Can't save: %s", strerror(errno));
    out_file << buff;
    out_file.close();
    editor_set_status("%d bytes saved to disk.", buff.length());
    E.edited = false;
}

#pragma endregion FILEIO

#pragma region SPECIAL_FUNCTIONS

bool match_insensitive(char a, char b){
    return (tolower(a) == tolower(b));
}

void editor_find(){
    std::string target = editor_prompt("Find: %s (ESC to cancel)");
    if(target.size() == 0)
        return;
    std::string::iterator f;
    for(int i = 0; i < E.max_rows; i++){
        if((f = std::search(E.row[i].render.begin(), E.row[i].render.end(), target.begin(), target.end(), match_insensitive)) != E.row[i].render.end()){
            E.cx = editor_rx_to_cx(E.row[i], f - E.row[i].render.begin()); 
            E.cy = i;
            E.row_off = E.max_rows;
            return;
        }
    }
    editor_set_status("%s not found.", target.c_str());
}
#pragma endregion SPECIAL_FUNCTIONS

#pragma region OUTPUT
/*** output ***/

void editor_scroll(){
    E.rx = 0;
    if(E.cy < E.max_rows){
        E.rx = editor_cx_to_rx(E.row[E.cy], E.cx);
    }
    if(E.cy < E.row_off){
        E.row_off = E.cy;
    }
    if(E.cy >= E.row_off + E.screen_rows){
        E.row_off = E.cy - E.screen_rows + 1;
    }
    if(E.rx < E.col_off){
        E.col_off = E.rx;
    }
    if(E.rx >= E.col_off + E.screen_cols){
        E.col_off = E.rx - E.screen_cols + 1;
    }
}

void editor_draw_rows(std::string *buff){
    for(int y = 0; y < E.screen_rows-1; y++){
        int file_row = y + E.row_off; //account for row_offset
        if(file_row >= E.max_rows){ // check if row is part of the text buffer
            if (E.max_rows == 0 && y == E.screen_rows / 3) { // print welcome message 1/3 down the screen
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
            buff->append(E.row[file_row].render.substr(E.col_off));
        }
        buff->append("\x1b[K", 3); //erase line to right of cursor
        buff->append("\r\n", 2);
    }
}

void editor_draw_status(std::string *buff){
    buff->append("\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s%s - %d lines", (E.file_name.size() > 0 ? E.file_name.c_str() : "[No Name]"), (E.edited ? "(*)" : ""),  E.max_rows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", E.cy+1, E.max_rows);
    if(len > E.screen_cols)
        len = E.screen_cols;
    buff->append(status, len);
    for(int i = len; i < E.screen_cols; i++){
        if(E.screen_cols - i == rlen){
            buff->append(rstatus, rlen);
            break;
        } else{
            buff->append(" ", 1);
            
        }
        
    }
    buff->append("\x1b[m", 3);
    buff->append("\r\n", 2);
}

void editor_draw_msg(std::string *buff){
    buff->append("\x1b[K", 3);
    int msglen = E.status_msg.length();
    if(msglen > E.screen_cols)
        msglen = E.screen_cols;
    auto t = std::chrono::duration_cast<std::chrono::duration<int>>(std::chrono::steady_clock::now() - E.status_time);
    if(msglen && t.count() < 5)
        buff->append(E.status_msg);
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
    editor_draw_status(&buff);
    editor_draw_msg(&buff);

    char tempbuff[32];
    snprintf(tempbuff, sizeof(tempbuff), "\x1b[%d;%dH", (E.cy-E.row_off)+1, (E.rx-E.col_off)+1);
    buff.append(tempbuff);

    buff.append("\x1b[?25h", 6);

    char *cbuff = new char[buff.length()+1];
    strcpy(cbuff, buff.c_str());

    write(STDOUT_FILENO, cbuff, buff.length()); 
}

void editor_set_status(const char *fmt, ...){
    char msg[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    E.status_msg = std::string(msg);
    E.status_time = std::chrono::steady_clock::now();
}

#pragma endregion OUTPUT


#pragma region INPUT
/*** input ***/ 

std::string editor_prompt(std::string prompt){
    std::string res;
    while(true){
        editor_set_status(prompt.c_str(), res.c_str());
        editor_refresh_screen();
        int c = editor_read_key();
        if(c == '\r'){
            if(res.length() > 0){
                editor_set_status("");
                return res;
            }
        } else if(c == BACKSPACE){
            res.pop_back();
        } else if(c == '\x1b'){
            editor_set_status("");
            return "";
        }else if(!iscntrl(c) && c < 128){
            res.push_back(c);
        } 
    }
}

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
            if(E.cy < E.max_rows)
                E.cy++;
            break;
        case ARROW_RIGHT:
            if(E.cy < E.max_rows && E.cx < E.row[E.cy].size)
                E.cx++;
            else if(E.cy < E.max_rows && E.cx == E.row[E.cy].size)
                E.cy++, E.cx = 0;
            break;
    }
    //snap to end of next line
    int len = (E.cy < E.max_rows) ? E.row[E.cy].size : 0;
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
        case CTRL_KEY('s'):
            editor_save();
        case CTRL_KEY('f'):
            editor_find();
        case ARROW_UP:
        case ARROW_DOWN:
        case ARROW_LEFT:
        case ARROW_RIGHT:
            editor_move_cursor(c);
            break;
        case PAGE_UP:
        case PAGE_DOWN:
            {
                if(c == PAGE_UP){
                    E.cy = E.row_off;
                } else if(c == PAGE_DOWN){
                    E.cy = E.row_off + E.screen_cols - 1;
                    if(E.cy > E.max_rows)
                        E.cy = E.max_rows;
                }
                int x = E.screen_rows;
                while(x--)
                    editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case HOME_KEY:
            E.cx = 0;
            break;
        case END_KEY:
            if(E.cy < E.max_rows)
                E.cx = E.row[E.cy].size;
            break;
        case BACKSPACE:
            editor_delete_char();
            break;
        case DELETE_KEY:
            editor_move_cursor(ARROW_RIGHT);
            editor_delete_char();
            break;
        case '\r': // ENTER KEY
            editor_insert_newline();
            break;
        case CTRL_KEY('l'): //useless
        case CTRL_KEY('h'):
        case '\x1b':
            break;
        default:
            editor_insert_char(c);
            break;
    }
}
#pragma endregion INPUT


#pragma region INIT
/*** init ***/ 
void init_editor(){
    E.cx = 0;
    E.cy = 0;
    E.rx = 0;
    E.row_off = 0;
    E.col_off = 0;
    E.max_rows = 0;
    E.row = std::vector<ERow>();
    E.file_name = "";
    E.edited = false;
    if(get_window_size(&E.screen_rows, &E.screen_cols) == -1)
        die("get_window_size");
    E.screen_rows -= 2;
}

int main(int argc, char *argv[]){
    enable_raw_mode();
    init_editor();
    if(argc >= 2){
        editor_open(argv[1]);
    }
    
    editor_set_status("HELP: CTRL-Q to quit");

    while(true){
        editor_refresh_screen();
        editor_process_keypress();
    }
    return 0;
}
#pragma endregion INIT