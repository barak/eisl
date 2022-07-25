#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <termios.h>
#define NCURSES_OPAQUE 1
#include <curses.h>
#include <locale.h>
#include <stdbool.h>
#include <string.h>
#include "compat/cdefs.h"
#include "edlis.h"


#ifndef CTRL
#define CTRL(X) ((X) & 0x1F)
#endif

#define TOKEN_MAX   80
#define LEFT_MARGIN 7
#define TOP_MARGIN  2
#define BOTTOM      22
#define MIDDLE      10


const int NUM_STR_MAX = 5;
const int SHORT_STR_MAX = 20;

bool edit_loop (char *fname);
int ctrl_c = 0;
int ctrl_z = 0;

// -----editor-----
int ed_scroll;
int ed_footer;
int ed_row;
int ed_col;
int ed_start;
int ed_end;
bool ed_ins = true;
int ed_tab = 0;
int ed_indent = 1;
int ed_name = NIL;
char ed_data[ROW_SIZE][COL_SIZE];
char ed_copy[COPY_SIZE][COL_SIZE];
int ed_lparen_row;
int ed_lparen_col;
int ed_rparen_row;
int ed_rparen_col;
int ed_clip_start;
int ed_clip_end;
int ed_copy_end;
const char *ed_candidate[COMPLETION_CANDIDATES_MAX];
int ed_candidate_pt;
const enum Color ed_syntax_color = RED_ON_DFL;
const enum Color ed_builtin_color = CYAN_ON_DFL;
const enum Color ed_extended_color = MAGENTA_ON_DFL;
const enum Color ed_string_color = YELLOW_ON_DFL;
const enum Color ed_comment_color = BLUE_ON_DFL;
int ed_incomment = -1;		// #|...|# comment
bool modify_flag;

__dead void
errw (const char *msg)
{
  endwin ();
  fprintf (stderr, "%s\n", msg);
  exit (EXIT_FAILURE);
}

void
clear_status ()
{
  ESCREV ();
  ESCMOVE (ed_footer, 1);
  CHECK (addstr, "                                            ");
  ESCMOVE (ed_footer, 1);
}

void
init_ncurses ()
{
  if (initscr () == NULL)
    {
      fputs ("initscr\n", stderr);
      exit (EXIT_FAILURE);
    }
  if (has_colors ())
    {
      CHECK (start_color);
#ifdef NCURSES_VERSION
      CHECK (use_default_colors);
#endif
    }
  CHECK (scrollok, stdscr, TRUE);
  CHECK (idlok, stdscr, TRUE);
  CHECK (noecho);
  CHECK (keypad, stdscr, TRUE);
  CHECK (cbreak);
  CHECK (nonl);
  CHECK (intrflush, NULL, FALSE);
  curs_set (1);
#ifdef NCURSES_VERSION
  set_tabsize (8);
#endif

  if (has_colors ())
    {
      // Colors
      CHECK (init_pair, RED_ON_DFL, COLOR_RED, -1);
      CHECK (init_pair, YELLOW_ON_DFL, COLOR_YELLOW, -1);
      CHECK (init_pair, BLUE_ON_DFL, COLOR_BLUE, -1);
      CHECK (init_pair, MAGENTA_ON_DFL, COLOR_MAGENTA, -1);
      CHECK (init_pair, CYAN_ON_DFL, COLOR_CYAN, -1);
      CHECK (init_pair, DFL_ON_CYAN, -1, COLOR_CYAN);
    }
}

void
signal_handler_c (int signo __unused)
{
  ctrl_c = 1;
}

void
signal_handler_z (int signo __unused)
{
  ctrl_z = 1;
}

int
main (int argc, char *argv[])
{
  int i, j;
  char *fname;

  if (system ("stty -ixon") == -1)
    {
      printf ("terminal error\n");
      return (0);
    }

  setlocale (LC_ALL, "");
  if (argc <= 1)
    {
      fputs ("usage: edlis <filename>\n", stderr);
      exit (EXIT_FAILURE);
    }
  fname = argv[1];
  signal (SIGINT, signal_handler_c);
  signal (SIGSTOP, signal_handler_z);
  signal (SIGTSTP, signal_handler_z);
  for (i = 0; i < ROW_SIZE; i++)
    for (j = 0; j < COL_SIZE; j++)
      ed_data[i][j] = NUL;
  FILE *port = fopen (fname, "r");

  ed_row = 0;
  ed_col = 0;
  ed_start = 0;
  ed_end = 0;
  ed_lparen_row = -1;
  ed_rparen_row = -1;
  ed_clip_start = -1;
  ed_clip_end = -1;
  ed_data[0][0] = EOL;
  if (port != NULL)
    {
      int c;

      c = fgetc (port);
      while (c != EOF)
	{
	  ed_data[ed_row][ed_col] = c;
	  if (c == EOL)
	    {
	      ed_row++;
	      ed_col = 0;
	      if (ed_row >= ROW_SIZE)
		printf ("row %d over max-row", ed_row);
	    }
	  else
	    {
	      ed_col++;
	      if (ed_col >= COL_SIZE)
		printf ("column %d over max-column", ed_col);
	    }
	  c = fgetc (port);
	}
      ed_end = ed_row;
      ed_data[ed_end][0] = EOL;
      fclose (port);
    }
  init_ncurses ();
  ed_scroll = LINES - 4;
  ed_footer = LINES - 1;
  ESCCLS ();
  display_command (fname);
  display_screen ();
  ed_row = ed_col = 0;
  edit_screen (fname);
  CHECK (endwin);
  if (system ("stty ixon") == -1)
    {
      printf ("terminal error\n");
    }
}

void
right ()
{
  if (ed_col == findeol (ed_row) || ed_col >= COL_SIZE)
    return;
  ed_col++;
  if (ed_col < COLS - 1)
    {
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else
    {
      if (ed_col == COLS)
	{
	  reset_paren ();
	  ESCCLSLA ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 0);
	  display_line (ed_row);
	}
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
    }
}

