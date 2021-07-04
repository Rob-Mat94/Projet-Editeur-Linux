#include <stdlib.h>	
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <time.h>
/**************** Structures *******************/
	/* Mode */
typedef enum Mode
{
	INSERTION =1,
	NORMAL,
	UNDEFINED
}Mode;

/* touche spécial */
typedef enum key
{
	UP,
	DOWN,
	LEFT,
	RIGHT,
	ESC,
	SUPPR, /* [3~ */
	BACKSPACE
}key;

typedef enum Warning
{
	UNSAVE,
	EXISTING,
	UNTITLED,
	EMPTY
}Warning;


/* Type pour représenter une ligne d'un fichier */
typedef struct line
{
	char* chaine;
	int length;

	/* Cela représente la chaine du dessus sans les tabulations (pour gérer le curseur) */
	char* rendu;
	int sndlength;

}line;

/* Structure générale pour stocker les informations importantes */
struct UserConfig
{
	struct termios original;/* On sauvegarde la structure original de la console */

	int termrow; /*  nombre de lignes */
	int termcol; /* nombre de colonnes */

	int havefile; /* indicateur d'un fichier présent */
	int ChargedFile; /* Descripteur de fichier en cours */
	char* NameFile; /* Nom du fichier */
	
	int exit; /* booléen pour la sortie */

	int numline; /* nombre de ligne */
	line* ligne; /* Contiendra les lignes */

	int CursorX; /* Position X */
	int CursorY ; /* Position Y */
	int CursorTabX; /* Position en fonction des tabulations sur la ligne */

	int ScrollY; /* Vérifie si on dépasse en bas ou en haut de l'écran (CursorY + Scroll = ligne actuelle) */
	int ScrollX; /* idem que ScrollY mais horizontale */

	time_t session; /* On stocke ici le temps en seconde où l'on ) commencé le programme */

	/* Warning */
	Warning wmessage; /* Contiendra le type d'erreur utilisateur */
	int have_warning; /* booléen qui informe si la présence éventuelle d'une erreur utilisateur */

	/* Il faut modifier le fichier /etc/group en superadmin, ou modfier les droits avec sudo (sudo chmod 0444 MOUSE) */
	int mousefd; /* MOUSE ( /dev/input/mice ) */

	Mode mode; /* mode de l'éditeur */

	int is_save; /* booléen pour vérifier la sauvegarde */

};

/* Cela permet l'écriture de plusieurs séquences d'échappements ou autres */
typedef struct dynabuff
{
	char* txt;
	int length;
}dynabuff;

/**************** Macro et variables globales ********************/

#define VTIMER 0.8 /* timer de lecture, (pour gérer la touche ESC) */
#define MOUSE "/dev/input/mice"
#define COLORRESET {write(STDOUT_FILENO,"\x1b[0m",4);} /* Enlève tous les attributs de couleurs */
#define WARNINGCOLOR {write(STDOUT_FILENO,"\x1b[31m",5);} /* Couleur rouge */

struct UserConfig config; /* structure globale pour les informations */

/**************** Configuration du Terminal ********************/

void DisableRawMode()
{	
	/* On remet les attributs du terminal par défault avec le version originale stocké dans la structure */
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&config.original);
}

void EnableRawMode()
{
	tcgetattr(STDIN_FILENO,&config.original);
	
	struct termios t = config.original; 
	/* Désactiver les autres drapeaux ne change rien, ICANON est désactivé */

	/* Modification */

	t.c_lflag &= ~(ECHO | ICANON | ISIG | IEXTEN);
	/* ISIG : Désactive CTRL-C et CTRL-Z */
	/* Impression des touches désactivé (ECHO) */
	/* Mode canonique désactivé */
	/* IEXTEN : CTRL-V */

	t.c_iflag &= ~(IXON | ICRNL);
	/* Désactive CTRL-S et CTRL-Q */
	/* Plus de traduction en sortie (\r en \n) */

	
	t.c_oflag &= ~(OPOST);
	/* Désactive tout traitement de sortie */

	t.c_cc[VTIME]=VTIMER;
	t.c_cc[VMIN]=0;
	
	tcsetattr(STDIN_FILENO,TCSAFLUSH,&t);
}


