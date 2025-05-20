/* LIBRARI */ 
#include <ctype.h>  
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* DEFINIRI */

#define CTRL_KEY(k) ((k) & 0x1f) // imita ctrl-(k)

/* DATA */

struct editorConfig
{
    int screenrows;
    int screencols;
    struct termios orig_termios;
};

struct editorConfig E;


/* TERMINAL */

// afisare eroare si terminare program
void die(const char *s) 
{
    // stergere ecran
    write(STDOUT_FILENO, "\x1b[2J", 4);
    write(STDOUT_FILENO, "\x1b[H", 3);

    perror(s);
    exit(1);
}

void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() 
{
    // salveaza si inregistreaza aplicarea la terminare programului setarile default terminal
    if(tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // opreste CTRL - M (carriage) | CTRL - S/Q | restul de siguranta, legacy
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);

    raw.c_oflag &= ~(OPOST); // opreste "\n" in "\r\n"
    
    // opreste afisare characterelor in terminal | canonical mode | CTRL - V | CTRL - C/Z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 

    // timp de asteptare pentru input, character control
    raw.c_cc[VMIN] = 0; // minimul de bytes cititi pentru read() sa returneze
    raw.c_cc[VTIME] = 1; // ./10 secunde

    //aplicare setari, flash la unread characters
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw))
        die("tcsetattr");
}

// returneaza fiecare bit / charater citit
char editorReadKey() 
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN) 
            die("read");
    }

    return c;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) // Terminal IOCtl (input/output control) Get WINdow SiZe
    {
        return -1;
    }

    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/* IESIRE */

// pentru ~ la inceputul liniilor 
void editorDrawRows()
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        write(STDOUT_FILENO, "~\r\n", 3);
    }
}

void editorRefreshScreen()
{
    write(STDOUT_FILENO, "\x1b[2J", 4); // <esc>[ - Escape (x1b = 0x1B = 27), 2 - tot ecranul, J - clear screen
    write(STDOUT_FILENO, "\x1b[H", 3); // pozitioneaza mouse-ul la inceput

    editorDrawRows();
    write(STDOUT_FILENO, "\x1b[H", 3);
}

/* INTRARE */

// proceseaza characterul
void editorProcessKeypress()
{
  char c = editorReadKey();

  switch (c)
  {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;
  }
}

/* INITIALIZARE */

// preia dimensiunile ecranului
void initEditor()
{
    if (getWindowSize(&E.screenrows, &E.screencols) == -1) die("getWindowSize");
}

int main()
{
    enableRawMode();
    initEditor();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 1;
}