void
left ()
{
  if (ed_col == 0)
    return;
  ed_col--;
  if (ed_col <= COLS - 1)
    {
      if (ed_col == COLS - 1)
	{
	  reset_paren ();
	  ESCCLSLA ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 0);
	  display_line (ed_row);
	}
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else if (ed_col >= COLS)
    {
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
    }
}

void
up ()
{
  int i;

  if (ed_row == 0)
    return;
  else if (ed_clip_start != -1 && ed_row == ed_start)
    {
      if (ed_row == ed_clip_start)
	ed_clip_start--;
      else
	ed_clip_end--;
      ed_row--;
      ed_start--;
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
    }
  else if (ed_row == ed_start)
    {
      ed_row = ed_row - MIDDLE;
      ed_start = ed_start - MIDDLE;
      if (ed_row < 0)
	ed_row = ed_start = 0;
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      display_screen ();
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (2, ed_col + LEFT_MARGIN);
    }
  else if (ed_clip_start != -1)
    {
      if (ed_row == ed_clip_start)
	ed_clip_start--;
      else
	ed_clip_end--;
      ed_row--;
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      display_screen ();
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else
    {
      if (ed_col >= COLS)
	{
	  ed_col = COLS - 1;
	  ESCCLSLA ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 0);
	  display_line (ed_row);
	}
      ed_row--;
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
}

void
down ()
{
  int i;

  if (ed_row == ed_end)
    return;
  else if (ed_clip_start != -1 && ed_row == ed_start + ed_scroll)
    {
      if (ed_row == ed_clip_end)
	ed_clip_end++;
      else
	ed_clip_start++;
      ed_row++;
      ed_start++;
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
    }
  else if (ed_row == ed_start + ed_scroll)
    {
      ed_row = ed_row + MIDDLE;
      ed_start = ed_start + MIDDLE;
      if (ed_row > ed_end)
	ed_row = ed_start = ed_end - ed_scroll;
      display_screen ();
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      ESCMOVE (BOTTOM, ed_col + LEFT_MARGIN);
    }
  else if (ed_clip_start != -1)
    {
      if (ed_row == ed_clip_end)
	ed_clip_end++;
      else
	ed_clip_start++;
      ed_row++;
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      display_screen ();
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else
    {
      if (ed_col >= COLS)
	{
	  ed_col = COLS - 1;
	  ESCCLSLA ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 0);
	  display_line (ed_row);
	}
      ed_row++;
      i = findeol (ed_row);
      if (ed_col >= i)
	ed_col = i;
      restore_paren ();
      emphasis_lparen ();
      emphasis_rparen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
}

void
backspace_key ()
{
  enum HighlightToken type;

  if (ed_row == 0 && ed_col == 0)
    return;
  else if (ed_col == 0)
    {
      restore_paren ();
      deleterow ();
      if (ed_row < ed_start)
	ed_start = ed_row;
      display_screen ();
      if (ed_row < ed_start + ed_scroll)
	{
	  if (ed_col <= COLS - 1)
	    ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  else
	    ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
	}
      else
	{
	  if (ed_col <= COLS - 1)
	    ESCMOVE (21, ed_col + LEFT_MARGIN);
	  else
	    ESCMOVE (21, ed_col - COLS + LEFT_MARGIN);
	}
    }
  else if (ed_col >= COLS)
    {
      type = check_token (ed_row, ed_col - 2);
      if (type == HIGHLIGHT_MULTILINE_COMMENT)
	ed_incomment = -1;
      backspace ();
      display_screen ();
      if (ed_row < ed_start + ed_scroll)
	ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
      else
	ESCMOVE (BOTTOM, ed_col - COLS + LEFT_MARGIN);
    }
  else
    {
      type = check_token (ed_row, ed_col - 2);
      if (type == HIGHLIGHT_MULTILINE_COMMENT)
	ed_incomment = -1;
      backspace ();
      display_screen ();
      if (ed_row < ed_start + ed_scroll)
	ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      else
	ESCMOVE (BOTTOM, ed_col + LEFT_MARGIN);
    }
  modify_flag = true;
}

void
del ()
{
  if (ed_data[ed_row][ed_col] == EOL)
    return;
  ed_col++;
  backspace ();
  display_screen ();
  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
  modify_flag = true;
}

void
pageup ()
{
  ed_start = ed_start - ed_scroll;
  if (ed_start < 0)
    ed_start = 0;
  ed_row = ed_start;
  display_screen ();
  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
}

void
home ()
{
  ed_row = 0;
  ed_start = 0;
  display_screen ();
  ESCMOVE (2, ed_col + LEFT_MARGIN);
}

void
end ()
{
  ed_row = ed_end;
  if (ed_end > ed_scroll)
    ed_start = ed_row - MIDDLE;
  display_screen ();
  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
}

void
pagedn ()
{
  if (ed_end < ed_start + ed_scroll)
    return;
  ed_start = ed_start + ed_scroll;
  if (ed_start > ed_end)
    ed_start = ed_end - ed_scroll;
  ed_row = ed_start;
  display_screen ();
  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
}


char *
getname ()
{
  int c, pos;
  static char buf[20];
  pos = 0;
  buf[0] = 0;

  while (1)
    {
      CHECK (refresh);
      c = getch ();
      if (c == ERR)
	{
	  errw ("getch");
	}
      switch (c)
	{
	case RET:
	  return (buf);
	case KEY_BACKSPACE:
	case DEL:
	  if (pos > 0)
	    pos--;
	  buf[pos] = 0;
	  break;
	default:
	  if (pos > 20)
	    break;
	  else if (c < 20)
	    break;
	  buf[pos] = c;
	  pos++;
	  break;
	}
      ESCMOVE (ed_footer, 12);
      ESCREV ();
      CHECK (addstr, "                    ");
      ESCMOVE (ed_footer, 12);
      CHECK (addstr, buf);
      ESCRST ();
    }

}

void
edit_screen (char *fname)
{
  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
  bool quit = edit_loop (fname);
  while (!quit)
    {
      quit = edit_loop (fname);
    }
}

bool
edit_loop (char *fname)
{
  int c;
  int i;
  char str1[SHORT_STR_MAX], str2[SHORT_STR_MAX];
  struct position pos;
  FILE *port;

  CHECK (refresh);
  c = getch ();
  if (c == ERR)
    {
      errw ("getch");
    }
  switch (c)
    {
    case CTRL ('H'):
      ESCMOVE (2, 1);		// help
      ESCCLS1 ();
      CHECK (addstr, "Edlis help\n"
	     "CTRL+F  move to right          CTRL+S  forward search word\n"
	     "CTRL+B  move to left           CTRL+R  backward search word\n"
	     "CTRL+P  move to up             ESC TAB complete name\n"
	     "CTRL+N  move to down           ESC <   goto top page\n"
	     "CTRL+J  end of line            ESC >   goto end page\n"
	     "CTRL+A  begin of line          ESC ^   mark(or unmark) row for selection\n"
	     "CTRL+E  end of line            CTRL+D  delete one char\n"
	     "CTRL+V  page down              CTRL+O  return\n"
	     "ESC V   page up                CTRL+T  replace word\n"
	     "TAB     insert spaces according to lisp indent rule\n"
	     "CTRL+X CTRL+C quit from editor with save\n"
	     "CTRL+X CTRL+S save file\n"
	     "CTRL+X CTRL+I insert buffer from file\n"
	     "CTRL+X CTRL+W write buffer to file\n"
	     "CTRL+K  cut one line\n"
	     "CTRL+W  cut selection\n"
	     "CTRL+Y  uncut selection\n"
	     "CTRL+G  cancel command\n" "\n  enter any key to exit help\n");
      CHECK (refresh);
      CHECK (getch);
      display_screen ();
      break;
    case CTRL ('F'):
      right ();
      break;
    case CTRL ('B'):
      left ();
      break;
    case CTRL ('P'):
      up ();
      break;
    case CTRL ('N'):
      down ();
      break;
    case CTRL ('D'):
      del ();
      break;
    case CTRL ('A'):
      ed_col = 0;
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      break;
    case CTRL ('E'):
      for (i = 0; i < COL_SIZE; i++)
	{
	  if (ed_data[ed_row][i] == NUL)
	    break;
	}
      ed_col = i - 1;
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      modify_flag = true;
      break;
    case CTRL ('K'):
      ed_clip_start = ed_clip_end = ed_row;
      copy_selection ();
      delete_selection ();
      ed_row = ed_clip_start;
      ed_clip_start = ed_clip_end = -1;
      restore_paren ();
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      modify_flag = true;
      break;
    case CTRL ('W'):
      copy_selection ();
      delete_selection ();
      ed_row = ed_clip_start;
      ed_clip_start = ed_clip_end = -1;
      restore_paren ();
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      modify_flag = true;
      break;
    case CTRL ('Y'):
      paste_selection ();
      restore_paren ();
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      modify_flag = true;
      break;
    case CTRL ('X'):
      ESCMOVE (ed_footer, 1);
      ESCREV ();
      clear_status ();
      ESCMOVE (ed_footer, 1);
      CHECK (addstr, "^X");
      ESCRST ();
      while (1)
	{
	  if (ctrl_c == 1)
	    {
	      if (!modify_flag)
		{
		  ESCCLS ();
		  ESCMOVE (1, 1);
		  return true;
		}
	      else
		{
		  do
		    {
		      ESCREV ();
		      ESCMOVE (ed_footer, 1);
		      CHECK (addstr, "save modified buffer? y/n/c ");
		      CHECK (refresh);
		      c = getch ();
		      if (c == ERR)
			{
			  errw ("getch");
			}
		      ESCRST ();
		      switch (c)
			{
			case 'y':
			  save_data (fname);
			  ESCCLS ();
			  ESCMOVE (1, 1);
			  return true;
			  break;
			case 'n':
			  ESCCLS ();
			  ESCMOVE (1, 1);
			  return true;
			  break;
			case 'c':
			  clear_status ();
			  ESCRST ();
			  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
			  break;
			}
		    }
		  while (c != 'c');
		}
	    }
	  timeout (10);
	  c = getch ();
	  timeout (-1);
	  if (c == CTRL ('S'))
	    {
	      save_data (fname);
	      ESCMOVE (ed_footer, 1);
	      ESCREV ();
	      CHECK (addstr, "saved");
	      ESCRST ();
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	      modify_flag = false;
	      break;
	    }
	  else if (c == CTRL ('W'))
	    {
	      ESCMOVE (ed_footer, 1);
	      clear_status ();
	      CHECK (addstr, "filename:  ");
	      strcpy (str1, getname ());
	      save_data (str1);
	      ESCMOVE (ed_footer, 1);
	      ESCREV ();
	      CHECK (addstr, "saved ");
	      CHECK (addstr, str1);
	      ESCRST ();
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	      modify_flag = false;
	      break;
	    }
	  else if (c == CTRL ('I'))
	    {
	      clear_status ();
	      CHECK (addstr, "filename:  ");
	      strcpy (str1, getname ());
	      ESCRST ();
	      port = fopen (str1, "r");
	      if (port == NULL)
		{
		  clear_status ();
		  CHECK (addstr, str1);
		  CHECK (addstr, " doesn't exist");
		  ESCRST ();
		  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
		  break;
		}
	      c = fgetc (port);
	      while (c != EOF)
		{
		  ed_data[ed_row][ed_col] = c;
		  if (c == EOL)
		    {
		      ed_row++;
		      ed_col = 0;
		      if (ed_row >= ROW_SIZE - 1)
			{
			  CHECK (printw, "row %d over max-row", ed_row);
			}
		    }
		  else
		    {
		      ed_col++;
		      if (ed_col >= COL_SIZE)
			{
			  CHECK (printw, "column %d over max-column", ed_col);
			}
		    }
		  c = fgetc (port);
		}
	      ed_end = ed_row;
	      ed_data[ed_end][0] = EOL;
	      fclose (port);
	      display_screen ();
	      modify_flag = true;
	      break;
	    }
	  else if (c == CTRL ('G'))
	    {
	      ESCMOVE (ed_footer, 1);
	      ESCREV ();
	      clear_status ();
	      ESCRST ();
	      break;
	    }
	}
      break;
    case CTRL ('V'):
      pagedn ();
      break;
    case CTRL ('S'):
      clear_status ();
      CHECK (addstr, "search:    ");
      strcpy (str1, getname ());
      ESCRST ();
      pos = find_word (str1);
      if (pos.row == -1)
	{
	  ESCREV ();
	  ESCMOVE (ed_footer, 1);
	  CHECK (addstr, "can't find ");
	  CHECK (addstr, str1);
	  ESCRST ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  break;
	}
      ed_row = pos.row;
      ed_col = pos.col;
      ed_start = ed_row - ed_scroll / 2;
      if (ed_start < 0)
	{
	  ed_start = 0;
	}
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      break;
    case CTRL ('R'):
      clear_status ();
      CHECK (addstr, "search:    ");
      strcpy (str1, getname ());
      ESCRST ();
      pos = find_word_back (str1);
      if (pos.row == -1)
	{
	  ESCREV ();
	  ESCMOVE (ed_footer, 1);
	  CHECK (addstr, "can't find ");
	  CHECK (addstr, str1);
	  ESCRST ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  break;
	}
      ed_row = pos.row;
      ed_col = pos.col;
      ed_start = ed_row - ed_scroll / 2;
      if (ed_start < 0)
	{
	  ed_start = 0;
	}
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      break;

    case CTRL ('T'):
      clear_status ();
      CHECK (addstr, "search: ");
      CHECK (refresh);
      CHECK (getnstr, str1, SHORT_STR_MAX);
      clear_status ();
      CHECK (addstr, "replace: ");
      CHECK (refresh);
      CHECK (getnstr, str2, SHORT_STR_MAX);
      ESCRST ();
      pos = find_word (str1);
      while (pos.row != -1)
	{
	  ed_row = pos.row;
	  ed_col = pos.col;
	  ed_start = ed_row - ed_scroll / 2;
	  if (ed_start < 0)
	    {
	      ed_start = 0;
	    }
	  display_screen ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  ESCREV ();
	  CHECK (addstr, str1);
	  clear_status ();
	  do
	    {
	      CHECK (addstr, "replace? y/n ");
	      ESCRST ();
	      CHECK (refresh);
	      c = getch ();
	      if (c == ERR)
		{
		  errw ("getch");
		}
	    }
	  while (c != 'y' && c != 'n');
	  if (c == 'y')
	    {
	      ed_row = pos.row;
	      ed_col = pos.col;
	      replace_word (str1, str2);
	      display_screen ();
	      modify_flag = true;
	      ed_col++;
	    }
	  else
	    {
	      display_screen ();
	      ed_col++;
	    }
	  pos = find_word (str1);
	}
      clear_status ();
      CHECK (addstr, "can't find ");
      CHECK (addstr, str1);
      ESCRST ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      break;
    case ESC:
      ESCMOVE (ed_footer, 1);
      ESCREV ();
      clear_status ();
      ESCMOVE (ed_footer, 1);
      CHECK (addstr, "^meta");
      ESCRST ();
      CHECK (refresh);
      c = getch ();
      if (c == ERR)
	{
	  errw ("getch");
	}
      switch (c)
	{
	case '<':
	  home ();
	  break;
	case '>':
	  end ();
	  break;
	case 'v':
	  pageup ();
	  break;
	case '^':
	  if (ed_clip_start == -1)
	    {
	      ed_clip_start = ed_clip_end = ed_row;
	      ESCMOVE (ed_footer, 1);
	      ESCREV ();
	      CHECK (addstr, "marked");
	      ESCRST ();
	      return false;
	    }
	  else
	    {
	      ed_clip_start = ed_clip_end = -1;
	      display_screen ();
	      ESCMOVE (ed_footer, 1);
	      ESCREV ();
	      CHECK (addstr, "unmark");
	      ESCRST ();
	      return false;
	    }
	case TAB:
	  find_candidate ();	// completion
	  if (ed_candidate_pt == 0)
	    break;
	  else if (ed_candidate_pt == 1)
	    {
	      replace_fragment (ed_candidate[0]);
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, 0);
	      display_line (ed_row);
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	    }
	  else
	    {
	      const int CANDIDATE = 3;
	      int k = 0;
	      ESCMOVE (ed_footer, 1);
	      bool more_candidates_selected;
	      do
		{
		  more_candidates_selected = false;
		  ESCREV ();
		  for (i = 0; i < CANDIDATE; i++)
		    {
		      if (i + k >= ed_candidate_pt)
			break;
		      CHECK (printw, "%d:%s ", i + 1, ed_candidate[i + k]);
		    }
		  if (ed_candidate_pt > k + CANDIDATE)
		    CHECK (addstr, "4:more");
		  ESCRST ();
		  bool bad_candidate_selected;
		  do
		    {
		      bad_candidate_selected = false;
		      CHECK (refresh);
		      c = getch ();
		      if (c == ERR)
			{
			  errw ("getch");
			}
		      if (c != CTRL ('G'))
			{
			  i = c - '1';
			  more_candidates_selected =
			    ed_candidate_pt > k + CANDIDATE && i == CANDIDATE;
			  if (more_candidates_selected)
			    {
			      k = k + CANDIDATE;
			      ESCMVLEFT (1);
			      ESCCLSL ();
			      break;
			    }
			  bad_candidate_selected =
			    i + k > ed_candidate_pt || i < 0 || c == RET;
			}
		      else
			{
			  ESCMOVE (ed_footer, 1);
			  ESCREV ();
			  clear_status ();
			  ESCRST ();
			  return false;
			}
		    }
		  while (bad_candidate_selected);
		}
	      while (more_candidates_selected);
	      if (c != ESC)
		replace_fragment (ed_candidate[i + k]);
	      display_screen ();
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	    }
	  return false;
	case CTRL ('G'):
	  ESCMOVE (ed_footer, 1);
	  ESCREV ();
	  clear_status ();
	  ESCRST ();
	  break;
	}
      break;
    case KEY_UP:
      up ();
      break;
    case KEY_DOWN:
      down ();
      break;
    case KEY_LEFT:
      left ();
      break;
    case KEY_RIGHT:
      right ();
      break;
    case KEY_HOME:
      home ();
      break;
    case KEY_END:
      end ();
      break;
    case KEY_IC:
      ed_ins = !ed_ins;
      break;
    case KEY_PPAGE:
      pageup ();
      break;
    case KEY_NPAGE:
      pagedn ();
      break;
    case KEY_DC:
      del ();
      break;
    case KEY_BACKSPACE:
    case DEL:
      backspace_key ();
      break;
    case RET:
    case CTRL ('O'):
      if (ed_indent == 1)
	i = calc_tabs ();
      if (ed_row == ed_start + ed_scroll)
	{
	  restore_paren ();
	  insertrow ();
	  ed_start++;
	  ed_row++;
	  ed_end++;
	  ed_col = 0;
	  display_screen ();
	  ESCMOVE (BOTTOM, LEFT_MARGIN);
	}
      else if (ed_col >= COLS)
	{
	  restore_paren ();
	  insertrow ();
	  ed_start++;
	  ed_row++;
	  ed_end++;
	  ed_col = 0;
	  display_screen ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
	}
      else
	{
	  restore_paren ();
	  insertrow ();
	  ed_row++;
	  ed_end++;
	  ed_col = 0;
	  display_screen ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
	}
      if (ed_indent == 1)
	{
	  ed_col = 0;
	  remove_headspace (ed_row);
	  softtabs (i);
	  display_screen ();
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	}
      modify_flag = true;
      break;
    case TAB:
      if (ed_tab == 0)
	{
	  ed_col = 0;
	  i = calc_tabs ();
	  remove_headspace (ed_row);
	  softtabs (i);
	}
      else
	{
	  softtabs (ed_tab);
	}
      display_screen ();
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
      modify_flag = true;
      break;
    default:
      if (ed_ins)
	{
	  if (ed_col >= COL_SIZE)
	    break;
	  else if (ed_col >= COLS)
	    {
	      ESCCLSLA ();
	      restore_paren ();
	      insertcol ();
	      ed_data[ed_row][ed_col] = c;
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
	      display_line (ed_row);
	      emphasis_lparen ();
	      emphasis_rparen ();
	      ed_col++;
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
	    }
	  else
	    {
	      restore_paren ();
	      insertcol ();
	      ed_data[ed_row][ed_col] = c;
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, 1);
	      display_line (ed_row);
	      emphasis_lparen ();
	      emphasis_rparen ();
	      ed_col++;
	      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	    }
	}
      else
	{
	  if (ed_col >= COL_SIZE)
	    break;
	  else if (ed_col >= COLS)
	    {
	      if (ed_col == COLS)
		ESCCLSLA ();
	      ed_data[ed_row][ed_col] = c;
	      CHECK (addch, c);
	      emphasis_lparen ();
	      ed_col++;
	    }
	  else
	    {
	      ed_data[ed_row][ed_col] = c;
	      CHECK (addch, c);
	      emphasis_lparen ();
	      ed_col++;
	    }
	}
      modify_flag = true;
    }
  return false;
}