/****************** Erreurs **********************/

void error(char* log)
{	
	write(STDOUT_FILENO,"\x1b[2J",4); /* Efface l'écran */
	write(STDOUT_FILENO,"\x1b[H",3); /* Place le curseur en haut à gauche */

	perror(log);
	exit(EXIT_FAILURE);
}

/**************** Buffer ********************/
/* Initialisation */
dynabuff CreateBuffer()
{
	dynabuff buffer;
	buffer.txt = NULL;
	buffer.length = 0;

	return buffer;
}

/* Ajout */
void EditBuffer(dynabuff* buff, int l,char* chaine)
{
	char* temporaire = realloc(buff->txt,buff->length + l);
	if(temporaire==NULL)
		error("Memory failure");

	strncpy(&temporaire[buff->length],chaine,l);
	buff->txt = temporaire;
	buff->length = buff->length + l;
}

/* Suppression */
void FreeBuffer(dynabuff* buff)
{
	free(buff->txt);
}

/* Libère la mémoire de toutes les lignes de la structure */
void freeAll()
{
	for(int i = 0;i<config.numline;i++)
	{
		free(config.ligne[i].chaine);
		free(config.ligne[i].rendu);
	}
	free(config.ligne);
}
/*****************Console*******************/

/* Il faut calculer la véritable position de CursorX si il y'a
	une tabulation, nous avons fixé à '\t' = 6 */
int TabConversion(line* ligne, int CursorX)
{
	int conversion = 0;

	for(int i = 0;i<CursorX;i++)
	{
		if(ligne->chaine[i] == '\t')
		{
			conversion = conversion + 5 - (conversion%6);
		}
		conversion++;
	}

	return conversion;
}
/* Comme on ne lit plus "config.termcol" caractères mais
	une ligne complète, un défilement vertical s'impose */

/* La fonction reprend le même principe que dans les versions précédentes avec ScrollY */
/* On affiche grâce à cela les bonnes lignes du fichier en fonction de la position des curseurs */
void Scroll()
{	
				/* Vertical */
	if(config.CursorY < config.ScrollY)
	{
		config.ScrollY = config.CursorY;
	}

	/* Si le curseur dépasse en bas */
	if(config.CursorY >= config.ScrollY + config.termrow)
	{
		config.ScrollY = config.CursorY - config.termrow + 1;
	}

				/* Horizontal */
	/* Même principe que pour CursorY mais le CursorX est sensible au tabulation, il faut l'actualiser en fonction de la 
		présence éventuelle de tabulations */
	config.CursorTabX = 0;
	if(config.CursorY != config.numline)
		config.CursorTabX = TabConversion(&config.ligne[config.CursorY],config.CursorX);

	if(config.CursorTabX < config.ScrollX)
		config.ScrollX = config.CursorTabX;

	if(config.CursorTabX >= config.ScrollX + config.termcol)
		config.ScrollX = config.CursorTabX - config.termcol+1;
}

