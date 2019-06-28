#include <stdio.h>
#include <unistd.h>
#include <termios.h>
#include <ctype.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <string.h>

#define BANKAI_VERSION	"0.0.1"
#define CTRL_KEY(key)	( key & 0x1F )
#define ABUF_INIT 		{ NULL, 0}

typedef enum
{
	MOVE_UP = 1000,
	MOVE_DOWN,
	MOVE_LEFT,
	MOVE_RIGHT,
	PAGE_UP,
	PAGE_DOWN
}arrow_key_t;

typedef struct{
  char *b;
  int len;
}abuf_t;
  
typedef struct 
{
	unsigned int current_x;
	unsigned int current_y;
	unsigned int term_rows;
	unsigned int term_cols;
	struct termios orig_term;
}editorConfig_t;

editorConfig_t editorConfig;

int ReadKeyInput(void);

void abAppend(abuf_t *ab, const char *s, int len) 
{
	char *new = realloc(ab->b, ab->len + len);
	if(new != NULL)
	{
		(void)memcpy(&new[ab->len], s, len);
		ab->b = new;
		ab->len += len;
	}
}

void abFree(abuf_t *ab) 
{
	free(ab->b);
	ab->len = 0;
}

void die(const char *error_string) 
{
	perror(error_string);
	exit(1);
}

void disableRawMode()
{
	if( tcsetattr( STDIN_FILENO, TCSAFLUSH, &editorConfig.orig_term) == -1 )
	{
		die("tcsetattr");
	}
}

void enableRawMode() 
{
	struct termios raw;
	
	if( tcgetattr( STDIN_FILENO, &editorConfig.orig_term) == -1 )
	{
	  die("tcgetattr");
	}
	atexit( disableRawMode);
	
	raw = editorConfig.orig_term;
	
	raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
	raw.c_oflag &= ~(OPOST );
	raw.c_cflag |= (CS8);
	raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG );
	raw.c_cc[VMIN] = 0;
	raw.c_cc[VTIME] = 1;
	
	if( tcsetattr( STDIN_FILENO, TCSAFLUSH, &raw) == -1 )
	{
	  die("tcsetattr");
	}
}

void editorMoveCursor(int key) 
{
	switch( key ) 
	{
		case MOVE_LEFT:
			if( editorConfig.current_x > 0 )
			{
				--editorConfig.current_x;
			}
			break;
		case MOVE_RIGHT:
			if( editorConfig.current_x < editorConfig.term_cols )
			{ 
				++editorConfig.current_x;
			}
			break;
		case MOVE_UP:
			if( editorConfig.current_y > 0 )
			{
				--editorConfig.current_y;
			}
			break;
		case MOVE_DOWN:
			if( editorConfig.current_y < editorConfig.term_rows )
			{
				++editorConfig.current_y;
			}
			break;
		default:
			break;
	}
}

int getCursorPosition(int *rows, int *cols) 
{
	char buff[32];
	unsigned int index = 0;
	unsigned int retval = -1;
	
	if(write(STDOUT_FILENO, "\x1b[6n", 4) == 4) 
	{
		while( ( read(STDIN_FILENO, &buff[index], 1) == 1 ) &&
				( buff[index] != 'R' ) )
		{
			index++;
		}
		buff[++index] = '\0';
		
		if(buff[0] == '\x1b' && buff[1] == '[')
		{ 
			if(sscanf(&buff[2], "%d;%d", rows, cols) == 2)
			{
				retval = 0;
			}
		}
	}
	return retval;
}

int getTerminalSize(int *rows, int *cols) 
{
	int retval = 0;
	struct winsize termsize;
	if( ( ioctl(STDOUT_FILENO, TIOCGWINSZ, &termsize) == -1 ) || ( termsize.ws_col == 0) ) 
	{
		if( write(STDOUT_FILENO, "\x1b[999C\x1b[999B", 12) == 12 )
		{
			if( getCursorPosition(rows,cols) == -1 )
			{
				retval = -1;
			}
		}
		else
		{
			retval = -1;
		}
	} 
	else 
	{
		*cols = termsize.ws_col;
		*rows = termsize.ws_row;
	}
	return retval;
}

void editorDrawRows(abuf_t* ab) 
{
	int y;
	char welcome[80];
	unsigned int welcome_len;
	unsigned int padding_len;
	for( y = 0; y < editorConfig.term_rows; y++) 
	{
		if( y == ( editorConfig.term_rows / 3 ) )
		{
			welcome_len = snprintf( &welcome[0], sizeof(welcome), "Bankai Version - %s", BANKAI_VERSION);
			
			if( welcome_len > editorConfig.term_cols )
			{
				welcome_len = editorConfig.term_cols;
			}
			padding_len = ( editorConfig.term_cols - welcome_len) / 2;
			
			abAppend( ab, "~", 1 );
			--padding_len;
			while( padding_len )
			{
				abAppend( ab, " ", 1);
				--padding_len;
			}
			abAppend(ab, &welcome[0], welcome_len );
		}
		else
		{
			abAppend( ab, "~", 1);
		}
		abAppend(ab, "\x1b[K", 3); /* clear single line */
		if( y < ( editorConfig.term_rows - 1 ) )
		{
			abAppend( ab, "\r\n", 3);
		}
	}
}

