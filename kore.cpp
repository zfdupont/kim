#include "kore.h"

Kore::Kore(){
    cx = 0;
    cy = 0;
    rx = 0;
    row_off = 0;
    col_off = 0;
    max_rows = 0;
    row = std::vector<ERow>();
    file_name = "";
    running = true;
    edited = false;
    if(get_window_size(&screen_rows, &screen_cols) == -1)
        die("get_window_size");
    screen_rows -= 2;
    editor_set_status("HELP: CTRL-Q to quit");
}
Kore::Kore(char *file) : Kore(){
    editor_open(file);
}

/* struct EditorConfig
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

EditorConfig E; */
#pragma endregion DATA


#pragma region TERMINAL
/*** terminal ***/ 

bool Kore::dead(){
    return running;
}


//exit function
void Kore::die(const char *s){
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);
    perror(s); // set errno to indicate what happened
    this->running = false; // exit
}



int Kore::editor_read_key(){
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
    }
    return c;
}

int Kore::get_window_size(int *rows, int *cols){
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

int Kore::editor_cx_to_rx(ERow row, int cx){
    //int rx = cx + std::count(row.str.begin(), row.str.end(), '\t')*((TAB_SIZE-1)-(rx%TAB_SIZE));
    int rx = 0;
    for(int i = 0; i < cx; i++){
        if(row.str[i] == '\t')
            rx += (TAB_SIZE-1)-(rx%TAB_SIZE);
        rx++;
    }
    return rx;
}
int Kore::editor_rx_to_cx(ERow row, int rx){
    //int rx = cx + std::count(row.str.begin(), row.str.end(), '\t')*((TAB_SIZE-1)-(rx%TAB_SIZE));
    int i = 0;
    for(int temp = 0; i < row.size && temp <= rx; i++){
        if(row.str[i] == '\t')
            temp += (TAB_SIZE-1)-(temp%TAB_SIZE);
        temp++;
    }
    return i;
}

void Kore::editor_update_row(ERow *row){
    row->render = std::string(row->str);
    for (auto pos = row->render.find('\t'); pos != std::string::npos;){
        row->render.replace(pos, std::string::npos, TAB_SIZE, ' ');
        pos = row->render.find('\t', pos+TAB_SIZE);
    }
    //row->render.replace(row->render.begin(), row->render.end(), TAB_SIZE, ' ');
    row->rsize = row->render.size();
}

void Kore::editor_insert_row(int at, std::string str){
    if(at < 0 || at > this->max_rows)
        return;
    this->row.resize(++this->max_rows);
    this->row.insert(this->row.begin()+at, (ERow){(int)str.size(), 0, std::string(str), ""});
    editor_update_row(&this->row[at]);
    this->edited = true;
}

void Kore::editor_append_row(std::string str){
    editor_insert_row(this->max_rows, str);
}

void Kore::editor_row_append_string(ERow *row, std::string s){
    row->str.append(s);
    row->size = row->str.size();
    editor_update_row(row);
    this->edited = true;
}

void Kore::editor_row_insert_char(ERow *row, int at, char c){
    if(at < 0 || at > row->size)
        at = row->size;
    row->str.insert(at, 1, c);
    row->size++;
    editor_update_row(row);
    this->edited = true;
}

void Kore::editor_row_delete_char(ERow *row, int at){
    if(at < 0 || at >= row->size)
        return;
    row->str.erase(at, 1);
    row->size--;
    editor_update_row(row);
    this->edited = true;
}

void Kore::editor_delete_row(int at){
    if(at < 0 || at >= this->max_rows)
        return;
    this->row.erase(this->row.begin()+at);
    this->max_rows--;
}
#pragma endregion ROW_OPERATIONS


#pragma region EDITOR_OPERATIONS
void Kore::editor_insert_char(int c){
    if(this->cy == this->max_rows)
        editor_append_row("");
    editor_row_insert_char(&this->row[this->cy], this->cx++, c);
}

void Kore::editor_delete_char(){
    if(this->cy == this->max_rows || this->cx == 0 && this->cy == 0)
        return;
    if(this->cx > 0){
        editor_row_delete_char(&this->row[this->cy], --this->cx);
    } else {
        this->cx = this->row[this->cy-1].size;
        editor_row_append_string(&this->row[this->cy-1], this->row[this->cy].str);
        editor_delete_row(this->cy--);
    }
}

void Kore::editor_insert_newline(){
    if(this->cx == 0){
        editor_insert_row(this->cy, "");
    } else {
        editor_insert_row(this->cy+1, this->row[this->cy].str.substr(this->cx));
        this->row[this->cy].str.erase(this->cx);
        this->row[this->cy].size = this->cx;
        editor_update_row(&this->row[this->cy]);
    }
    this->cx = 0;
    this->cy++;
}
#pragma endregion EDITOR_OPERATIONS


#pragma region FILEIO

std::string Kore::editor_rows_to_string(){
    std::stringstream ss;
    for(auto row : this->row){
        ss << row.str << "\n";
    }
    return ss.str();
}

void Kore::editor_open(char *file_name){
    this->file_name = std::string(file_name);

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
    this->edited = false;
}

void Kore::editor_save(){ //TODO: make saving more secure
    if(this->file_name.size() == 0)
        if((this->file_name = editor_prompt("Save as: %s")).size() == 0){
            editor_set_status("Save exited");    
            return;
        }
    std::string buff = editor_rows_to_string();
    std::ofstream out_file(this->file_name, std::ofstream::out);
    if(!out_file.is_open())
        editor_set_status("Can't save: %s", strerror(errno));
    out_file << buff;
    out_file.close();
    editor_set_status("%d bytes saved to disk.", buff.length());
    this->edited = false;
}

#pragma endregion FILEIO

#pragma region SPECIAL_FUNCTIONS

bool Kore::match_insensitive(char a, char b){
    return (tolower(a) == tolower(b));
}

void Kore::editor_find(){
    std::string target = editor_prompt("Find: %s (ESC to cancel)");
    if(target.size() == 0)
        return;
    std::string::iterator f;
    for(int i = 0; i < this->max_rows; i++){
        if((f = std::search(this->row[i].render.begin(), this->row[i].render.end(), target.begin(), target.end(), match_insensitive)) != this->row[i].render.end()){
            this->cx = editor_rx_to_cx(this->row[i], f - this->row[i].render.begin()); 
            this->cy = i;
            this->row_off = this->max_rows;
            return;
        }
    }
    editor_set_status("%s not found.", target.c_str());
}
#pragma endregion SPECIAL_FUNCTIONS

#pragma region OUTPUT
/*** output ***/

void Kore::editor_scroll(){
    this->rx = 0;
    if(this->cy < this->max_rows){
        this->rx = editor_cx_to_rx(this->row[this->cy], this->cx);
    }
    if(this->cy < this->row_off){
        this->row_off = this->cy;
    }
    if(this->cy >= this->row_off + this->screen_rows){
        this->row_off = this->cy - this->screen_rows + 1;
    }
    if(this->rx < this->col_off){
        this->col_off = this->rx;
    }
    if(this->rx >= this->col_off + this->screen_cols){
        this->col_off = this->rx - this->screen_cols + 1;
    }
}

void Kore::editor_draw_rows(std::string *buff){
    for(int y = 0; y < this->screen_rows-1; y++){
        int file_row = y + this->row_off; //account for row_offset
        if(file_row >= this->max_rows){ // check if row is part of the text buffer
            if (this->max_rows == 0 && y == this->screen_rows / 3) { // print welcome message 1/3 down the screen
                char welcome[80];
                int welcomelen = snprintf(welcome, sizeof(welcome), "Kore editor -- ver %s", KORE_VERSION);
                if(welcomelen > this->screen_cols){
                    welcomelen = this->screen_cols;
                }
                //center welcome
                int pad = (this->screen_cols - welcomelen)/2;
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
            buff->append(this->row[file_row].render.substr(this->col_off));
        }
        buff->append("\x1b[K", 3); //erase line to right of cursor
        buff->append("\r\n", 2);
    }
}

void Kore::editor_draw_status(std::string *buff){
    buff->append("\x1b[7m", 4);
    char status[80], rstatus[80];
    int len = snprintf(status, sizeof(status), "%.20s%s - %d lines", (this->file_name.size() > 0 ? this->file_name.c_str() : "[No Name]"), (this->edited ? "(*)" : ""),  this->max_rows);
    int rlen = snprintf(rstatus, sizeof(rstatus), "%d/%d", this->cy+1, this->max_rows);
    if(len > this->screen_cols)
        len = this->screen_cols;
    buff->append(status, len);
    for(int i = len; i < this->screen_cols; i++){
        if(this->screen_cols - i == rlen){
            buff->append(rstatus, rlen);
            break;
        } else{
            buff->append(" ", 1);
            
        }
        
    }
    buff->append("\x1b[m", 3);
    buff->append("\r\n", 2);
}

void Kore::editor_draw_msg(std::string *buff){
    buff->append("\x1b[K", 3);
    int msglen = this->status_msg.length();
    if(msglen > this->screen_cols)
        msglen = this->screen_cols;
    auto t = std::chrono::duration_cast<std::chrono::duration<int>>(std::chrono::steady_clock::now() - this->status_time);
    if(msglen && t.count() < 5)
        buff->append(this->status_msg);
}

void Kore::editor_refresh_screen(){
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
    snprintf(tempbuff, sizeof(tempbuff), "\x1b[%d;%dH", (this->cy-this->row_off)+1, (this->rx-this->col_off)+1);
    buff.append(tempbuff);

    buff.append("\x1b[?25h", 6);

    char *cbuff = new char[buff.length()+1];
    strcpy(cbuff, buff.c_str());

    write(STDOUT_FILENO, cbuff, buff.length()); 
}

void Kore::editor_set_status(const char *fmt, ...){
    char msg[80];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof(msg), fmt, ap);
    va_end(ap);
    this->status_msg = std::string(msg);
    this->status_time = std::chrono::steady_clock::now();
}

#pragma endregion OUTPUT


#pragma region INPUT
/*** input ***/ 

std::string Kore::editor_prompt(std::string prompt){
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

void Kore::editor_move_cursor(int key){
    switch (key)
    {
        case ARROW_UP:
            if(this->cy != 0)
                this->cy--;
            break;
        case ARROW_LEFT:
            if(this->cx != 0)
                this->cx--;
            else if(this->cy > 0)
                this->cx = this->row[--this->cy].size;
            break;
        case ARROW_DOWN:
            if(this->cy < this->max_rows)
                this->cy++;
            break;
        case ARROW_RIGHT:
            if(this->cy < this->max_rows && this->cx < this->row[this->cy].size)
                this->cx++;
            else if(this->cy < this->max_rows && this->cx == this->row[this->cy].size)
                this->cy++, this->cx = 0;
            break;
    }
    //snap to end of next line
    int len = (this->cy < this->max_rows) ? this->row[this->cy].size : 0;
    if(this->cx > len){
        this->cx = len;
    }
}

void Kore::editor_process_keypress(){
    int c = editor_read_key();
    switch(c){
        case CTRL_KEY('q'):
            write(STDOUT_FILENO, "\x1b[2J", 4);
            write(STDOUT_FILENO, "\x1b[H", 3);
            this->running = false;
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
                    this->cy = this->row_off;
                } else if(c == PAGE_DOWN){
                    this->cy = this->row_off + this->screen_cols - 1;
                    if(this->cy > this->max_rows)
                        this->cy = this->max_rows;
                }
                int x = this->screen_rows;
                while(x--)
                    editor_move_cursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
            }
            break;
        case HOME_KEY:
            this->cx = 0;
            break;
        case END_KEY:
            if(this->cy < this->max_rows)
                this->cx = this->row[this->cy].size;
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
/* void init_editor(){
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
} */


#pragma endregion INIT