/* Pour visualiser le fichier */
void ProcessLine(dynabuff* buff)
{	
	char c;
	for(short i = 0 ; i < config.termrow ;i++)
	{
		int index = i + config.ScrollY;

		if(index == config.numline)break;

		/* Prise en compte du décalage horizontal, on écrit la chaine en commençant au caractère correspondant au décalage */
		int length = config.ligne[index].sndlength - config.ScrollX; 

		if(length>config.termcol)
			length = config.termcol;
		if(length < 0) 
			length = 0;

		if(config.ligne[index].rendu!=NULL)
			EditBuffer(buff,length,&config.ligne[index].rendu[config.ScrollX]);
			/* Modification du pointeur pour commencer avec le bon décalage */		
		EditBuffer(buff,2,"\r\n");

		/* On regarde si il y'a un message d'avertissement stocké dans la structure */
		/* Si oui on l'affiche sur la dernière ligne du terminal et on attend une pression sur une touche pour continuer */
		if(config.have_warning)
		{	
			/* On déplace le curseur en bas comme pour Cmd() sans modifier les valeurs dans la structure */
			char buff[64];
			sprintf(buff,"\x1b[%d;%dH",config.termrow+1,0);
			write(STDOUT_FILENO,buff,strlen(buff));
			switch(config.wmessage)
			{
				case EXISTING :

					WARNINGCOLOR;
					write(STDOUT_FILENO,"\x1b[2K",4); /* On efface la ligne */
					write(STDOUT_FILENO,"File already exist",18);
					COLORRESET;
					config.have_warning = 0;
					config.wmessage = UNDEFINED;
					while(read(STDOUT_FILENO,&c,1)!=1);
					break;

				case UNSAVE :

					WARNINGCOLOR;
					write(STDOUT_FILENO,"\x1b[2K",4); /* On efface la ligne */
					write(STDOUT_FILENO,"Unsave file (:q! to exit)",25);
					COLORRESET;
					config.have_warning = 0;
					config.wmessage = UNDEFINED;
					while(read(STDOUT_FILENO,&c,1)!=1);
					break;

				case UNTITLED :

					WARNINGCOLOR;
					write(STDOUT_FILENO,"\x1b[2K",4); /* On efface la ligne */
					write(STDOUT_FILENO,"Untitled File, use [:w \"filename\"] to create one",48);
					COLORRESET;
					config.have_warning = 0;
					config.wmessage = UNDEFINED;
					while(read(STDOUT_FILENO,&c,1)!=1);
					break;

				default:
					break;

			}
		}			
	}
}

void Clear(dynabuff* buff)
{	
	EditBuffer(buff,4,"\x1b[2J"); /* efface l'écran */
	EditBuffer(buff,3,"\x1b[H"); /* repositionne le curseur en haut à gauche (position d'origine) */
}

void MoveCursor(dynabuff* buff)
{	
	char buffer[64];
	sprintf(buffer,"\x1b[%d;%dH",(config.CursorY-config.ScrollY)+1,(config.CursorTabX -config.ScrollX)+1);
	EditBuffer(buff,strlen(buffer),buffer);
}

/* Rafraichir l'écran, après une pression sur une touche, on actualise l'écran pour afficher ce que 
	l'utilisateur à rentré */
void Refresh()
{	
	Scroll();
	dynabuff buffer = CreateBuffer();
	Clear(&buffer);
	ProcessLine(&buffer);
	MoveCursor(&buffer);
	write(STDOUT_FILENO,buffer.txt,buffer.length);
	FreeBuffer(&buffer); 
}

/* Obtenir la taille du terminal */
int GetWindowSize()
{	
	/* référence ioctl dans le man termios */
	struct winsize bashsave;
	if(ioctl(STDOUT_FILENO,TIOCGWINSZ,&bashsave)==-1)return -1;
	config.termcol = bashsave.ws_col ;
	config.termrow = bashsave.ws_row ;

	return 0;

}

/***************** Fichier et visualisation *************************/

void OptimizeLine(line * buff)
{	
	int special = 0;
	for(int i = 0;i<buff->length;i++)
		if(buff->chaine[i] == '\t')special++;
	/* On compte le nombre de tabulations */

	free(buff->rendu);
	buff -> rendu = malloc(buff->length + special*5 +1);

	int i; 
	int k = 0;

	for(i= 0;i < buff->length;i++)
	{
		if(buff->chaine[i] == '\t')
		{	
			buff->rendu[k++] = ' ';
			while(k % 6 !=0)buff->rendu[k++] = ' ';
		}

		else
		{
			buff->rendu[k++] = buff->chaine[i];
		}
	}
	buff->rendu[k]='\0';
	buff->sndlength = k;
}