void
display_command (char *fname)
{
  int i;
  ESCHOME ();
  ESCREV ();
  CHECK (printw, "Edlis %1.2f        File: %s    ", VERSION, fname);
  for (i = 31; i < COLS; i++)
    CHECK (addch, ' ');
  ESCRST ();
}

void
display_screen ()
{
  int line1, line2, i;

  ESCTOP ();
  ESCCLS1 ();
  line1 = ed_start;
  line2 = ed_start + ed_scroll;
  if (line2 > ed_end)
    line2 = ed_end;

  while (line1 <= line2)
    {
      display_line (line1);
      line1++;
    }
  ESCMOVE (ed_footer, 1);
  ESCREV ();
  for (i = 0; i < COLS - 31; i++)
    CHECK (addch, ' ');
  CHECK (addstr, "^H(help) ^X^C(quit) ^X^S(save)");
  ESCRST ();
}

void
display_line (int line)
{
  int col;
  char linestr[10];

  sprintf(linestr,"% 5d ",line);
  CHECK(addstr,linestr);

  if (ed_row != line || ed_col <= COLS - 1)
    col = 0;
  else
    col = COLS;

  while (((ed_col <= COLS - 1 && col <= COLS - 1)
	  || (ed_col >= COLS && col < COL_SIZE))
	 && ed_data[line][col] != EOL && ed_data[line][col] != NUL)
    {
      if (line >= ed_clip_start && line <= ed_clip_end)
	ESCREV ();
      else
	ESCRST ();

      if (ed_incomment != -1 && line >= ed_incomment)
	{			// comment 
	  // 
	  // #|...|#
	  ESCBOLD ();
	  setcolor (ed_comment_color);
	  while (((ed_col <= COLS - 1 && col <= COLS - 1)
		  || (ed_col >= COLS && col < COL_SIZE))
		 && ed_data[line][col] != EOL && ed_data[line][col] != NUL)
	    {
	      CHECK (addch, ed_data[line][col]);
	      col++;
	      if (ed_data[line][col - 2] == '|' &&
		  ed_data[line][col - 1] == '#')
		{
		  ed_incomment = -1;
		  ESCRST ();
		  ESCFORG ();
		  break;
		}
	    }
	  ESCRST ();
	  ESCFORG ();
	}
      else if (ed_data[line][col] == ' ' ||
	       ed_data[line][col] == '(' || ed_data[line][col] == ')')
	{
	  CHECK (addch, ed_data[line][col]);
	  col++;
	}
      else
	{
	  switch (check_token (line, col))
	    {
	    case HIGHLIGHT_SYNTAX:
	      ESCBOLD ();
	      setcolor (ed_syntax_color);
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != ' '
		     && ed_data[line][col] != '('
		     && ed_data[line][col] != ')'
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		}
	      ESCRST ();
	      ESCFORG ();
	      break;
	    case HIGHLIGHT_BUILTIN:
	      ESCBOLD ();
	      setcolor (ed_builtin_color);
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != ' '
		     && ed_data[line][col] != '('
		     && ed_data[line][col] != ')'
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		}
	      ESCRST ();
	      ESCFORG ();
	      break;
	    case HIGHLIGHT_STRING:
	      ESCBOLD ();
	      setcolor (ed_string_color);
	      CHECK (addch, ed_data[line][col]);
	      col++;
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		  if (ed_data[line][col - 1] == '"')
		    break;
		}
	      ESCRST ();
	      ESCFORG ();
	      break;
	    case HIGHLIGHT_COMMENT:
	      ESCBOLD ();
	      setcolor (ed_comment_color);
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		}
	      ESCRST ();
	      ESCFORG ();
	      break;
	    case HIGHLIGHT_EXTENDED:
	      ESCBOLD ();
	      setcolor (ed_extended_color);
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != ' '
		     && ed_data[line][col] != '('
		     && ed_data[line][col] != ')'
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		}
	      ESCRST ();
	      ESCFORG ();
	      break;
	    case HIGHLIGHT_MULTILINE_COMMENT:
	      ESCBOLD ();
	      setcolor (ed_comment_color);
	      ed_incomment = line;
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != EOL
		     && ed_data[line][col] != NUL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		  if (ed_data[line][col - 2] == '|' &&
		      ed_data[line][col - 1] == '#')
		    {
		      ed_incomment = -1;
		      ESCRST ();
		      ESCFORG ();
		      break;
		    }
		}
	      break;
	    default:
	      while (((ed_col <= COLS - 1 && col <= COLS - 1)
		      || (ed_col >= COLS && col < COL_SIZE))
		     && ed_data[line][col] != ' '
		     && ed_data[line][col] != '('
		     && ed_data[line][col] != ')'
		     && ed_data[line][col] != NUL
		     && ed_data[line][col] != EOL)
		{
		  CHECK (addch, ed_data[line][col]);
		  col++;
		}
	    }
	}
    }
  CHECK (addch, EOL);
  ESCRST ();
}

