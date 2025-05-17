#include <ctype.h>
#include <stdio.h>
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

    struct termios raw = orig_termios;

    // opreste CTRL - M (carriage) | CTRL - S/Q
    raw.c_iflag &= ~(ICRNL | IXON);
    
    // opreste afisare characterelor in terminal | canonical mode | CTRL - V | CTRL - C/Z
    raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 

    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw); //aplicare setari, flash la unread characters
}

int main()
{
    enableRawMode();

    // Citeste fiecare bit / charater
    char c;
    while(read(STDIN_FILENO, &c, 1) == 1 && c != 'q')
    {
        if (iscntrl(c)) // non-printable/control character
        {
            printf("%d\n", c);
        } 

        else 
        {
            printf("%d ('%c')\n", c, c);
        }
    }

    return 1;
}