/* Création d'une ligne dans le tableau avec la chaine et la taille spécifié à l'index indiqué (on bouge l'ancienne avec memmove)*/
void LoadLine(int index, char* src , int length)
{	
	if(index < 0 || index > config.numline)return;

	config.ligne = realloc(config.ligne,sizeof(line)*(config.numline+1));

	memmove(&config.ligne[index +1],&config.ligne[index],sizeof(line)*(config.numline - index));

	config.ligne[index].chaine = malloc(length+1);
	strncpy(config.ligne[index].chaine,src,length);
	config.ligne[index].chaine[length] = '\0';
	config.ligne[index].length = length;

	/* Ligne qui contiendra les caractères "réels" (où l'on enlèvera les tabulations) */
	config.ligne[index].sndlength = 0;
	config.ligne[index].rendu = NULL;
	OptimizeLine(&config.ligne[index]);

	config.numline++;
}

/* Insére letter dans ligne à la position pos dans la chaine */
void ModifyLine(line * ligne, int pos, char letter)
{
	if(pos > ligne->length||pos < 0)pos = ligne->length;
	ligne->chaine = realloc(ligne->chaine,ligne->length+2);

	/* pos -> pos+1, on insère à pos */
	memmove(&ligne->chaine[pos+1],&ligne->chaine[pos],ligne->length - pos +1);
	ligne->length+=1;
	ligne->chaine[pos] = letter;

	/* "Optimisation" si il y'a tabulation */
	OptimizeLine(ligne);
}

void ModifyLineSuppr(line* ligne,int pos)
{
	if(pos<0||pos>=ligne->length)return;
	memmove(&ligne->chaine[pos],&ligne->chaine[pos+1],ligne->length -pos); /* On écrase le caractère précédent avec memmove */
	ligne->length--; /* On réduit la taille de la ligne */
	OptimizeLine(ligne);
}

void DeleteLine(int index)
{
	if(index < 0 || index == config.numline)return;
	free(config.ligne[index].chaine);free(config.ligne[index].rendu);
	memmove(&config.ligne[index],&config.ligne[index+1],sizeof(line)*(config.numline - index - 1));
	/* ligne précédente écrasée */
	config.numline--;
}

/* Ajoute la chaine "ajout" de taille length à la une ligne, requis quand on supprime une ligne */
void AddtoLine(line * ligne,char * ajout,int length)
{
	ligne->chaine = realloc(ligne->chaine,ligne->length + length +1);
	strncpy(&ligne->chaine[ligne->length],ajout,length); /* On ajoute à la fin, (position = taille de la chaine) */
	ligne->length += length;
	ligne->chaine[ligne->length-1] = '\0';

	OptimizeLine(ligne);
}
/* Gestion du retour chariot */
void Enter()
{
	if(config.CursorX == 0)
		LoadLine(config.CursorY,"",0);
	else
	{
		line* ligne = &config.ligne[config.CursorY];
		/* On crée une ligne à CursorY+1 (Enter) avec le reste de la chaine ou l'on est et la taille correspondante */
		LoadLine(config.CursorY+1, &ligne->chaine[config.CursorX], ligne->length - config.CursorX);
		ligne = &config.ligne[config.CursorY];
		ligne->length = config.CursorX;
		ligne->chaine[ligne->length]='\0';
		OptimizeLine(ligne);
	}
	config.CursorX = 0;
	config.CursorY++;
}

void Delete()
{	
	if(config.CursorX == 0 && config.CursorY ==0)return;
	if(config.CursorY == config.numline)return;

	line * ligne = &config.ligne[config.CursorY];

	if(config.CursorX == 0)
	{
		config.CursorX = config.ligne[config.CursorY - 1].length;
		AddtoLine(&config.ligne[config.CursorY-1],ligne->chaine,ligne->length);
		DeleteLine(config.CursorY);
		config.CursorY--;
	}
	if(config.CursorX > 0)
	{
		ModifyLineSuppr(ligne,config.CursorX -1);
		config.CursorX--;
	}
}
void Write(char letter)
{
	if(config.CursorY == config.numline)
	{
		LoadLine(config.numline,"",0); /* Création d'une ligne en cas de fin de fichier */
	}
	/* On Ajoute le caractère à la n° CursoY ligne, et au CursorX caractère */
	ModifyLine(&config.ligne[config.CursorY],config.CursorX,letter);
	config.CursorX++;
}