void
setcolor (enum Color n)
{
  if (has_colors ())
    {
      CHECK (color_set, n, NULL);
    }
}

void
backspace ()
{
  int i;

  if (ed_data[ed_row][ed_col - 1] == ')')
    {
      ed_lparen_row = -1;
      ed_rparen_row = -1;
    }
  i = ed_col;
  while (i < COL_SIZE)
    {
      ed_data[ed_row][i - 1] = ed_data[ed_row][i];
      i++;
    }
  ed_col--;
}

void
insertcol ()
{
  int i;

  i = findeol (ed_row);
  while (i >= ed_col)
    {
      ed_data[ed_row][i + 1] = ed_data[ed_row][i];
      i--;
    }
}

void
insertrow ()
{
  int i, j, k;

  for (i = ed_end; i >= ed_row; i--)
    {
      for (j = 0; j < COL_SIZE; j++)
	{
	  ed_data[i + 1][j] = ed_data[i][j];
	}
    }
  k = 0;
  for (j = ed_col; j < COL_SIZE; j++)
    {
      ed_data[ed_row + 1][k] = ed_data[ed_row][j];
      k++;
    }
  ed_data[ed_row][ed_col] = EOL;
}

void
deleterow ()
{
  int i, j, k, l;

  k = l = findeol (ed_row - 1);
  for (j = 0; j < COL_SIZE; j++)
    {
      ed_data[ed_row - 1][k] = ed_data[ed_row][j];
      k++;
      if (ed_data[ed_row][j] == EOL)
	break;
    }

  for (i = ed_row; i < ed_end; i++)
    {
      for (j = 0; j < COL_SIZE; j++)
	{
	  ed_data[i][j] = ed_data[i + 1][j];
	}
    }
  ed_row--;
  ed_end--;
  ed_col = l;
}