void editorRefreshScreen() 
{
	abuf_t ab = ABUF_INIT;
	char local_buff[32];
	abAppend( &ab, "\x1b[?25l", 6); /* Hides cursor */
	//abAppend( &ab, "\x1b[2J", 4); /* clear entire screen */
	abAppend( &ab, "\x1b[H", 3); /* move cursor to top left */
	editorDrawRows(&ab);
	//abAppend( &ab, "\x1b[H", 3); /* move cursor to top left */
	snprintf(local_buff, sizeof(local_buff), "\x1b[%d;%dH", editorConfig.current_y + 1, editorConfig.current_x + 1);
	abAppend(&ab, local_buff, strlen(local_buff));		
	abAppend( &ab, "\x1b[?25h", 6); /* Show Cursor */
	
	write( STDOUT_FILENO, ab.b, ab.len);
	
	abFree(&ab);
}

int ReadKeyInput()
{
	char input;
	int retval;
	char sequence[3];
	while( 1 )
	{
		retval = read( STDIN_FILENO, &input, 1 );
		if( ( retval == -1 ) && ( errno != EAGAIN ) )
		{
			die("read");
		}
		else if( retval == 1 )
		{
			/**
			 * Escape Sequence Read Algorithm 
			 * */
			retval = input;
			if( input == '\x1b' )
			{
				if( read(STDIN_FILENO, &sequence[0], 1) == 1)
				{
					if( read(STDIN_FILENO, &sequence[1], 1) == 1)
					{						
						if( sequence[0] == '[') 
						{
							if( sequence[1] >= '0' && sequence[1] <= '9') 
							{
								if( read(STDIN_FILENO, &sequence[2], 1) == 1) 
								{
									if( sequence[2] == '~') 
									{
										switch( sequence[1]) 
										{
											case '5': 
												retval = PAGE_UP;
												break;
											case '6': 
												retval = PAGE_DOWN;
												break;
											default:
												break;
										}
									}
								}
								else
								{
									retval = '\x1b';
								}
							}
							else
							{					
								switch( sequence[1] ) 
								{
									case 'A': 
										retval = MOVE_UP;
										break;
									case 'B': 
										retval = MOVE_DOWN;
										break;
									case 'C': 
										retval = MOVE_RIGHT;
										break;
									case 'D': 
										retval = MOVE_LEFT;
										break;
									default:
										break;
								}	
							}
						}
					}
					else
					{
						retval = '\x1b';
					}				
				}
			}
			if( iscntrl( retval ) )
			{
				printf("%d",retval);
			}
			else
			{
				printf("%d ('%c') ", retval, retval);
			}
			fflush(stdout);
			break;
		}
	}
	return retval;	
}

void KeyPresshandler()
{
	int keyevent = ReadKeyInput();
	
	switch( keyevent )
	{
		case CTRL_KEY('q'):
			write(STDOUT_FILENO, "\x1b[2J", 4);/* clear entire screen */
			write(STDOUT_FILENO, "\x1b[H", 3);/* move cursor to top left - default 1;1*/
			exit(0);
		case MOVE_LEFT:
		case MOVE_DOWN:
		case MOVE_RIGHT:
		case MOVE_UP:
			editorMoveCursor( keyevent);
			break;
		default:
			break;
	}
}

void initEditor() 
{
	editorConfig.current_x = 0;
	editorConfig.current_y = 0;
	
	if(getTerminalSize(&editorConfig.term_rows, &editorConfig.term_cols) == -1) 
	{
		die("getWindowSize");
	}
}

int main()
{
	enableRawMode();
	initEditor();
	while( 1 )
	{
		editorRefreshScreen();
		KeyPresshandler();
	}
	return 0;
}

/** 
 * STEPS TILL NOW
 * ------------------
 * 
 * enableRawMode()  -> Read character byte by byte rather than line by line
 * 					-> Diasabled Echo while reading
 * 					-> Diabled Control Signals and XON/XOFF
 * 					-> ICRNL - Change \r\n to only \n
 * Exit on pressing CTRL_Q 
 * Refresh screen after every ouput
 * ~(Tilde) on starting of line for whole screen
 * Calculating Rows and columns of screen
 * 				- use ioctl to get window size
 * 				- move cursor to bottom right corner and get cursor position using VT100 commands
 * Move Some writes in to common buffer for single write
 * 
 * WORKING FLOW
 * -----------------
 * 
 * Terminal Init 
 * 			- Calculate no of rows and columns
 * 			- Disable and Enable desired features of terminal
 *
 * loop:
 * 		Refresh Screen
 * 			- Show tilde in all rows of terminal
 * 		Wait for any data from stdin
 * 		Exit on CTRL_Q / Loop again of other keys
 * */ 