/* Cette fonction recopie le fichier ouvert dans config.ChargedFile ligne par ligne */
/* On ne fait plus jusqu'à "config.termcol" caractères comme dans les anciennes versions */
void LoadFile()
{	
	while(1)
	{
		char* buffer = malloc(sizeof(char)+1);char letter;
		int l = 0;
		ssize_t r = read(config.ChargedFile,&letter,1);

		while((r>0 && letter!='\n'))
		{
			buffer = realloc(buffer,l+1);
			buffer[l] = letter;
			l++;
			r = read(config.ChargedFile,&letter,1);
		}
		if(r == 0)break;
		LoadLine(config.numline,buffer,l);
		free(buffer);
	}
	close(config.ChargedFile);
}

/*****************Edition********************/

void SaveActualFile()
{	
	if(config.havefile == 0)
	{	
		config.wmessage = UNTITLED;
		config.have_warning = 1;
		return;
	} 

	config.is_save = 1;

	config.ChargedFile = open(config.NameFile,O_CREAT|O_RDWR|O_TRUNC,S_IRWXU);
	/* On tronque le fichier pour totalement le réécrire */
	for(int i = 0;i<config.numline;i++)
	{
		write(config.ChargedFile,config.ligne[i].chaine,config.ligne[i].length);
		write(config.ChargedFile,"\n",1);
	}

	close(config.ChargedFile);
}

void SaveNewFile(char* filename)
{	
	if(config.havefile == 1)return; 
	config.NameFile = filename;
	config.is_save = 1;
	/* Si le fichier existe déjà */
	int fd;
	if((fd = open(filename,O_RDWR))!=-1)
	{
		close(fd);
		config.wmessage = EXISTING;
		config.have_warning = 1;
		return;
	}

	config.ChargedFile = open(filename,O_RDWR|O_CREAT,S_IRWXU);
	config.havefile = 1;
	for(int i = 0;i<config.numline;i++)
	{
		write(config.ChargedFile,config.ligne[i].chaine,config.ligne[i].length);
		write(config.ChargedFile,"\n",1);
	}
	close(config.ChargedFile);

}

void UserInfo()
{
	char buffer[config.termcol];
	if(config.NameFile == NULL)
	{
		sprintf(buffer,"File : Untitled, Line : n°%d / %d Session : %d minute(s)",config.CursorY,config.numline,(int)((time(NULL)-config.session)/60));
	}
	else
	{
		sprintf(buffer,"File : \"%s\", Line : n°%d / %d Session : %d minute(s)",config.NameFile,config.CursorY,config.numline,(int)((time(NULL)-config.session)/60));
	}

	write(STDOUT_FILENO,"\x1b[1;41m",7);

	/* On déplace le curseur en bas comme pour Cmd() */
	char buff[64];
	sprintf(buff,"\x1b[%d;%dH",config.termrow+1,0);
	write(STDOUT_FILENO,buff,strlen(buff));

	write(STDOUT_FILENO,buffer,strlen(buffer));

	char letter;
	while(read(STDIN_FILENO,&letter,1)!=1); /* On attend une pression sur une touche */

	write(STDOUT_FILENO,"\x1b[2K",4); /* On efface la ligne */
	COLORRESET;/* On rétablit la couleur de base */

}