int
findeol (int row)
{
  int i;

  for (i = 0; i < COL_SIZE; i++)
    {
      if (ed_data[row][i] == EOL)
	return (i);
    }
  return (-1);
}

struct position
findlparen (int bias)
{
  int nest, row, col, limit;
  struct position pos;

  row = ed_row;
  if (ed_col != 0)
    col = ed_col - bias;
  else
    {
      row--;
      if (row < 0)
	{
	  pos.col = 0;
	}
      col = findeol (row);
    }

  nest = 0;
  limit = ed_row - ed_scroll;
  if (limit < 0)
    limit = 0;

  while (row >= limit)
    {
      if (ed_data[row][col] == '(' && nest == 0)
	break;
      else if (ed_data[row][col] == ')')
	nest++;
      else if (ed_data[row][col] == '(')
	nest--;


      if (col == 0)
	{
	  row--;
	  col = findeol (row);
	}
      else
	{
	  col--;
	}
    }
  if (row >= limit)
    {
      pos.row = row;
      pos.col = col;
    }
  else
    {
      pos.row = -1;
      pos.col = 0;
    }
  return (pos);
}

struct position
findrparen (int bias)
{
  int nest, row, col, limit;
  struct position pos;

  row = ed_row;
  col = ed_col + bias;
  nest = 0;
  limit = ed_row + ed_scroll;
  if (limit > ed_end)
    limit = ed_end;

  while (row < limit)
    {
      if (ed_data[row][col] == ')' && nest == 0)
	break;
      else if (ed_data[row][col] == '(')
	nest++;
      else if (ed_data[row][col] == ')')
	nest--;


      if (col == findeol (row))
	{
	  row++;
	  col = 0;
	}
      else
	{
	  col++;
	}
    }
  if (row < limit)
    {
      pos.row = row;
      pos.col = col;
    }
  else
    {
      pos.row = -1;
      pos.col = 0;
    }
  return (pos);
}

