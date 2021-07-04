#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <ctype.h>
#include <sys/wait.h>

#define prl {printf("\n");}
#define BUFFSIZE 1024

#define CTRL_KEY(k) ((k) & 0x1f)
/* Lorque qu'on appuie sur CTRL-(K), K une lettre
	, les bits 5 et 6 sont supprimer puis la lettre est 
	envoyé
	0x1f = 0001 1111 */
	
// sauvegarde termios //
struct termios original;

/*************TOOL************/

void clear()
{
	if(fork()==0)execlp("clear","clear",NULL);
	wait(NULL);
}

void error(char* message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

void print_file(int desc)
{
	char buffer[BUFFSIZE];
	ssize_t r;
	while((r=read(desc,buffer,BUFFSIZE))>0)
	{
		write(STDOUT_FILENO,buffer,r);
	}

}
/***********CONSOLE*********************/

void StopRaw()
{	
	// On remet les attributs du terminal par défault //
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&original);
}

void RawMode()
{
	tcgetattr(STDIN_FILENO,&original);
	atexit(StopRaw); /* On fait appel à stopRaw au moment
						de quitter */


	struct termios t = original; // Modification //
	t.c_lflag &= ~(ECHO | ICANON |ISIG);
	// ISIG : Désactive CTRL-C et CTRL-Z //
	// impression des touches désactivé (ECHO) //
	// Mode canonique désactivé //
	t.c_iflag &= ~(IXON);
	/*désactive CTRL-S et CTRL-Q */

	tcsetattr(STDIN_FILENO,TCSAFLUSH,&t);


}
/************ECRITURE******************/
int Treatment(char letter)
{
	if(letter == CTRL_KEY('q')){prl;exit(0);}
	// if((int)letter == 27){StopRaw();return 1;}
	return 0;

}

void Writting(int fd)
{	
	char letter;
	/* On lit caractère par caractère pour vérifier
		la valeur ascii du caractère entré */
	read(STDIN_FILENO,&letter,1);
	/* Return true si letter est un caractère de controle*/

		int is_ctrl = Treatment(letter);

		if((!iscntrl(letter)||letter=='\n') && is_ctrl==0)
		{		
			write(fd,&letter,1);
			write(STDOUT_FILENO,&letter,1);
		}		
}

/************************************/
int main(int argc, char const *argv[])
{	
	RawMode();
	atexit(clear);
	
	// On regarde si un nom de fichier est lancé avec l'éditeur //
	int file = open(argv[argc-1],O_RDWR|O_CREAT,S_IRWXU);
	if(file==-1)error("open");

	print_file(file);/* on affiche le fichier */

	while(1)
	{	
		Writting(file);		
	}
	close(file);
	return 0;
}