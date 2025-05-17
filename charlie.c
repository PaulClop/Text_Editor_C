#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

struct termios orig_termios;

void disableRawMode()
{
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios);
}

void enableRawMode() 
{
    // salveaza si inregistreaza aplicarea la terminare programului setarile default terminal
    tcgetattr(STDIN_FILENO, &orig_termios);
    atexit(disableRawMode);

    // opreste afisare characterelor in terminal, flash la unread characters
    struct termios raw = orig_termios;
    raw.c_lflag &= ~(ECHO);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

int main()
{
    enableRawMode();

    // Citeste fiecare bit / charater
    char c;
    while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q');

    return 1;
}