void
reset_paren ()
{
  ed_lparen_row = -1;
  ed_rparen_row = -1;
}

void
restore_paren ()
{
  if (ed_lparen_row != -1 && ed_lparen_row >= ed_start
      && ed_lparen_row <= ed_start + ed_scroll)
    {
      if (ed_lparen_col <= COLS - 1)
	ESCMOVE (ed_lparen_row + TOP_MARGIN - ed_start, ed_lparen_col + LEFT_MARGIN);
      else
	ESCMOVE (ed_lparen_row + TOP_MARGIN - ed_start, ed_lparen_col - COLS + LEFT_MARGIN);
      ESCBORG ();
      CHECK (addch, '(');
      ed_lparen_row = -1;
    }
  if (ed_rparen_row != -1 && ed_rparen_row >= ed_start
      && ed_rparen_row <= ed_start + ed_scroll)
    {
      if (ed_rparen_col <= COLS - 1)
	ESCMOVE (ed_rparen_row + TOP_MARGIN - ed_start, ed_rparen_col + LEFT_MARGIN);
      else
	ESCMOVE (ed_rparen_row + TOP_MARGIN - ed_start, ed_rparen_col - COLS + LEFT_MARGIN);
      ESCBORG ();
      CHECK (addch, ')');
      ed_rparen_row = -1;
    }
}