void Cmd()
{	
	/* Commande : 
	- :w
	- :wq
	- :w "file"
	- :i
	- :q!
	*/
	char* buffer;
	buffer = malloc(sizeof(char)*100);
	int buffsize = 0;

	int i =1;

	buffer[0] = ':';
	buffsize++;

	while(1)
	{	
		char buff[64];
		sprintf(buff,"\x1b[%d;%dH",config.termrow+1,0);
		write(STDOUT_FILENO,buff,strlen(buff));

		write(STDOUT_FILENO,buffer,buffsize);

		char c;
		while((read(STDIN_FILENO,&c,1)!=1));

		/* On peut modifier la couleur pour l'invité de commande, il faut rétablir à chaque fois */
		write(STDOUT_FILENO,"\x1b[1;46m",9);

		if((int)c == 127)
		{
			if(buffsize == 1)
			{
				COLORRESET;
				return;
			}
			buffer[i-1]='\0';
			i--;
			buffsize--;
			write(STDOUT_FILENO,"\x1b[2K",4); /* efface toute la ligne (elle est réaffiché juste après) */		
		}

		else if((int)c > 31)
		{	
			buffer[i] = c;
			buffsize++;
			i++;
		}
		/* 13 (\r) car il y'a plus de traduction/traitement en sortie */
		else if((int)c==13 || c == '\r')
		{
			buffer[i] ='\0';

			/* On regarde si cela correspont aux commandes suivantes : */
			if(strcmp(buffer,":q")==0)
			{	
				if(config.is_save)
				{
					config.exit=1;
					COLORRESET;
					return;
				}
				else
				{
					config.wmessage = UNSAVE;
					config.have_warning = 1;
					COLORRESET;
					return;
				}
			}
			if(strcmp(buffer,":w")==0)
			{
				SaveActualFile();
				COLORRESET;
				return;
			}
			if(strcmp(buffer,":i")==0)
			{
				COLORRESET;
				UserInfo();
				return;
			}
			if(strcmp(buffer,":wq")==0)
			{
				SaveActualFile();
				config.exit=1;
				COLORRESET;
				return;
			}
			if(strcmp(buffer,":q!")==0)
			{
				config.exit=1;
				COLORRESET;
				return;
			}

			char * s = malloc(strlen(buffer)+1);
			strncpy(s,buffer,buffsize);
			char * t = malloc(strlen(buffer)+1);
			strncpy(t,buffer,buffsize);

			if((strtok(s,":w "))!=NULL && buffer[1]=='w')
			{
				SaveNewFile(&buffer[3]);
				free(s);
				COLORRESET;
				return;
			}
			/* On appuie sur ENTER, on quitte par défault */
			COLORRESET;
			return;
		}

	}
	free(buffer);
}


int ReadMouseProcess()
{	
	/* Le processus est le même que pour certaines touches */
	char data[3];
	int left,middle;

	if(read(config.mousefd,data,sizeof(data))>0)
	{
		left = data[0] & 0x1; /* 0 ou 1 */
		middle = data[0] & 0x4; /* 0 ou 1 */

		if(middle)return DOWN;
		if(left) return RIGHT;
	}

	return -1;
}

int ReadKey()
{	
	char letter;
	if((letter=ReadMouseProcess())==-1)
		while((read(STDIN_FILENO,&letter,1))!=1);
	else
		return letter;
	
	if((int)letter==127) 
		return BACKSPACE;
	
	if(letter!='\x1b')
		return letter;

	/* On lit une nouvelle fois pour analyser la touche */
	/* [A -> Haut, [B -> Bas, [C -> Droit, [D -> Gauche*/
	/* Si il n'y a pas ce que nous cherchons, on renvoit ESC directement */
	char letterS[3];
	read(STDIN_FILENO,&letterS[0],1);
	read(STDIN_FILENO,&letterS[1],1);
	read(STDIN_FILENO,&letterS[2],1);

	if(letterS[0]=='[')
	{	
		if(letterS[1]!='A' && letterS[1]!='B'
			&& letterS[1]!='D' && letterS[1]!='C' && letterS[1]!='3' && letterS[2]!='~')
		{
			return ESC; /* durée de "pression" changé pour la rapidité de lecture */
		}
		switch(letterS[1])
		{
			case 'A':
				return UP;
			case 'B':
				return DOWN;
			case 'C':
				return RIGHT;
			case 'D':
				return LEFT;
			case '3':
			if(letterS[2]=='~')return SUPPR;
			break;
		}
	}

	return ESC; 
					
}

