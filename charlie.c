/* LIBRARI */ 
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <termios.h>
#include <unistd.h>

/* DATA */

struct termios orig_termios;

/* TERMINAL */

// afisare eroare si terminare program
void die(const char *s) 
{
  perror(s);
  exit(1);
}

void disableRawMode()
{
    if(tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) == -1)
        die("tcsetattr");
}

void enableRawMode() 
{
    // salveaza si inregistreaza aplicarea la terminare programului setarile default terminal
    if(tcgetattr(STDIN_FILENO, &orig_termios) == -1)
        die("tcgetattr");
    atexit(disableRawMode);

    struct termios raw = orig_termios;

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

/* INITIALIZARE */

int main()
{
    enableRawMode();

    // Citeste fiecare bit / charater
    while(1)
    {
        char c = '\0';
        if(read(STDIN_FILENO, &c, 1) == -1 && errno != EAGAIN) // fara eroare cand time out
            die("read");
    
        if (iscntrl(c)) // non-printable/control character
        {
            printf("%d\r\n", c);
        } 

        else 
        {
            printf("%d ('%c')\r\n", c, c);
        }

        if(c == 'q')
            break;
    }

    return 1;
}