void
emphasis_lparen ()
{
  struct position pos;

  if (ed_data[ed_row][ed_col] != ')')
    return;

  pos = findlparen (1);
  if (ed_col <= COLS - 1 && pos.col <= COLS - 1)
    {
      if (pos.row != -1)
	{
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  ESCBCYAN ();
	  CHECK (addch, ')');
	  ESCBORG ();
	  if (pos.row >= ed_start)
	    {
	      ESCMOVE (pos.row + TOP_MARGIN - ed_start, pos.col + LEFT_MARGIN);
	      ESCBCYAN ();
	      CHECK (addch, '(');
	    }
	  ed_lparen_row = pos.row;
	  ed_lparen_col = pos.col;
	  ed_rparen_row = ed_row;
	  ed_rparen_col = ed_col;
	  ESCBORG ();
	}
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else if (ed_col >= COLS && pos.col >= COLS)
    {
      if (pos.row != -1)
	{
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
	  ESCBCYAN ();
	  CHECK (addch, ')');
	  ESCBORG ();
	  if (pos.row >= ed_start)
	    {
	      ESCMOVE (pos.row + TOP_MARGIN - ed_start, pos.col - COLS + LEFT_MARGIN);
	      ESCBCYAN ();
	      CHECK (addch, '(');
	    }
	  ed_lparen_row = pos.row;
	  ed_lparen_col = pos.col;
	  ed_rparen_row = ed_row;
	  ed_rparen_col = ed_col;
	  ESCBORG ();
	}
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
    }
}

void
emphasis_rparen ()
{
  struct position pos;

  if (ed_data[ed_row][ed_col] != '(')
    return;

  pos = findrparen (1);
  if (ed_col <= COLS - 1 && pos.col <= COLS - 1)
    {
      if (pos.row != -1)
	{
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
	  ESCBCYAN ();
	  CHECK (addch, '(');
	  ESCBORG ();
	  if (pos.row <= ed_start + ed_scroll)
	    {
	      ESCMOVE (pos.row + TOP_MARGIN - ed_start, pos.col + LEFT_MARGIN);
	      ESCBCYAN ();
	      CHECK (addch, ')');
	    }
	  ed_rparen_row = pos.row;
	  ed_rparen_col = pos.col;
	  ed_lparen_row = ed_row;
	  ed_lparen_col = ed_col;
	  ESCBORG ();
	}
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col + LEFT_MARGIN);
    }
  else if (ed_col >= COLS && pos.col >= COLS)
    {
      if (pos.row != -1)
	{
	  ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
	  ESCBCYAN ();
	  CHECK (addch, '(');
	  ESCBORG ();
	  if (pos.row <= ed_start + ed_scroll)
	    {
	      ESCMOVE (pos.row + TOP_MARGIN - ed_start, pos.col - COLS + LEFT_MARGIN);
	      ESCBCYAN ();
	      CHECK (addch, ')');
	    }
	  ed_rparen_row = pos.row;
	  ed_rparen_col = pos.col;
	  ed_lparen_row = ed_row;
	  ed_lparen_col = ed_col;
	  ESCBORG ();
	}
      ESCMOVE (ed_row + TOP_MARGIN - ed_start, ed_col - COLS + LEFT_MARGIN);
    }
}


void
softtabs (int n)
{
  while (n > 0)
    {
      insertcol ();
      ed_data[ed_row][ed_col] = ' ';
      ed_col++;
      n--;
    }
}


void
save_data (char *fname)
{
  int row, col;

  FILE *port = fopen (fname, "w");
  for (row = 0; row <= ed_end; row++)
    for (col = 0; col < COL_SIZE; col++)
      {
	fputc (ed_data[row][col], port);
	if (ed_data[row][col] == EOL)
	  break;
      }
  fclose (port);
}

bool
is_special (int row, int col)
{
  char str[TOKEN_MAX];
  int pos;

  pos = 0;
  while (ed_data[row][col] != ' ' &&
	 ed_data[row][col] != '(' && ed_data[row][col] >= ' ')
    {
      str[pos] = ed_data[row][col];
      col++;
      pos++;
    }
  str[pos] = NUL;
  if (pos == 0)
    return false;
  return in_special_table (str);
}

int
findnext (int row, int col)
{
  if (ed_data[row][col] == '(')
    return (col);
  else
    {
      while (ed_data[row][col] != ' ' && ed_data[row][col] > ' ')
	col++;

      while (ed_data[row][col] == ' ')
	col++;
    }
  return (col);
}


void
remove_headspace (int row __unused)
{
  int col, i, j, k;

  col = 0;
  while (ed_data[ed_row][col] == ' ')
    col++;
  k = findeol (ed_row);
  if (k == -1)			// can't find
    k = 0;
  i = 0;
  for (j = col; j <= k; j++)
    {
      ed_data[ed_row][i] = ed_data[ed_row][j];
      i++;
    }
}

int
calc_tabs ()
{
  struct position pos;

  pos = findlparen (0);

  if (ed_data[ed_row][ed_col] == '(')
    return (0);

  if (pos.row == -1)
    return (0);			// can't find left paren

  if (is_special (pos.row, pos.col + 1))
    return (pos.col + 4);
  else
    return (findnext (pos.row, pos.col + 1));

  return (0);			// dummy
}

void
copy_selection ()
{
  int i, j, k;

  if (ed_clip_end - ed_clip_start > COPY_SIZE)
    return;

  j = 0;
  for (i = ed_clip_start; i <= ed_clip_end; i++)
    {
      for (k = 0; k < COLS; k++)
	ed_copy[j][k] = ed_data[i][k];
      j++;
    }
  ed_copy_end = j;
}

