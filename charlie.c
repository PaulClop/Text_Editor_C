/* LIBRARI */
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <termios.h>
#include <unistd.h>

/* DEFINIRI */

#define CTRL_KEY(k) ((k) & 0x1f) // imita ctrl-(k)
#define ABUF_INIT {NULL, 0}
#define Charlie_VERSION "0.1"

enum editorKey
{
    ARROW_LEFT = 1000,
    ARROW_RIGHT,
    ARROW_UP,
    ARROW_DOWN,
    DEL_KEY,
    HOME_KEY,
    END_KEY,
    PAGE_UP,
    PAGE_DOWN,
};

/* DATA */

typedef struct erow
{
    int size;
    char *chars;
} erow;

struct editorConfig
{
    int cx, cy;
    int screenrows;
    int screencols;
    int numrows;
    erow row;
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
    // salveaza si inregistreaza aplicarea la terminare programului setarile
    // default terminal
    if (tcgetattr(STDIN_FILENO, &E.orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = E.orig_termios;

    // opreste CTRL - M (carriage) | CTRL - S/Q | restul de siguranta, legacy
    raw.c_iflag &= ~(ICRNL | IXON | BRKINT | INPCK | ISTRIP);

    raw.c_oflag &= ~(OPOST); // opreste "\n" in "\r\n"

    // opreste afisare characterelor in terminal | canonical mode | CTRL - V |
    // CTRL - C/Z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);

    // timp de asteptare pentru input, character control
    raw.c_cc[VMIN] = 0;  // minimul de bytes cititi pentru read() sa returneze
    raw.c_cc[VTIME] = 1; // ./10 secunde

    // aplicare setari, flash la unread characters
    if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw))
        die("tcsetattr");
}

// returneaza fiecare bit / charater citit
int editorReadKey()
{
    int nread;
    char c;

    while ((nread = read(STDIN_FILENO, &c, 1)) != 1)
    {
        if (nread == -1 && errno != EAGAIN)
            die("read");
    }

    // sageti mapare ( alias )
    if (c == '\x1b')
    {
        char seq[3];
        if (read(STDIN_FILENO, &seq[0], 1) != 1)
            return '\x1b';
        if (read(STDIN_FILENO, &seq[1], 1) != 1)
            return '\x1b';

        if (seq[0] == '[')
        {
            if (seq[1] >= '0' && seq[1] <= '9')
            {
                if (read(STDIN_FILENO, &seq[2], 1) != 1)
                    return '\x1b';
                if (seq[2] == '~')
                {
                    switch (seq[1])
                    {
                    case '1': return HOME_KEY;
                    case '3': return DEL_KEY; // momentan nu face nimic
                    case '4': return END_KEY;
                    case '5': return PAGE_UP;
                    case '6': return PAGE_DOWN;
                    case '7': return HOME_KEY;
                    case '8': return END_KEY;
                    }
                }
            }

            else
            {
                switch (seq[1])
                {
                case 'A': return ARROW_UP;
                case 'B': return ARROW_DOWN;
                case 'C': return ARROW_RIGHT;
                case 'D': return ARROW_LEFT;
                case 'H': return HOME_KEY;
                case 'F': return END_KEY;
                }
            }
        }

        else if (seq[0] == 'O')
        {
            switch (seq[1])
            {
            case 'H': return HOME_KEY;
            case 'F': return END_KEY;
            }
        }

        return '\x1b';
    }

    else
    {
        return c;
    }
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
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == -1 ||
        ws.ws_col == 0) // Terminal IOCtl (input/output control) Get WINdow SiZe
    {
        // in cazul daca prima metoda nu functioneaza, cursor coltul dreapta jos si
        // aflam coordonatele
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

/*** fisier i/o ***/
void editorOpen()
{
    char *line = "Hello, world!";
    ssize_t linelen = 13;

    E.row.size = linelen;
    E.row.chars = malloc(linelen + 1);
    memcpy(E.row.chars, line, linelen);
    E.row.chars[linelen] = '\0';
    E.numrows = 1;
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
        if (y >= E.numrows)
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
        }

        else
        {
            int len = E.row.size;
            if (len > E.screencols) // afara din ecran, pierdem
                len = E.screencols;
            abAppend(ab, E.row.chars, len);
        }

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

    char buf[32];
    snprintf(buf, sizeof(buf), "\x1b[%d;%dH", E.cy + 1,
             E.cx + 1); // terminalul foloseste index 1
    abAppend(&ab, buf, strlen(buf));

    abAppend(&ab, "\x1b[?25h", 6); // afiseaza mouse-ul

    write(STDOUT_FILENO, ab.b, ab.len);
    abFree(&ab);
}

/* INTRARE */

// miscare cursor w, a, s, d
void editorMoveCursor(int key)
{
    switch (key)
    {
    case ARROW_LEFT:
        if (E.cx != 0) // in limita marginiilor
        {
            E.cx--;
        }
        break;

    case ARROW_RIGHT:
        if (E.cx != E.screencols - 1)
        {
            E.cx++;
        }
        break;

    case ARROW_UP:
        if (E.cy != 0)
        {
            E.cy--;
        }
        break;

    case ARROW_DOWN:
        if (E.cy != E.screenrows - 1)
        {
            E.cy++;
        }
        break;
    }
}

// proceseaza characterul
void editorProcessKeypress()
{
    int c = editorReadKey();

    switch (c)
    {
    case CTRL_KEY('q'):
        write(STDOUT_FILENO, "\x1b[2J", 4);
        write(STDOUT_FILENO, "\x1b[H", 3);
        exit(0);
        break;

    case HOME_KEY: E.cx = 0; break;
    case END_KEY: E.cx = E.screencols - 1; break;

    // ↑ sau ↓ nr. de randuri vizibile
    case PAGE_UP:
    case PAGE_DOWN: {
        int times = E.screenrows;
        while (times--)
            editorMoveCursor(c == PAGE_UP ? ARROW_UP : ARROW_DOWN);
    }
    break;

    case ARROW_UP:
    case ARROW_DOWN:
    case ARROW_LEFT:
    case ARROW_RIGHT: editorMoveCursor(c); break;
    }
}

/* INITIALIZARE */

// preia dimensiunile ecranului
void initEditor()
{
    E.cx = 0;
    E.cy = 0;
    E.numrows = 0;

    if (getWindowSize(&E.screenrows, &E.screencols) == -1)
        die("getWindowSize");
}

int main()
{
    enableRawMode();
    initEditor();
    editorOpen();

    while (1)
    {
        editorRefreshScreen();
        editorProcessKeypress();
    }

    return 1;
}