int IsCtrlCmd(char letter)
{
	switch(letter)
	{
		case 'i':
			config.mode = INSERTION;
			return 1;
		case ':':  /* Invité de commande */
			Cmd();
			return 1;
		case '+': /* FIn de fichier */
			config.CursorY = config.numline;
			config.CursorX = 0;
			return 1;
		case '-': /* Début de fichier */
			config.CursorY = 0;
			config.CursorX = 0;
			return 1;
		case '/': /* Milieu de fichier */
			config.CursorY = config.numline/2;
			config.CursorX = 0;
			return 1;
		default:
			return 0;
	}
	
}
void ReadProcess()
{	
	
	char letter;	
	letter = ReadKey();
	/*********************************/
	if(config.mode == NORMAL)
	{
		if(IsCtrlCmd(letter)==1)return;
	}

	/* On récupére la ligne en cours pour certaines vérifications */
	line* buff;
	if(config.CursorY >= config.numline)
		buff = NULL;
	else
		buff = &config.ligne[config.CursorY];

	switch(letter)
	{
		/* il n'y a plus de traitement en sortie ('\r')*/
		case '\r':
			if(config.mode == INSERTION)
			{
				Enter();
				config.is_save = 0;
			}
			break;

		/* Suppression à droite */
		case SUPPR:
			if(config.mode == INSERTION)
			{
				config.CursorX++;
				Delete();
				config.is_save = 0;
			}
			break;

		case BACKSPACE:
			if(config.mode == INSERTION)
			{
				Delete();
				config.is_save = 0;
			}
			break;

		case UP:
			if(config.CursorY != 0)
				config.CursorY--;
			break;

		case DOWN:
			if(config.CursorY != config.numline)
				config.CursorY++;
			break;

		case RIGHT:
			/* Il faut vérifier que l'utilisateur n'aille pas au dela de la longueur de la ligne */
			/* Pour cela on a récupérer la ligne en cours et sa taille pour vérifier */
			if(buff != NULL && config.CursorX < buff->length)
				config.CursorX++;
			/* Si on bouge vers la droite mais que l'on est en fin de ligne, on passe à la ligne suivante */
			else if(buff != NULL && config.CursorX == buff->length)
			{
				config.CursorY++;
				config.CursorX = 0;
			}
			break;

		case LEFT:
			if(config.CursorX != 0)
				config.CursorX--;
			/* Gauche et debut de ligne = ligne du dessus */
			else if(config.CursorY != 0)
			{
				config.CursorY--;
				config.CursorX = config.ligne[config.CursorY].length;
			}
			break;

		case ESC:
			/* INSERTION */
			if(config.mode != NORMAL)config.mode = NORMAL;
			break;

		default :
		/* On regarde si ne n'est pas un caractère de contrôle, sauf une tabulation pour insérer */
		if(config.mode == INSERTION && (!iscntrl(letter) ||letter =='\t'))
		{	
			config.is_save = 0;
			Write(letter);
		}
		break;
	}

	/* Si on se déplace sur une ligne plus courte (vers le bas ou haut) : 
	 Il faut alors déplacer le curseur à la fin de celle-ci sinon le curseur est dans le vide (pas dans le fichier) */
	if(config.CursorY >= config.numline) 
		buff = NULL;
	else 
		buff = &config.ligne[config.CursorY];

	int x;
	if(buff == NULL)
		x = 0;
	else
		x = buff->length;

	if(config.CursorX > x)
		config.CursorX = x; /* Rectification du curseurX */
}