void
paste_selection ()
{
  int i, j, k;

  if (ed_copy_end == 0)
    return;
  if (ed_end + ed_copy_end > 1000)
    return;

  for (i = ed_end; i >= ed_row; i--)
    {
      for (j = 0; j < COL_SIZE; j++)
	{
	  ed_data[i + ed_copy_end][j] = ed_data[i][j];
	}
    }
  ed_end = ed_end + ed_copy_end;
  ed_data[ed_end][0] = EOL;

  k = ed_row;
  for (i = 0; i < ed_copy_end; i++)
    {
      for (j = 0; j < COL_SIZE; j++)
	{
	  ed_data[k][j] = ed_copy[i][j];
	}
      k++;
    }
}

void
delete_selection ()
{
  int i, j, k;

  if (ed_clip_start == -1)
    return;
  k = ed_clip_end - ed_clip_start + 1;
  for (i = ed_clip_start; i <= ed_end; i++)
    {
      for (j = 0; j < COL_SIZE; j++)
	{
	  ed_data[i][j] = ed_data[i + k][j];
	}
    }
  ed_end = ed_end - k;
  ed_data[ed_end][0] = EOL;
}

enum HighlightToken
check_token (int row, int col)
{
  char str[COLS];
  int pos;

  pos = 0;
  if (ed_data[row][col] == '"')
    return HIGHLIGHT_STRING;
  else if (ed_data[row][col] == ';')
    return HIGHLIGHT_COMMENT;
  while (ed_data[row][col] != ' ' &&
	 ed_data[row][col] != '(' &&
	 ed_data[row][col] != ')' &&
	 ed_data[row][col] != NUL && ed_data[row][col] != EOL)
    {
      str[pos] = ed_data[row][col];
      col++;
      pos++;
    }
  str[pos] = NUL;
  if (pos == 0)
    return HIGHLIGHT_NONE;
  else if (str[0] == '#' && str[1] == '|')
    return HIGHLIGHT_MULTILINE_COMMENT;	// #|...|#
  return maybe_match (str);
}

char *
get_fragment ()
{
  static char str[TOKEN_MAX];
  int col, pos;

  col = ed_col - 1;
  while (col >= 0 &&
	 ed_data[ed_row][col] != ' ' &&
	 ed_data[ed_row][col] != '(' && ed_data[ed_row][col] != ')')
    {
      col--;
    }
  col++;
  pos = 0;
  while (ed_data[ed_row][col] != ' ' &&
	 ed_data[ed_row][col] != '(' && ed_data[ed_row][col] >= ' ')
    {
      str[pos] = ed_data[ed_row][col];
      col++;
      pos++;
    }
  str[pos] = NUL;
  return (str);
}

void
find_candidate ()
{
  char *str;

  str = get_fragment ();
  ed_candidate_pt = 0;
  if (str[0] != NUL)
    gather_fuzzy_matches (str, ed_candidate, &ed_candidate_pt);
}

void
replace_fragment (const char *newstr)
{
  char *oldstr;
  int m, n;

  oldstr = get_fragment ();
  m = strlen (oldstr);
  n = strlen (newstr);
  while (m > 0)
    {
      backspace ();
      m--;
    }
  while (n > 0)
    {
      insertcol ();
      ed_data[ed_row][ed_col] = *newstr;
      ed_col++;
      newstr++;
      n--;
    }
}

struct position
find_word (const char *word)
{
  int i, j, k, len;
  struct position pos;
  const char *word1;

  i = ed_row;
  j = ed_col;
  word1 = word;
  len = strlen (word);
  while (i <= ed_end + 1)
    {
      while (j < COL_SIZE && ed_data[i][j] != NUL)
	{
	  k = j;
	  while (k < j + len && ed_data[i][k] == *word)
	    {
	      word++;
	      k++;
	    }
	  if (k >= j + len)
	    {
	      pos.row = i;
	      pos.col = j;
	      return (pos);
	    }
	  j++;
	  word = word1;
	}
      i++;
      j = 0;
    }
  // can't find word
  pos.row = -1;
  pos.col = 0;
  return (pos);
}

struct position
find_word_back (const char *word)
{
  int i, j, k, len;
  struct position pos;
  const char *word1;

  i = ed_row;
  j = ed_col;
  word1 = word;
  len = strlen (word);
  while (i >= 0)
    {
      while (j < COL_SIZE && ed_data[i][j] != NUL)
	{
	  k = j;
	  while (k < j + len && ed_data[i][k] == *word)
	    {
	      word++;
	      k++;
	    }
	  if (k >= j + len)
	    {
	      pos.row = i;
	      pos.col = j;
	      return (pos);
	    }
	  j++;
	  word = word1;
	}
      i--;
      j = 0;
    }
  // can't find word
  pos.row = -1;
  pos.col = 0;
  return (pos);
}


void
replace_word (const char *str1, const char *str2)
{
  int len1, len2, i, j;

  len1 = strlen (str1);
  len2 = strlen (str2);

  if (len1 == len2)
    {
      for (i = 0; i < len1; i++)
	{
	  ed_data[ed_row][ed_col] = *str2;
	  ed_col++;
	  str2++;
	}
    }
  else if (len1 > len2)
    {
      i = ed_col + len1;
      j = len1 - len2;
      while (ed_data[ed_row][i] != NUL)
	{
	  ed_data[ed_row][i - j] = ed_data[ed_row][i];
	  i++;
	}
      ed_data[ed_row][i] = NUL;

      for (i = 0; i < len2; i++)
	{
	  ed_data[ed_row][ed_col + i] = *str2;
	  str2++;
	}
    }
  else
    {				// len1 < len2
      i = findeol (ed_row);
      j = len2 - len1;
      while (i >= ed_col + len1)
	{
	  ed_data[ed_row][i + j] = ed_data[ed_row][i];
	  i--;
	}
      ed_data[ed_row][i] = NUL;
      for (i = 0; i < len2; i++)
	{
	  ed_data[ed_row][ed_col + i] = *str2;
	  str2++;
	}
    }
}
