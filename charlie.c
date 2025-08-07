/* LIBRARI */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

/* DEFINIRI */

#define CTRL_KEY(k) ((k) & 0x1f) // imita ctrl-(k)
#define ABUF_INIT {NULL, 0}
#define Charlie_VERSION "0.1"

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
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &E.orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode()
{
    // salveaza si inregistreaza aplicarea la terminare programului setarile default terminal
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // opreste CTRL - M (carriage) | CTRL - S/Q | restul de siguranta, legacy
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);

    raw.c_oflag &= ~(OPOST); // opreste "\n" in "\r\n"

    // opreste afisare characterelor in terminal | canonical mode | CTRL - V | CTRL - C/Z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // timp de asteptare pentru input, character control
    raw.c_cc[VMIN] = 0;  // minimul de bytes cititi pentru read() sa returneze
    raw.c_cc[VTIME] = 1; // ./10 secunde

    // aplicare setari, flash la unread characters
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw))
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

int getCursorPosition(int *rows, int *cols)
{
    char buf[32];
    unsigned int i = 0;

    if (write(STDOUT_FILENO, "\x1b[6n", 4) != 4) // primeste coordonatele cursor-ului sub forma <esc>linie;coloanaR
        return -1;

    while (i < sizeof(buf) - 1)
    {
        if (read(STDIN_FILENO, &buf[i], 1) != 1)
            break;
        if (buf[i] == 'R')
            break;
        ++i;
    }

    buf[i] = '\0';

    // verificare buffer + extragere
    if (buf[0] != '\x1b' || buf[1] != '[')
        return -1;
    if (sscanf(&buf[2], "%d;%d", rows, cols) != 2)
        return -1;

    return 0;
}

int getWindowSize(int *rows, int *cols)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 || ws.ws_col == 0) // Terminal IOCtl (input/output control) Get WINdow SiZe
    {
        // in cazul daca prima metoda nu functioneaza, cursor coltul dreapta jos si aflam coordonatele
        if (write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) != 12) // daca nicio metoda nu functioneaza
            return -1;
        return getCursorPosition(rows, cols);
    }

    else
    {
        *cols = ws.ws_col;
        *rows = ws.ws_row;
        return 0;
    }
}

/* append buffer */
struct abuf
{
    char *b;
    int len;
};

// alocare memorie si append
void abAppend(struct abuf *ab, const char *s, int len)
{
    char *new = realloc(ab->b, ab->len + len);
    if (new == NULL)
        return;

    memcpy(&new[ab->len], s, len);
    ab->b = new;
    ab->len += len;
}

// golire memorie abuf
void abFree(struct abuf *ab)
{
    free(ab->b);
}

/* IESIRE */

// pentru ~ la inceputul liniilor
void editorDrawRows(struct abuf *ab)
{
    int y;
    for (y = 0; y < E.screenrows; y++)
    {
        // afisare mesaj de bun-venit
        if (y == E.screenrows / 3)
        {
            char welcome[80];
            int welcomelen = snprintf(welcome, sizeof(welcome), "Charlie editor -- version %s", Charlie_VERSION);
            if (welcomelen > E.screencols)
                welcomelen = E.screencols;

            // centrare mesaj
            int padding = (E.screencols - welcomelen) / 2;
            if (padding)
            {
                abAppend(ab, "~", 1);
                padding--;
            }
            while (padding--)
                abAppend(ab, " ", 1);

            abAppend(ab, welcome, welcomelen);
        }
        else
            abAppend(ab, "~", 1);

        abAppend(ab, "\x1b[K", 3); // sterge pornind dupa cursor linia

        // verificare ultima linie
        if (y < E.screenrows - 1)
            abAppend(ab, "\r\n", 2);
    }
}

void editorRefreshScreen()
{
    struct abuf ab = ABUF_INIT;

    // <esc>[ - Escape (x1b = 0x1B = 27)
    abAppend(&ab, "\x1b[?25l", 6); // ascunde mouse-ul
    abAppend(&ab, "\x1b[H", 3);    // pozitioneaza mouse-ul la inceput

    editorDrawRows(&ab);
    abAppend(&ab, "\x1b[H", 3);
    abAppend(&ab, "\x1b[?25h", 6); // afiseaza mouse-ul

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
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
    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
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