/* Menu principal de l'éditeur de texte */
void MainMenu()
{	
	char title[] = "CloneEditor v0.1 Alpha";
	char buffer[64];
	sprintf(buffer,"\x1b[%d;%dH",2,(int)(config.termcol/2 - (strlen(title)/2)));

	dynabuff buff = CreateBuffer();
	EditBuffer(&buff,6,"\x1b[?25l");
	EditBuffer(&buff,strlen(buffer),buffer);
	EditBuffer(&buff,7,"\x1b[40;1m");
	EditBuffer(&buff,strlen(title),title);

	char message[] = "Press ENTER to continue, or enter a filename to open it";
	char buffer2[64];
	sprintf(buffer2,"\x1b[%d;%dH",5,(int)(config.termcol/2 - (strlen(message)/2))); 

	EditBuffer(&buff,strlen(buffer2),buffer2);
	EditBuffer(&buff,strlen(message),message);
	EditBuffer(&buff,6,"\x1b[?25h");
	EditBuffer(&buff,4,"\x1b[0m");

	char pos[64];
	sprintf(pos,"\x1b[%d;%dH",config.termrow+1,0); 
	
	EditBuffer(&buff,strlen(pos),pos);

	write(STDOUT_FILENO,buff.txt,buff.length);

	int exit = 0;
	char cmd[32];int cmdsize = 0;
	while(!exit)
	{
		char c;
		while((read(STDIN_FILENO,&c,1)!=1));

		if(c == '\r')
		{	
			if(cmdsize == 0)exit = 1;
			else
			{	
				cmd[cmdsize] = '\0';
				config.ChargedFile = open(cmd,O_CREAT|O_RDWR,S_IRWXU);
				config.havefile = 1;
				config.NameFile = malloc(sizeof(char)*cmdsize+1);
				strcpy(config.NameFile,cmd);

				config.mode = NORMAL;
				LoadFile();
				FreeBuffer(&buff);
				return;
			}
		}
		else if ((int)c == 127)
		{	
			buffer[cmdsize]='\0';
			if(cmdsize > 0)cmdsize--;	
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,buff.txt,buff.length);
			write(STDOUT_FILENO,cmd,cmdsize);
		}
		else
		{	
			cmd[cmdsize] = c;
			cmdsize++;
			write(STDOUT_FILENO,"\x1b[2J",4);
			write(STDOUT_FILENO,buff.txt,buff.length);
			write(STDOUT_FILENO,cmd,cmdsize);
		}
	}
}
/************ Initialisation *************/

/* Cette fonction initialise tous les champs de la structure config */
void InitConfig()
{	
	config.session = time(NULL);
	config.CursorX = 0; /* Position x curseur */
	config.CursorY = 0; /* Position y curseur */

	config.CursorTabX = 0;

	GetWindowSize(); /* On récupère la taille du terminal et on la met dans config */
	config.termrow--;

	config.havefile = 0; /* Pas de fichier en cours */
	config.ChargedFile = -1; /* Descripteur à -1 */
	config.NameFile = NULL; /* Nom du fichier */

	EnableRawMode(); /* Activation du mode brut qui est désactivé à la sortie */

	config.exit = 0; /* booléen à faux */

	config.numline = 0; /* numéro de ligne écrite */
	config.ligne = NULL;

	config.ScrollY = 0; /* Dépassement de 0 */
	config.ScrollX = 0; /* Dépassement de 0 */

	config.mode = UNDEFINED; /* On fixe le mode sur non défini */
	if((config.mousefd = open(MOUSE,O_RDONLY|O_NONBLOCK))==-1)error("Open"); /* On ouvre /dev/input/mice */

	config.wmessage = EMPTY;
	config.have_warning = 0;

	config.is_save = 1; /* Le fichier n'est pas changé pour le moment */
}

/***************** Sortie ******************/

void Exit()
{	
	/* Fermeture des fichiers */
	if(config.havefile && config.ChargedFile > -1)close(config.ChargedFile);
	close(config.mousefd);

	dynabuff buffer = CreateBuffer();
	Clear(&buffer);
	write(STDOUT_FILENO,buffer.txt,buffer.length);
	FreeBuffer(&buffer);

	DisableRawMode(); /* désactivation des modifications */
}

/***************** Main ********************/

int main(int argc, char *argv[])
{	
	InitConfig();
	atexit(freeAll);

	/* On regarde si un fichier est passé en argument */
	if(argc > 1)
	{
		if((config.ChargedFile = open(argv[1],O_RDWR|O_CREAT,S_IRWXU))==-1)error("Open");
		config.havefile = 1;
		config.NameFile = argv[1];
		config.mode = NORMAL;
	}

	if(config.mode == UNDEFINED)config.mode = INSERTION;

	if(config.havefile)LoadFile(); /* Chargement du fichier dans la structure */
	else
	{	
		Refresh();
		MainMenu();
	}
	while(!config.exit)
	{	
		Refresh();
		ReadProcess();
	}
	/********************* Fin *********************/
	Exit();	
	return EXIT_SUCCESS;
}
