/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	$Id: menu.c,v 1.93 2007-10-26 07:55:45 dkure Exp $

*/

#ifndef _WIN32
#include <dirent.h>
#include <sys/stat.h>
#endif
#include "quakedef.h"
#ifdef GLQUAKE
#include "gl_model.h"
#include "gl_local.h"
#endif
#ifndef CLIENTONLY
#include "server.h"
#endif
#include "menu.h"
#include "mp3_player.h"
#include "EX_browser.h"
#include "menu_demo.h"
#include "menu_proxy.h"
#include "menu_options.h"
#include "menu_ingame.h"
#include "EX_FileList.h"
#include "help.h"
#include "utils.h"
#include "qsound.h"
#include "keys.h"

qbool vid_windowedmouse = true;
void (*vid_menudrawfn)(void);
void (*vid_menukeyfn)(int key);
void CL_Disconnect_f(void);

extern cvar_t con_shift;

void M_Menu_Main_f (void);
	void M_Menu_SinglePlayer_f (void);
		void M_Menu_Load_f (void);
		void M_Menu_Save_f (void);
	void M_Menu_MultiPlayer_f (void);
		void M_Menu_ServerList_f (void);
			void M_Menu_SEdit_f (void);
		void M_Menu_Demos_f (void);
		void M_Menu_GameOptions_f (void);
	void M_Menu_Options_f (void);
	void M_Menu_MP3_Control_f (void);
	void M_Menu_Quit_f (void);

void M_Main_Draw (void);
	void M_SinglePlayer_Draw (void);
		void M_Load_Draw (void);
		void M_Save_Draw (void);
	void M_MultiPlayer_Draw (void);
		void M_ServerList_Draw (void);
			void M_SEdit_Draw (void);
		void M_Demo_Draw (void);
		void M_GameOptions_Draw (void);
		void M_Proxy_Draw (void);
	void M_Options_Draw (void);
	void M_Help_Draw (void);
	void M_Quit_Draw (void);

void M_Main_Key (int key);
	void M_SinglePlayer_Key (int key);
		void M_Load_Key (int key);
		void M_Save_Key (int key);
	void M_MultiPlayer_Key (int key);
		void M_ServerList_Key (int key);
			void M_SEdit_Key (int key);
		void M_Demo_Key (int key);
		void M_GameOptions_Key (int key);
		void M_Proxy_Key (int key);
	void M_Options_Key (int key, int unichar);
	void M_Help_Key (int key);
	void M_Quit_Key (int key);


int FindBestNick (char *s,int use);

m_state_t m_state;

qbool    m_entersound;          // play after drawing a frame, so caching
                                // won't disrupt the sound
qbool    m_recursiveDraw;
int            m_topmenu;       // set if a submenu was entered via a
                                // menu_* command
#define    SLIDER_RANGE    10

typedef struct menu_window_s {
	int x;
	int y;
	int w;
	int h;
} menu_window_t;

//=============================================================================
/* Support Routines */

#ifdef GLQUAKE
cvar_t     scr_scaleMenu = {"scr_scaleMenu","1"};
int        menuwidth = 320;
int        menuheight = 240;
#else
#define menuwidth vid.width
#define menuheight vid.height
#endif

cvar_t     scr_centerMenu = {"scr_centerMenu","1"};
int        m_yofs = 0;

void M_DrawCharacter (int cx, int line, int num) {
	Draw_Character (cx + ((menuwidth - 320)>>1), line + m_yofs, num);
}

void M_Print_GetPoint(int cx, int cy, int *rx, int *ry, const char *str, qbool red) {
	cx += ((menuwidth - 320)>>1);
	cy += m_yofs;
	*rx = cx;
	*ry = cy;
	if (red)
		Draw_Alt_String (cx, cy, str);
	else
		Draw_String(cx, cy, str);
}

void M_Print (int cx, int cy, char *str) {
	int rx, ry;
	M_Print_GetPoint(cx, cy, &rx, &ry, str, true);
}

void M_PrintWhite (int cx, int cy, char *str) {
	Draw_String (cx + ((menuwidth - 320)>>1), cy + m_yofs, str);
}

// replacement of M_DrawTransPic - sometimes we need the real coordinate of the pic
static void M_DrawTransPic_GetPoint (int x, int y, int *rx, int *ry, mpic_t *pic)
{
	*rx = x + ((menuwidth - 320)>>1);
	*ry = y + m_yofs;
	Draw_TransPic (x + ((menuwidth - 320)>>1), y + m_yofs, pic);
}

void M_DrawTransPic (int x, int y, mpic_t *pic) {
	int tx, ty;
	M_DrawTransPic_GetPoint(x, y, &tx, &ty, pic);
}

void M_DrawPic (int x, int y, mpic_t *pic) {
	Draw_Pic (x + ((menuwidth - 320)>>1), y + m_yofs, pic);
}

void M_DrawTextBox (int x, int y, int width, int lines) {
	Draw_TextBox (x + ((menuwidth - 320) >> 1), y + m_yofs, width, lines);
}

void M_DrawSlider (int x, int y, float range) {
	int    i;

	range = bound(0, range, 1);
	M_DrawCharacter (x-8, y, 128);
	for (i=0 ; i<SLIDER_RANGE ; i++)
		M_DrawCharacter (x + i*8, y, 129);
	M_DrawCharacter (x+i*8, y, 130);
	M_DrawCharacter (x + (SLIDER_RANGE-1)*8 * range, y, 131);
}

void M_DrawCheckbox (int x, int y, int on) {
	if (on)
		M_Print (x, y, "on");
	else
		M_Print (x, y, "off");
}

qbool Key_IsLeftRightSameBind(int b);

void M_FindKeysForCommand (const char *command, int *twokeys) {
	int count, j, l;
	char *b;

	twokeys[0] = twokeys[1] = -1;
	l = strlen(command) + 1;
	count = 0;

	for (j = 0 ; j < (sizeof(keybindings) / sizeof(*keybindings)); j++) {
		b = keybindings[j];
		if (!b)
			continue;
		if (!strncmp (b, command, l)) {
			if (count) {
				if (j == twokeys[0] + 1 && (twokeys[0] == K_LCTRL || twokeys[0] == K_LSHIFT || twokeys[0] == K_LALT)) {

					twokeys[0]--;
					continue;
				}
			}
			twokeys[count] = j;
			count++;
			if (count == 2) {

				if (Key_IsLeftRightSameBind(twokeys[1]))
					twokeys[1]++;
				break;
			}
		}
	}
}

void M_Unscale_Menu(void)
{
#ifdef GLQUAKE
	// do not scale this menu
	if (scr_scaleMenu.value) {
		menuwidth = vid.width;
		menuheight = vid.height;
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho  (0, menuwidth, menuheight, 0, -99999, 99999);
	}
#endif
}

// will apply menu scaling effect for given window region
// scr_scaleMenu 1 uses glOrtho function and we use the same algorithm in here
static void M_Window_Adjust(const menu_window_t *original, menu_window_t *scaled)
{
#ifdef GLQUAKE
	double sc_x, sc_y; // scale factors

	if (scr_scaleMenu.value)
	{
		sc_x = (double) vid.width / (double) menuwidth;
		sc_y = (double) vid.height / (double) menuheight;
		scaled->x = original->x * sc_x;
		scaled->y = original->y * sc_y;
		scaled->w = original->w * sc_x;
		scaled->h = original->h * sc_y;
	}
	else
	{
		memcpy(scaled, original, sizeof(menu_window_t));
	}
#else
	memcpy(scaled, original, sizeof(menu_window_t));
#endif
}

// this function will look at window borders and current mouse cursor position
// and will change which item in the window should be selected
// we presume that all entries have same height and occupy the whole window
// 1st par: input, window position & size
// 2nd par: input, mouse position
// 3rd par: input, how many entries does the window have
// 4th par: output, newly selected entry, first entry is 0, second 1, ...
// return value: does the cursor belong to this window? yes/no
static qbool M_Mouse_Select(const menu_window_t *uw, const mouse_state_t *m, int entries, int *newentry)
{
	double entryheight;
	double nentry;
	menu_window_t rw;
	menu_window_t *w = &rw; // just a language "shortcut"
	
	M_Window_Adjust(uw, w);

	// window is invisible
	if (!(w->h > 0) || !(w->w > 0)) return false;

	// check if the pointer is inside of the window
	if (m->x < w->x || m->y < w->y || m->x > w->x + w->w || m->y > w->y + w->h)
		return false; // no, it's not

	entryheight = w->h / entries;
	nentry = (int) (m->y - w->y) / (int) entryheight;
	
	*newentry = bound(0, nentry, entries-1);

	return true;
}

//=============================================================================

void M_EnterMenu (int state) {
	if (key_dest != key_menu) {
		m_topmenu = state;
		if (state != m_proxy) {
			Con_ClearNotify ();
			// hide the console
			scr_conlines = 0;
			scr_con_current = 0;
		}
	} else {
		m_topmenu = m_none;
	}

	key_dest = key_menu;
	m_state = state;
	m_entersound = true;
}

static void M_ToggleHeadMenus(int type)
{
	m_entersound = true;

	if (key_dest == key_menu) {
		if (m_state != type) {
			M_EnterMenu(type);
			return;
		}
		key_dest = key_game;
		m_state = m_none;
	} else {
		M_EnterMenu(type);
	}
}

void M_ToggleMenu_f (void) {
	if (cls.state == ca_active) {
		if (cls.demoplayback || cls.mvdplayback)
			M_ToggleHeadMenus(m_democtrl);
		else
			M_ToggleHeadMenus(m_ingame);
	}
	else M_ToggleHeadMenus(m_main);
}

void M_EnterProxyMenu () {
	M_EnterMenu(m_proxy);
}

void M_LeaveMenu (int parent) {
	if (m_topmenu == m_state) {
		m_state = m_none;
		key_dest = key_game;
	} else {
		m_state = parent;
		m_entersound = true;
	}
}

// dunno how to call this function
// must leave all menus instead of calling few functions in row
void M_LeaveMenus (void) {
//	m_entersound = true; // fixme: which value we must set ???

	m_topmenu = m_none;
	m_state   = m_none;
	key_dest  = key_game;
}

void M_ToggleProxyMenu_f (void) {
	// this is what user has bound to a key - that means 
	// when in proxy menu -> turn it off; and vice versa
	m_entersound = true;

	if ((key_dest == key_menu) && (m_state == m_proxy)) {
		M_LeaveMenus();
		Menu_Proxy_Toggle();
	} else {
		M_EnterMenu (m_proxy);
		Menu_Proxy_Toggle();
	}
}

//=============================================================================
/* MAIN MENU */

int    m_main_cursor;
#define    MAIN_ITEMS    5
menu_window_t m_main_window;

void M_Menu_Main_f (void) {
	M_EnterMenu (m_main);
}

void M_Main_Draw (void) {
	int f;
	mpic_t *p;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );

	// the Main Manu heading
	p = Draw_CachePic ("gfx/ttl_main.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	// Main Menu items
	p = Draw_CachePic ("gfx/mainmenu.lmp");
	m_main_window.w = p->width;
	m_main_window.h = p->height;
	M_DrawTransPic_GetPoint (72, 32, &m_main_window.x, &m_main_window.y, p);
	
	// main menu specific correction, mainmenu.lmp|png have some useless extra space at the bottom
	// that makes the mouse pointer position calculation imperfect
	m_main_window.h *= 0.9;

	f = (int)(curtime * 10)%6;

	M_DrawTransPic (54, 32 + m_main_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );
}

void M_Main_Key (int key) {
	extern cvar_t cl_confirmquit;
	switch (key) {
	case K_ESCAPE:
	case K_MOUSE2:
		key_dest = key_game;
		m_state = m_none;
		break;

	case '`':
	case '~':
		key_dest = key_console;
		m_state = m_none;
		break;

	case K_UPARROW:
	case K_MWHEELUP:
		S_LocalSound ("misc/menu1.wav");
		if (--m_main_cursor < 0)
			m_main_cursor = MAIN_ITEMS - 1;
		break;

	case K_DOWNARROW:
	case K_MWHEELDOWN:
		S_LocalSound ("misc/menu1.wav");
		if (++m_main_cursor >= MAIN_ITEMS)
			m_main_cursor = 0;
		break;

	case K_HOME:
	case K_PGUP:
		S_LocalSound ("misc/menu1.wav");
		m_main_cursor = 0;
		break;

	case K_END:
	case K_PGDN:
		S_LocalSound ("misc/menu1.wav");
		m_main_cursor = MAIN_ITEMS - 1;
		break;

	case K_ENTER:
	case K_MOUSE1:
		m_entersound = true;

		switch (m_main_cursor) {
		case 0:
			M_Menu_SinglePlayer_f ();
			break;

		case 1:
			M_Menu_MultiPlayer_f ();
			break;

		case 2:
			M_Menu_Options_f ();
			break;

	#ifdef WITH_MP3_PLAYER
		case 3:
			M_Menu_MP3_Control_f ();
			break;
	#endif

		case 4:
			if (cl_confirmquit.value) {
				M_Menu_Quit_f ();
			}
			else {
				Host_Quit ();
			}
			break;
		}
	}
}

static qbool M_Main_Mouse_Event(const mouse_state_t* ms)
{
	M_Mouse_Select(&m_main_window, ms, MAIN_ITEMS, &m_main_cursor);
    
    if (ms->button_up == 1) M_Main_Key(K_MOUSE1);
    if (ms->button_up == 2) M_Main_Key(K_MOUSE2);

    return true;
}

//=============================================================================
/* OPTIONS MENU */

void M_Menu_Options_f (void) {
	M_EnterMenu (m_options);
}

void M_Options_Key (int key, int unichar) {
	Menu_Options_Key(key, unichar); // menu_options module
}

void M_Options_Draw (void) {
	Menu_Options_Draw();	// menu_options module
}

//=============================================================================
/* PROXY MENU */

// key press in demos menu
void M_Proxy_Draw(void){
	Menu_Proxy_Draw(); // menu_proxy module
}

// demos menu draw request
void M_Proxy_Key (int key) {
	int togglekeys[2];

	// the menu_proxy module doesn't know which key user has bound to toggleproxymenu action
	M_FindKeysForCommand("toggleproxymenu", togglekeys);
	if ((key == togglekeys[0]) || (key == togglekeys[1])) {
		Menu_Proxy_Toggle(); 
		M_LeaveMenus();
		return;
	}

	// ppl are used to access console even when in qizmo menu
	M_FindKeysForCommand("toggleconsole", togglekeys);
	if ((key == togglekeys[0]) || (key == togglekeys[1])) {
		Con_ToggleConsole_f();
		return;
	}

	Menu_Proxy_Key(key); // menu_proxy module
}


//=============================================================================
/* HELP MENU */

int        help_page;
#define    NUM_HELP_PAGES    6

void M_Menu_Help_f (void) {
	M_EnterMenu (m_help);
}

void M_Help_Draw (void) {
	int x, y, w, h;

#ifdef GLQUAKE
	if (scr_scaleMenu.value) {
		menuwidth = vid.width;
		menuheight = vid.height;
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho  (0, menuwidth, menuheight, 0, -99999, 99999);
	}
#endif

    // this will add top, left and bottom padding
    // right padding is not added because it causes annoying scrollbar behaviour
    // when mouse gets off the scrollbar to the right side of it
	w = vid.width - OPTPADDING; // here used to be a limit to 512x... size
	h = vid.height - OPTPADDING*2;
	x = OPTPADDING;
	y = OPTPADDING;

	Menu_Help_Draw (x, y, w, h);
}

void M_Help_Key (int key) {
	extern void Help_Key(int key);
	Menu_Help_Key(key);
}


//=============================================================================
/* QUIT MENU */

int        msgNumber;
int        m_quit_prevstate;
qbool    wasInMenus;

void M_Menu_Quit_f (void) {
	if (m_state == m_quit)
		return;
	wasInMenus = (key_dest == key_menu);
	m_quit_prevstate = m_state;
	msgNumber = rand()&7;
	M_EnterMenu (m_quit);
}

void M_Quit_Key (int key) {
	switch (key) {
		case K_ESCAPE:
		case 'n':
		case 'N':
			if (wasInMenus) {
				m_state = m_quit_prevstate;
				m_entersound = true;
			} else {
				key_dest = key_game;
				m_state = m_none;
			}
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_ENTER:
		case 'Y':
		case 'y':
			key_dest = key_console;
			Host_Quit ();
			break;

		default:
			break;
	}
}

//=============================================================================
/* SINGLE PLAYER MENU */

#ifndef CLIENTONLY

#define    SINGLEPLAYER_ITEMS    3
int    m_singleplayer_cursor;
qbool m_singleplayer_confirm;
qbool m_singleplayer_notavail;
menu_window_t m_singleplayer_window;

extern    cvar_t    maxclients;

void M_Menu_SinglePlayer_f (void) {
	M_EnterMenu (m_singleplayer);
	m_singleplayer_confirm = false;
	m_singleplayer_notavail = false;
}

void M_SinglePlayer_Draw (void) {
	int f;
	mpic_t *p;

	if (m_singleplayer_notavail) {
		p = Draw_CachePic ("gfx/ttl_sgl.lmp");
		M_DrawPic ( (320-p->width)/2, 4, p);
		M_DrawTextBox (60, 10*8, 24, 4);
		M_PrintWhite (80, 12*8, " Cannot start a game");
		M_PrintWhite (80, 13*8, "spprogs.dat not found");
		return;
	}

	if (m_singleplayer_confirm) {
		M_PrintWhite (64, 11*8, "Are you sure you want to");
		M_PrintWhite (64, 12*8, "    start a new game?");
		return;
	}

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/ttl_sgl.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	p = Draw_CachePic ("gfx/sp_menu.lmp");
	m_singleplayer_window.w = p->width;
	m_singleplayer_window.h = p->height;
	M_DrawTransPic_GetPoint(72, 32, &m_singleplayer_window.x, &m_singleplayer_window.y, p);

	f = (int)(curtime * 10)%6;
	M_DrawTransPic (54, 32 + m_singleplayer_cursor * 20,Draw_CachePic( va("gfx/menudot%i.lmp", f+1 ) ) );
}

static void CheckSPGame (void) {
#ifndef WITH_FTE_VFS
	FILE *f;
	FS_FOpenFile ("spprogs.dat", &f);
	if (f) {
		fclose (f);
		m_singleplayer_notavail = false;
	} else {
		m_singleplayer_notavail = true;
	}

#else
	vfsfile_t *v;
	if ((v = FS_OpenVFS("spprogs.dat", "rb", FS_ANY))) {
		VFS_CLOSE(v);
		m_singleplayer_notavail = false;
	} else {
		m_singleplayer_notavail = true;
	}
#endif
}

static void StartNewGame (void) {
	key_dest = key_game;
	Cvar_Set (&maxclients, "1");
	Cvar_Set (&teamplay, "0");
	Cvar_Set (&deathmatch, "0");
	Cvar_Set (&coop, "0");

	if (com_serveractive)
		Cbuf_AddText ("disconnect\n");

	progs = (dprograms_t *) FS_LoadHunkFile ("spprogs.dat", NULL);
	if (progs && !file_from_gamedir)
		Cbuf_AddText ("gamedir qw\n");
	Cbuf_AddText ("map start\n");
}

void M_SinglePlayer_Key (int key) {
	if (m_singleplayer_notavail) {
		switch (key) {
			case K_BACKSPACE:
			case K_ESCAPE:
			case K_ENTER:
				m_singleplayer_notavail = false;
				break;
		}
		return;
	}

	if (m_singleplayer_confirm) {
		if (key == K_ESCAPE || key == 'n' || key == K_MOUSE2) {
			m_singleplayer_confirm = false;
			m_entersound = true;
		} else if (key == 'y' || key == K_ENTER || key == K_MOUSE1) {
			StartNewGame ();
		}
		return;
	}

	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_main);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
			S_LocalSound ("misc/menu1.wav");
			if (++m_singleplayer_cursor >= SINGLEPLAYER_ITEMS)
				m_singleplayer_cursor = 0;
			break;

		case K_UPARROW:
		case K_MWHEELUP:
			S_LocalSound ("misc/menu1.wav");
			if (--m_singleplayer_cursor < 0)
				m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
			break;

		case K_HOME:
		case K_PGUP:
			S_LocalSound ("misc/menu1.wav");
			m_singleplayer_cursor = 0;
			break;

		case K_END:
		case K_PGDN:
			S_LocalSound ("misc/menu1.wav");
			m_singleplayer_cursor = SINGLEPLAYER_ITEMS - 1;
			break;

		case K_ENTER:
		case K_MOUSE1:
			switch (m_singleplayer_cursor) {
				case 0:
					CheckSPGame ();
					if (m_singleplayer_notavail) {
						m_entersound = true;
						return;
					}
					if (com_serveractive) {
						// bring up confirmation dialog
						m_singleplayer_confirm = true;
						m_entersound = true;
					} else {
						StartNewGame ();
					}
					break;

				case 1:
					M_Menu_Load_f ();
					break;

				case 2:
					M_Menu_Save_f ();
					break;
			}
	}
}

qbool M_SinglePlayer_Mouse_Event(const mouse_state_t* ms)
{
	M_Mouse_Select(&m_singleplayer_window, ms, SINGLEPLAYER_ITEMS, &m_singleplayer_cursor);

    if (ms->button_up == 1) M_SinglePlayer_Key(K_MOUSE1);
    if (ms->button_up == 2) M_SinglePlayer_Key(K_MOUSE2);

    return true;
}

#else    // !CLIENTONLY

void M_Menu_SinglePlayer_f (void) {
	M_EnterMenu (m_singleplayer);
}

void M_SinglePlayer_Draw (void) {
	mpic_t *p;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/ttl_sgl.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	//    M_DrawTransPic (72, 32, Draw_CachePic ("gfx/sp_menu.lmp") );

	M_DrawTextBox (60, 10*8, 23, 4);
	M_PrintWhite (88, 12*8, "This client is for");
	M_PrintWhite (88, 13*8, "Internet play only");
}

void M_SinglePlayer_Key (key) {
	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_ESCAPE:
		case K_ENTER:
			M_LeaveMenu (m_main);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;
	}
}
#endif    // CLIENTONLY


//=============================================================================
/* LOAD/SAVE MENU */

#ifndef CLIENTONLY

#define    MAX_SAVEGAMES        12

int        load_cursor;        // 0 < load_cursor < MAX_SAVEGAMES
char    m_filenames[MAX_SAVEGAMES][SAVEGAME_COMMENT_LENGTH + 1];
int        loadable[MAX_SAVEGAMES];
menu_window_t load_window, save_window;

void M_ScanSaves (char *sp_gamedir) {
	int i, j, version;
	char name[MAX_OSPATH];
#ifndef WITH_FTE_VFS
	FILE *f;
#else
	vfsfile_t *f;
#endif

	for (i = 0; i < MAX_SAVEGAMES; i++) {
		strlcpy (m_filenames[i], "--- UNUSED SLOT ---", SAVEGAME_COMMENT_LENGTH + 1);
		loadable[i] = false;

#ifndef WITH_FTE_VFS
		snprintf (name, sizeof(name), "%s/save/s%i.sav", sp_gamedir, i);
		if (!(f = fopen (name, "r")))
			continue;
		fscanf (f, "%i\n", &version);
		fscanf (f, "%79s\n", name);
		strlcpy (m_filenames[i], name, sizeof(m_filenames[i]));
#else
		snprintf (name, sizeof(name), "save/s%i.sav", i);
		if (!(f = FS_OpenVFS(name, "rb", FS_GAME_OS)))
			continue;
		VFS_GETS(f, name, sizeof(name));
		version = atoi(name);
		VFS_GETS(f, name, sizeof(name));
		strlcpy (m_filenames[i], name, sizeof(m_filenames[i]));
#endif

		// change _ back to space
		for (j = 0; j < SAVEGAME_COMMENT_LENGTH; j++)
			if (m_filenames[i][j] == '_')
				m_filenames[i][j] = ' ';
		loadable[i] = true;
#ifndef WITH_FTE_VFS
		fclose (f);
#else
		VFS_CLOSE(f);
#endif
	}
}

void M_Menu_Load_f (void) {
#ifndef WITH_FTE_VFS
	FILE *f;

	if (FS_FOpenFile ("spprogs.dat", &f) == -1)
		return;
#else
	vfsfile_t *f;

	if (!(f = FS_OpenVFS("spprogs.dat", "rb", FS_ANY)))
		return;
#endif

	M_EnterMenu (m_load);
	// VFS-FIXME: file_from_gamedir is not set in FS_OpenVFS
	M_ScanSaves (!file_from_gamedir ? "qw" : com_gamedir);
}

void M_Menu_Save_f (void) {
	if (sv.state != ss_active)
		return;
	if (cl.intermission)
		return;

	M_EnterMenu (m_save);
	M_ScanSaves (com_gamedir);
}

void M_Load_Draw (void) {
	int i;
	mpic_t *p;
	int lx, ly;	// lower bounds of the window

	p = Draw_CachePic ("gfx/p_load.lmp");
	M_DrawPic ( (320 - p->width) >> 1, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		if (i == 0)
			M_Print_GetPoint (16, 32 + 8*i, &load_window.x, &load_window.y, m_filenames[i], load_cursor == 0);
		else 
			M_Print_GetPoint (16, 32 + 8*i, &lx, &ly, m_filenames[i], load_cursor == i);
	}

	load_window.w = SAVEGAME_COMMENT_LENGTH*8; // presume 8 pixels for each letter
	load_window.h = ly - load_window.y + 8;

	// line cursor
	M_DrawCharacter (8, 32 + load_cursor * 8, FLASHINGARROW());
}

void M_Save_Draw (void) {
	int i;
	mpic_t *p;
	int lx, ly;	// lower bounds of the window

	p = Draw_CachePic ("gfx/p_save.lmp");
	M_DrawPic ( (320 - p->width) >> 1, 4, p);

	for (i = 0; i < MAX_SAVEGAMES; i++)
	{
		if (i == 0)
			M_Print_GetPoint (16, 32 + 8 * i, &save_window.x, &save_window.y, m_filenames[i], load_cursor == 0);
		else
			M_Print_GetPoint (16, 32 + 8 * i, &lx, &ly, m_filenames[i], load_cursor == i);
	}

	save_window.w = lx - SAVEGAME_COMMENT_LENGTH*8;
	save_window.h = ly - save_window.y + 8;

	// line cursor
	M_DrawCharacter (8, 32 + load_cursor * 8, FLASHINGARROW());
}

void M_Load_Key (int key) {
	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_singleplayer);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_ENTER:
		case K_MOUSE1:
			S_LocalSound ("misc/menu2.wav");
			if (!loadable[load_cursor])
				return;
			m_state = m_none;
			key_dest = key_game;

			// issue the load command
			if (FS_LoadHunkFile ("spprogs.dat", NULL) && !file_from_gamedir)
				Cbuf_AddText("disconnect; gamedir qw\n");
			Cbuf_AddText (va ("load s%i\n", load_cursor) );
			return;

		case K_UPARROW:
		case K_MWHEELUP:
		case K_LEFTARROW:
			S_LocalSound ("misc/menu1.wav");
			load_cursor--;
			if (load_cursor < 0)
				load_cursor = MAX_SAVEGAMES - 1;
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
		case K_RIGHTARROW:
			S_LocalSound ("misc/menu1.wav");
			load_cursor++;
			if (load_cursor >= MAX_SAVEGAMES)
				load_cursor = 0;
			break;
	}
}

void M_Save_Key (int key) {
	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_singleplayer);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_ENTER:
		case K_MOUSE1:
			m_state = m_none;
			key_dest = key_game;
			Cbuf_AddText (va("save s%i\n", load_cursor));
			return;

		case K_UPARROW:
		case K_MWHEELUP:
		case K_LEFTARROW:
			S_LocalSound ("misc/menu1.wav");
			load_cursor--;
			if (load_cursor < 0)
				load_cursor = MAX_SAVEGAMES-1;
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
		case K_RIGHTARROW:
			S_LocalSound ("misc/menu1.wav");
			load_cursor++;
			if (load_cursor >= MAX_SAVEGAMES)
				load_cursor = 0;
			break;
	}
}

qbool M_Save_Mouse_Event(const mouse_state_t *ms)
{
	return M_Mouse_Select(&save_window, ms, MAX_SAVEGAMES, &load_cursor);
}

qbool M_Load_Mouse_Event(const mouse_state_t *ms)
{
	M_Mouse_Select(&load_window, ms, MAX_SAVEGAMES, &load_cursor);

    if (ms->button_up == 1) M_Load_Key(K_MOUSE1);
    if (ms->button_up == 2) M_Load_Key(K_MOUSE2);

    return true;
}

#endif

//=============================================================================
/* MULTIPLAYER MENU */

int    m_multiplayer_cursor;
#ifdef CLIENTONLY
#define    MULTIPLAYER_ITEMS    2
#else
#define    MULTIPLAYER_ITEMS    3
#endif
menu_window_t m_multiplayer_window;

void M_Menu_MultiPlayer_f (void) {
	M_EnterMenu (m_multiplayer);
}

void M_MultiPlayer_Draw (void) {
	mpic_t    *p;
	int lx, ly;

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);
	M_Print_GetPoint (80, 40, &m_multiplayer_window.x, &m_multiplayer_window.y, "Join Game", m_multiplayer_cursor == 0);
	m_multiplayer_window.h = 8;
#ifndef CLIENTONLY
	M_Print_GetPoint (80, 48, &lx, &ly, "Create Game", m_multiplayer_cursor == 1);
	m_multiplayer_window.h += 8;
#endif
	M_Print_GetPoint (80, 56, &lx, &ly, "Demos", m_multiplayer_cursor == MULTIPLAYER_ITEMS - 1);
	m_multiplayer_window.h += 8;
	m_multiplayer_window.w = 20 * 8; // presume 20 letters long word and 8 pixels for a letter
	
	// cursor
	M_DrawCharacter (64, 40 + m_multiplayer_cursor * 8, FLASHINGARROW());
}

void M_MultiPlayer_Key (int key) {
	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_main);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
			S_LocalSound ("misc/menu1.wav");
			if (++m_multiplayer_cursor >= MULTIPLAYER_ITEMS)
				m_multiplayer_cursor = 0;
			break;

		case K_UPARROW:
		case K_MWHEELUP:
			S_LocalSound ("misc/menu1.wav");
			if (--m_multiplayer_cursor < 0)
				m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
			break;

		case K_HOME:
		case K_PGUP:
			S_LocalSound ("misc/menu1.wav");
			m_multiplayer_cursor = 0;
			break;

		case K_END:
		case K_PGDN:
			S_LocalSound ("misc/menu1.wav");
			m_multiplayer_cursor = MULTIPLAYER_ITEMS - 1;
			break;

		case K_ENTER:
		case K_MOUSE1:
			m_entersound = true;
			switch (m_multiplayer_cursor) {
				case 0:
					M_Menu_ServerList_f ();
					break;
#ifndef CLIENTONLY
				case 1:
					M_Menu_GameOptions_f ();
					break;
#endif
				case 2:
					M_Menu_Demos_f ();
					break;
			}
	}
}

qbool M_MultiPlayer_Mouse_Event(const mouse_state_t *ms)
{
	M_Mouse_Select(&m_multiplayer_window, ms, MULTIPLAYER_ITEMS, &m_multiplayer_cursor);

    if (ms->button_up == 1) M_MultiPlayer_Key(K_MOUSE1);
    if (ms->button_up == 2) M_MultiPlayer_Key(K_MOUSE2);
    
    return true;
}


//=============================================================================
// MULTIPLAYER MENU
// interface for menu_demo module
//=============================================================================

// entering demos menu
void M_Menu_Demos_f (void) {
	M_EnterMenu(m_demos); // switches client state
}

// key press in demos menu
void M_Demo_Draw(void){
	Menu_Demo_Draw(); // menu_demo module
}

// demos menu draw request
void M_Demo_Key (int key) {
	Menu_Demo_Key(key); // menu_demo module
}


//=============================================================================
// MP3 PLAYER MENU
//=============================================================================

#ifdef WITH_MP3_PLAYER

#define M_MP3_CONTROL_HEADINGROW    8
#define M_MP3_CONTROL_MENUROW        (M_MP3_CONTROL_HEADINGROW + 56)
#define M_MP3_CONTROL_COL            104
#define M_MP3_CONTROL_NUMENTRIES    12
#define M_MP3_CONTROL_BARHEIGHT        (200 - 24)

static int mp3_cursor = 0;
static int last_status;

void MP3_Menu_DrawInfo(void);
void M_Menu_MP3_Playlist_f(void);

void M_MP3_Control_Draw (void) {
	char songinfo_scroll[38 + 1], *s = NULL;
	int i, scroll_index, print_time;
	float frac, elapsed, realtime;

	static char lastsonginfo[128], last_title[128];
	static float initial_time;
	static int last_length, last_elapsed, last_total, last_shuffle, last_repeat;

	s = va("%s CONTROL", mp3_player->PlayerName_AllCaps);
	M_Print ((320 - 8 * strlen(s)) >> 1, M_MP3_CONTROL_HEADINGROW, s);

	M_Print (8, M_MP3_CONTROL_HEADINGROW + 16, "\x1d\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1f");


	if (!MP3_IsActive()) {
		s = va("%s LIBRARIES NOT FOUND", mp3_player->PlayerName_AllCaps);
		M_PrintWhite((320 - 24 * 8) >> 1, M_MP3_CONTROL_HEADINGROW + 40, s);
		return;
	}

	realtime = Sys_DoubleTime();

	last_status = MP3_GetStatus();

	if (last_status == MP3_PLAYING)
		M_PrintWhite(312 - 7 * 8, M_MP3_CONTROL_HEADINGROW + 8, "Playing");
	else if (last_status == MP3_PAUSED)
		M_PrintWhite(312 - 6 * 8, M_MP3_CONTROL_HEADINGROW + 8, "Paused");
	else if (last_status == MP3_STOPPED)
		M_PrintWhite(312 - 7 * 8, M_MP3_CONTROL_HEADINGROW + 8, "Stopped");
	else
		M_PrintWhite(312 - 11 * 8, M_MP3_CONTROL_HEADINGROW + 8, "Not Running");

	if (last_status == MP3_NOTRUNNING) {
		s = va("%s is not running", mp3_player->PlayerName_LeadingCaps);
		M_Print ((320 - 8 * strlen(s)) >> 1, 40, s);
		M_PrintWhite (56, 72, "Press");
		M_Print (56 + 48, 72, "ENTER");
		s = va("to start %s", mp3_player->PlayerName_NoCaps);
		M_PrintWhite (56 + 48 + 48, 72, s);
		M_PrintWhite (56, 84, "Press");
		M_Print (56 + 48, 84, "ESC");
		M_PrintWhite (56 + 48 + 32, 84, "to exit this menu");
		// FIXME: This warning is pointless on linux... but is useful on windows
		//M_Print (16, 116, "The variable");
		//M_PrintWhite (16 + 104, 116, mp3_dir.name);
		//M_Print (16 + 104 + 8 * (strlen(mp3_dir.name) + 1), 116, "needs to");
		//s = va("be set to the path for %s first", mp3_player->PlayerName_NoCaps);
		//M_Print (20, 124, s);
		return;
	}

	s = MP3_Menu_SongtTitle();
	if (!strcmp(last_title, s = MP3_Menu_SongtTitle())) {
		elapsed = 3.5 * max(realtime - initial_time - 0.75, 0);
		scroll_index = (int) elapsed;
		frac = bound(0, elapsed - scroll_index, 1);
		scroll_index = scroll_index % last_length;
	} else {
		snprintf(lastsonginfo, sizeof(lastsonginfo), "%s  ***  ", s);
		strlcpy(last_title, s, sizeof(last_title));
		last_length = strlen(lastsonginfo);
		initial_time = realtime;
		frac = scroll_index = 0;
	}

	if ((!mp3_scrolltitle.value || last_length <= 38 + 7) && mp3_scrolltitle.value != 2) {
		char name[38 + 1];
		strlcpy(name, last_title, sizeof(name));
		M_PrintWhite(max(8, (320 - (last_length - 7) * 8) >> 1), M_MP3_CONTROL_HEADINGROW + 32, name);
		initial_time = realtime;
	} else {
		for (i = 0; i < sizeof(songinfo_scroll) - 1; i++)
			songinfo_scroll[i] = lastsonginfo[(scroll_index + i) % last_length];
		songinfo_scroll[sizeof(songinfo_scroll) - 1] = 0;
		M_PrintWhite(12 -  (int) (8 * frac), M_MP3_CONTROL_HEADINGROW + 32, songinfo_scroll);
	}

	if (mp3_showtime.value) {
		MP3_GetOutputtime(&last_elapsed, &last_total);
		if (last_total == -1)
			goto menu_items;

		print_time = (mp3_showtime.value == 2) ? last_total - last_elapsed : last_elapsed;
		M_PrintWhite(8, M_MP3_CONTROL_HEADINGROW + 8, SecondsToMinutesString(print_time));

		if (mp3_showtime.value != 2)
			M_PrintWhite(48, M_MP3_CONTROL_HEADINGROW + 8, va("/%s", SecondsToMinutesString(last_total)));
	}
menu_items:
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW, "Play");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 8, last_status == MP3_PAUSED ? "Unpause" : "Pause");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 16, "Stop");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 24, "Next Track");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 32, "Prev Track");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 40, "Fast Forwd");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 48, "Rewind");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 56, "Volume");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 64, "Shuffle");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 72, "Repeat");
	M_Print (M_MP3_CONTROL_COL, M_MP3_CONTROL_MENUROW + 88, "View Playlist");

	M_DrawCharacter (M_MP3_CONTROL_COL - 8, M_MP3_CONTROL_MENUROW + mp3_cursor * 8, FLASHINGARROW());

	M_DrawSlider(M_MP3_CONTROL_COL + 96, M_MP3_CONTROL_MENUROW + 56, bound(0, Media_GetVolume(), 1));

	MP3_GetToggleState(&last_shuffle, &last_repeat);
	M_PrintWhite (M_MP3_CONTROL_COL + 88, M_MP3_CONTROL_MENUROW + 64, last_shuffle ? "On" : "Off");
	M_PrintWhite (M_MP3_CONTROL_COL + 88, M_MP3_CONTROL_MENUROW + 72, last_repeat ? "On" : "Off");

	M_DrawCharacter (16, M_MP3_CONTROL_BARHEIGHT, 128);
	for (i = 0; i < 35; i++)
		M_DrawCharacter (24 + i * 8, M_MP3_CONTROL_BARHEIGHT, 129);
	M_DrawCharacter (320 - 16, M_MP3_CONTROL_BARHEIGHT, 130);
	M_DrawCharacter (17 + 286 * ((float) last_elapsed / last_total), M_MP3_CONTROL_BARHEIGHT, 131);
}

void M_Menu_MP3_Control_Key(int key) {
	float volume;

	if (!MP3_IsActive() || last_status == MP3_NOTRUNNING) {
		switch(key) {
			case K_BACKSPACE:
				m_topmenu = m_none;
			case K_MOUSE2:
			case K_ESCAPE:
				M_LeaveMenu (m_main);
				break;

			case '`':
			case '~':
				key_dest = key_console;
				m_state = m_none;
				break;

			case K_MOUSE1:
			case K_ENTER:
				if (MP3_IsActive())
					MP3_Execute_f();
				break;
		}
		return;
	}

	con_suppress = true;
	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_main);
			break;
		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;
		case K_HOME:
		case K_PGUP:
			mp3_cursor = 0;
			break;
		case K_END:
		case K_PGDN:
			mp3_cursor = M_MP3_CONTROL_NUMENTRIES - 1;
			break;
		case K_DOWNARROW:
		case K_MWHEELDOWN:
			if (mp3_cursor < M_MP3_CONTROL_NUMENTRIES - 1)
				mp3_cursor++;
			if (mp3_cursor == M_MP3_CONTROL_NUMENTRIES - 2)
				mp3_cursor++;
			break;
		case K_UPARROW:
		case K_MWHEELUP:
			if (mp3_cursor > 0)
				mp3_cursor--;
			if (mp3_cursor == M_MP3_CONTROL_NUMENTRIES - 2)
				mp3_cursor--;
			break;
		case K_MOUSE1:
		case K_ENTER:
			switch (mp3_cursor) {
				case 0:  MP3_Play_f(); break;
				case 1:  MP3_Pause_f(); break;
				case 2:  MP3_Stop_f(); break;
				case 3:  MP3_Next_f(); break;
				case 4:  MP3_Prev_f(); break;
				case 5:  MP3_FastForward_f(); break;
				case 6:  MP3_Rewind_f(); break;
				case 7:  break;
				case 8:  MP3_ToggleShuffle_f(); break;
				case 9:  MP3_ToggleRepeat_f(); break;
				case 10: break;
				case 11: M_Menu_MP3_Playlist_f(); break;
			}
			break;
		case K_RIGHTARROW:
			switch(mp3_cursor) {
				case 7:
					volume = bound(0, Media_GetVolume(), 1);
					Media_SetVolume(volume + 0.02);
					break;
				default:
					MP3_FastForward_f();
					break;
			}
			break;
		case K_LEFTARROW:
			switch(mp3_cursor) {
				case 7:
					volume = bound(0, Media_GetVolume(), 1);
					Media_SetVolume(volume - 0.02);
					break;
				default:
					MP3_Rewind_f();
					break;
			}
			break;
		case 'r': MP3_ToggleRepeat_f(); break;
		case 's': MP3_ToggleShuffle_f(); break;
		case KP_LEFTARROW: case 'z': MP3_Prev_f(); break;
		case KP_5: case 'x': MP3_Play_f(); break;
		case 'c': MP3_Pause_f(); break;
		case 'v': MP3_Stop_f();     break;
		case 'V': MP3_FadeOut_f();    break;
		case KP_RIGHTARROW: case 'b': MP3_Next_f(); break;
		case KP_HOME: MP3_Rewind_f(); break;
		case KP_PGUP: MP3_FastForward_f(); break;
	}
	con_suppress = false;
}

void M_Menu_MP3_Control_f (void){
	M_EnterMenu (m_mp3_control);
}

#define PLAYLIST_MAXENTRIES        2048
#define PLAYLIST_MAXLINES        17
#define PLAYLIST_HEADING_ROW    8

#define PLAYLIST_MAXTITLE    (32 + 1)

static int playlist_size = 0;
static int playlist_cursor = 0, playlist_base = 0;
static char playlist_entries[PLAYLIST_MAXLINES][MP3_MAXSONGTITLE];


void M_Menu_MP3_Playlist_MoveBase(int absolute);
void M_Menu_MP3_Playlist_MoveCursor(int absolute);
void M_Menu_MP3_Playlist_MoveBaseRel(int offset);
void M_Menu_MP3_Playlist_MoveCursorRel(int offset);

/**
 * Center the playlist cursor on the current song
 */
static void Center_Playlist(void) {
	int current;
	int curosor_position;

	MP3_GetPlaylistInfo(&current, NULL);
	M_Menu_MP3_Playlist_MoveBase(current);

	/* Calculate the corect cursor position */
	/* We can display all songs so its just the current index */
	if (current < PLAYLIST_MAXLINES)
		curosor_position = current;
	/* We are at the end of the playlist */
	else if (playlist_size - (current) < PLAYLIST_MAXLINES)
		curosor_position = PLAYLIST_MAXLINES - (playlist_size - (current));
	/* We are in the middle of the playlist & the bottom is the current song*/
	else 
		curosor_position = 0;
	M_Menu_MP3_Playlist_MoveCursor(curosor_position);
}



/**
 * Move the cursor to an absolute position
 */
void M_Menu_MP3_Playlist_MoveCursor(int absolute) {
	playlist_cursor = bound(0, absolute, 
								min((PLAYLIST_MAXLINES - 1), playlist_size));
}

/**
 * Move the base of the playlist to an absolute position in the playlist
 */
void M_Menu_MP3_Playlist_MoveBase(int absolute) {
	int i;

	MP3_GetPlaylistInfo(NULL, &playlist_size);
	playlist_base = bound(0, absolute, playlist_size-PLAYLIST_MAXLINES);

	for (i = playlist_base; i < min(playlist_size, PLAYLIST_MAXLINES+playlist_base); i++) {
		MP3_GetSongTitle(i+1, playlist_entries[i%PLAYLIST_MAXLINES], sizeof(*playlist_entries));
	}

}


/**
 * Move the cursor by an offset relative to the current position
 */
void M_Menu_MP3_Playlist_MoveCursorRel(int offset) {
	/* Already at the bottom */
	if (playlist_base + playlist_cursor + offset > playlist_size - 1) {
		// Already at the top
	} else if (playlist_cursor + offset > PLAYLIST_MAXLINES - 1) {
		/* Need to scroll down */
		M_Menu_MP3_Playlist_MoveBaseRel(offset);
		playlist_cursor = PLAYLIST_MAXLINES - 1;
	} else if (playlist_cursor + offset < 0) {
		/* Need to scroll up*/
		M_Menu_MP3_Playlist_MoveBaseRel(offset);
		playlist_cursor = 0;
	} else {
		playlist_cursor += offset;
	}
}

/**
 * Move the base of the playlist relative to the current position
 */
void M_Menu_MP3_Playlist_MoveBaseRel(int offset) {
	int i;

	/* Make sure we don't exced the bounds */
	if (playlist_base + offset < 0) {
		offset = 0 - playlist_base;
	} else if (playlist_base + PLAYLIST_MAXLINES + offset > playlist_size) {
		offset = playlist_size - (playlist_base + PLAYLIST_MAXLINES);
	}

	if (offset < 0) {
		for (i = playlist_base - 1; i >= playlist_base + offset; i--) {
			MP3_GetSongTitle(i+1, playlist_entries[i % PLAYLIST_MAXLINES], 
							sizeof(*playlist_entries));
		}
	} else { // (offset > 0)
		int track_num = playlist_base + 1 + PLAYLIST_MAXLINES;
		for (i = playlist_base; i < playlist_base + offset; i++, track_num++) {
			MP3_GetSongTitle(track_num, playlist_entries[i%PLAYLIST_MAXLINES], 
							sizeof(*playlist_entries));
		}
	}

	playlist_base += offset;
}

/**
 * Reset the playlist, and read from the begining
 */
void M_Menu_MP3_Playlist_Read(void) {
	MP3_CachePlaylist();

	M_Menu_MP3_Playlist_MoveBase(0);
	M_Menu_MP3_Playlist_MoveCursor(0);
}

/**
 * Draw the playlist menu, with the current playing song in white
 */
void M_Menu_MP3_Playlist_Draw(void) {
	int		index, print_time, i, count;
	char 	name[PLAYLIST_MAXTITLE], *s;
	float 	realtime;

	static int last_status,last_elapsed, last_total, last_current;

	realtime = Sys_DoubleTime();

	last_status = MP3_GetStatus();

	if (last_status == MP3_NOTRUNNING) {
		M_Menu_MP3_Control_f();
		return;
	}

	s = va("%s PLAYLIST", mp3_player->PlayerName_AllCaps);
	M_Print ((320 - 8 * strlen(s)) >> 1, PLAYLIST_HEADING_ROW, s);
	M_Print (8, PLAYLIST_HEADING_ROW + 16, "\x1d\x1e\x1e\x1f \x1d\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1f");

	if (last_status == MP3_PLAYING)
		M_PrintWhite(312 - 7 * 8, PLAYLIST_HEADING_ROW + 8, "Playing");
	else if (last_status == MP3_PAUSED)
		M_PrintWhite(312 - 6 * 8, PLAYLIST_HEADING_ROW + 8, "Paused");
	else if (last_status == MP3_STOPPED)
		M_PrintWhite(312 - 7 * 8, PLAYLIST_HEADING_ROW + 8, "Stopped");

	if (mp3_showtime.value) {
		MP3_GetOutputtime(&last_elapsed, &last_total);
		if (last_total == -1)
			goto menu_items;

		print_time = (mp3_showtime.value == 2) ? last_total - last_elapsed : last_elapsed;
		M_PrintWhite(8, PLAYLIST_HEADING_ROW + 8, SecondsToMinutesString(print_time));

		if (mp3_showtime.value != 2)
			M_PrintWhite(48, M_MP3_CONTROL_HEADINGROW + 8, va("/%s", SecondsToMinutesString(last_total)));
	}
menu_items:
	if (!playlist_size) {
		M_Print (92, 32, "Playlist is empty");
		return;
	}

	MP3_GetPlaylistInfo(&last_current, NULL);

	for (index = playlist_base, count = 0;
			index < playlist_size && count < PLAYLIST_MAXLINES;
			index++, count++) {
		char *spaces;

		/* Pad the index count */
		if (index + 1 < 10)
			spaces = "   ";
		else if (index + 1 < 100)
			spaces = "  ";
		else if (index + 1 < 1000)
			spaces = " ";
		else
			spaces = "";

		/* Limit the title to PLAYLIST_MAXTITLE */
		strlcpy(name, playlist_entries[index % PLAYLIST_MAXLINES],sizeof(name));

		/* Print the current playing song in white */
		if (last_current == index)
			M_PrintWhite(16, PLAYLIST_HEADING_ROW + 24 
					+ (index - playlist_base) * 8, 
				va("%s%d %s", spaces, index + 1, name));
		else
			M_Print(16, PLAYLIST_HEADING_ROW + 24 
					+ (index - playlist_base) * 8, 
				va("%s%d %s", spaces, index + 1, name));
	}
	M_DrawCharacter(8, PLAYLIST_HEADING_ROW + 24 + playlist_cursor * 8, 
			FLASHINGARROW());

	M_DrawCharacter(16, M_MP3_CONTROL_BARHEIGHT, 128);
	for (i = 0; i < 35; i++)
		M_DrawCharacter(24 + i * 8, M_MP3_CONTROL_BARHEIGHT, 129);
	M_DrawCharacter(320 - 16, M_MP3_CONTROL_BARHEIGHT, 130);
	M_DrawCharacter(17 + 286 * ((float) last_elapsed / last_total), 
			M_MP3_CONTROL_BARHEIGHT, 131);
}

void M_Menu_MP3_Playlist_Key (int k) {
	con_suppress = true;
	switch (k) {
		case K_BACKSPACE:
			m_topmenu = m_none;
		case K_ESCAPE:
			M_LeaveMenu (m_mp3_control);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_UPARROW:
		case K_MWHEELUP:
			M_Menu_MP3_Playlist_MoveCursorRel(-1);
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
			M_Menu_MP3_Playlist_MoveCursorRel(1);
			break;

		case K_HOME:
			M_Menu_MP3_Playlist_MoveCursor(0);
			M_Menu_MP3_Playlist_MoveBase(0);
			break;

		case K_END:
			M_Menu_MP3_Playlist_MoveCursor(playlist_size-1);
			M_Menu_MP3_Playlist_MoveBase(playlist_size-1);
			break;

		case K_PGUP:
			M_Menu_MP3_Playlist_MoveBaseRel(-(PLAYLIST_MAXLINES));
			break;

		case K_PGDN:
			M_Menu_MP3_Playlist_MoveBaseRel(PLAYLIST_MAXLINES);
			break;

		case K_ENTER:
			if (!playlist_size)
				break;
			Cbuf_AddText(va("mp3_playtrack %d\n", playlist_cursor + playlist_base + 1));
			break;
		case K_SPACE: M_Menu_MP3_Playlist_Read(); Center_Playlist();break;
		case 'r': MP3_ToggleRepeat_f(); break;
		case 's': MP3_ToggleShuffle_f(); break;
		case KP_LEFTARROW: case 'z': MP3_Prev_f(); break;
		case KP_5: case 'x': MP3_Play_f(); break;
		case 'c': MP3_Pause_f(); break;
		case 'v': MP3_Stop_f();     break;
		case 'V': MP3_FadeOut_f();    break;
		case KP_RIGHTARROW: case 'b': MP3_Next_f(); break;
		case KP_HOME: case K_LEFTARROW:  MP3_Rewind_f(); break;
		case KP_PGUP: case K_RIGHTARROW: MP3_FastForward_f(); break;
	}
	con_suppress = false;
}

void M_Menu_MP3_Playlist_f (void){
	if (!MP3_IsActive()) {
		M_Menu_MP3_Control_f();
		return;
	}

	M_Menu_MP3_Playlist_Read();
	M_EnterMenu (m_mp3_playlist);
	Center_Playlist();
}

#endif // WITH_MP3_PLAYER



//=============================================================================
/* GAME OPTIONS MENU */

#ifndef CLIENTONLY

typedef struct {
	char    *name;
	char    *description;
} level_t;

level_t        levels[] = {
	{"start", "Entrance"},    // 0

	{"e1m1", "Slipgate Complex"},                // 1
	{"e1m2", "Castle of the Damned"},
	{"e1m3", "The Necropolis"},
	{"e1m4", "The Grisly Grotto"},
	{"e1m5", "Gloom Keep"},
	{"e1m6", "The Door To Chthon"},
	{"e1m7", "The House of Chthon"},
	{"e1m8", "Ziggurat Vertigo"},

	{"e2m1", "The Installation"},                // 9
	{"e2m2", "Ogre Citadel"},
	{"e2m3", "Crypt of Decay"},
	{"e2m4", "The Ebon Fortress"},
	{"e2m5", "The Wizard's Manse"},
	{"e2m6", "The Dismal Oubliette"},
	{"e2m7", "Underearth"},

	{"e3m1", "Termination Central"},            // 16
	{"e3m2", "The Vaults of Zin"},
	{"e3m3", "The Tomb of Terror"},
	{"e3m4", "Satan's Dark Delight"},
	{"e3m5", "Wind Tunnels"},
	{"e3m6", "Chambers of Torment"},
	{"e3m7", "The Haunted Halls"},

	{"e4m1", "The Sewage System"},                // 23
	{"e4m2", "The Tower of Despair"},
	{"e4m3", "The Elder God Shrine"},
	{"e4m4", "The Palace of Hate"},
	{"e4m5", "Hell's Atrium"},
	{"e4m6", "The Pain Maze"},
	{"e4m7", "Azure Agony"},
	{"e4m8", "The Nameless City"},

	{"end", "Shub-Niggurath's Pit"},            // 31

	{"dm1", "Place of Two Deaths"},                // 32
	{"dm2", "Claustrophobopolis"},
	{"dm3", "The Abandoned Base"},
	{"dm4", "The Bad Place"},
	{"dm5", "The Cistern"},
	{"dm6", "The Dark Zone"}
};

typedef struct {
	char    *description;
	int        firstLevel;
	int        levels;
} episode_t;

episode_t    episodes[] = {
	{"Welcome to Quake", 0, 1},
	{"Doomed Dimension", 1, 8},
	{"Realm of Black Magic", 9, 7},
	{"Netherworld", 16, 7},
	{"The Elder World", 23, 8},
	{"Final Level", 31, 1},
	{"Deathmatch Arena", 32, 6}
};

extern cvar_t maxclients, maxspectators;

int    startepisode;
int    startlevel;
int _maxclients, _maxspectators;
int _deathmatch, _teamplay, _skill, _coop;
int _fraglimit, _timelimit;

void M_Menu_GameOptions_f (void) {
	M_EnterMenu (m_gameoptions);

	// 16 and 8 are not really limits --- just sane values
	// for these variables...
	_maxclients = min(16, (int)maxclients.value);
	if (_maxclients < 2) _maxclients = 8;
	_maxspectators = max(0, min((int)maxspectators.value, 8));

	_deathmatch = max (0, min((int)deathmatch.value, 5));
	_teamplay = max (0, min((int)teamplay.value, 2));
	_skill = max (0, min((int)skill.value, 3));
	_fraglimit = max (0, min((int)fraglimit.value, 100));
	_timelimit = max (0, min((int)timelimit.value, 60));
}

int gameoptions_cursor_table[] = {48, 56, 64, 72, 80, 88, 96, 104, 112};
#define    NUM_GAMEOPTIONS    9
int        gameoptions_cursor;
menu_window_t gameoptions_window;

void M_GameOptions_Draw (void) {
	mpic_t *p;
	char *msg;
	int lx, ly; // lower bounds

	M_DrawTransPic (16, 4, Draw_CachePic ("gfx/qplaque.lmp") );
	p = Draw_CachePic ("gfx/p_multi.lmp");
	M_DrawPic ( (320-p->width)/2, 4, p);

	//M_DrawTextBox (152, 32, 10, 1);
	M_Print_GetPoint (160, 48, &gameoptions_window.x, &gameoptions_window.y, "begin game", gameoptions_cursor == 0);

	M_Print_GetPoint (0, 56, &gameoptions_window.x, &ly, "        game type", gameoptions_cursor == 1);
	if (!_deathmatch)
		M_Print_GetPoint (160, 56, &lx, &ly, "cooperative", gameoptions_cursor == 1);
	else
		M_Print_GetPoint (160, 56, &lx, &ly, va("deathmatch %i", _deathmatch), gameoptions_cursor == 1);

	M_Print_GetPoint (0, 64, &lx, &ly, "         teamplay", gameoptions_cursor == 2);

	switch(_teamplay) {
		default: msg = "Off"; break;
		case 1: msg = "No Friendly Fire"; break;
		case 2: msg = "Friendly Fire"; break;
	}
	M_Print_GetPoint (160, 64, &lx, &ly, msg, gameoptions_cursor == 2);

	if (_deathmatch == 0) {
		M_Print_GetPoint (0, 72, &lx, &ly, "            skill", gameoptions_cursor == 3);
		switch (_skill) {
			case 0:  M_Print_GetPoint (160, 72, &lx, &ly, "Easy", gameoptions_cursor == 3); break;
			case 1:  M_Print_GetPoint (160, 72, &lx, &ly, "Normal", gameoptions_cursor == 3); break;
			case 2:  M_Print_GetPoint (160, 72, &lx, &ly, "Hard", gameoptions_cursor == 3); break;
			default: M_Print_GetPoint (160, 72, &lx, &ly, "Nightmare", gameoptions_cursor == 3);
		}
	} else {
		M_Print_GetPoint (0, 72, &lx, &ly, "        fraglimit", gameoptions_cursor == 3);
		if (_fraglimit == 0)
			M_Print_GetPoint (160, 72, &lx, &ly, "none", gameoptions_cursor == 3);
		else
			M_Print_GetPoint (160, 72, &lx, &ly, va("%i frags", _fraglimit), gameoptions_cursor == 3);

		M_Print_GetPoint (0, 80, &lx, &ly, "        timelimit", gameoptions_cursor == 4);
		if (_timelimit == 0)
			M_Print_GetPoint (160, 80, &lx, &ly, "none", gameoptions_cursor == 4);
		else
			M_Print_GetPoint (160, 80, &lx, &ly, va("%i minutes", _timelimit), gameoptions_cursor == 4);
	}
	M_Print_GetPoint (0, 88, &lx, &ly, "       maxclients", gameoptions_cursor == 5);
	M_Print_GetPoint (160, 88, &lx, &ly, va("%i", _maxclients), gameoptions_cursor == 5 );

	M_Print_GetPoint (0, 96, &lx, &ly, "       maxspect.", gameoptions_cursor == 6);
	M_Print_GetPoint (160, 96, &lx, &ly, va("%i", _maxspectators), gameoptions_cursor == 6 );

	M_Print_GetPoint (0, 104, &lx, &ly, "         Episode", gameoptions_cursor == 7);
	M_Print_GetPoint (160, 104, &lx, &ly, episodes[startepisode].description, gameoptions_cursor == 7);

	M_Print_GetPoint (0, 112, &lx, &ly, "           Level", gameoptions_cursor == 8);
	M_Print_GetPoint (160, 112, &lx, &ly, levels[episodes[startepisode].firstLevel + startlevel].description, gameoptions_cursor == 8);
	M_Print_GetPoint (160, 120, &lx, &ly, levels[episodes[startepisode].firstLevel + startlevel].name, gameoptions_cursor == 8);

	gameoptions_window.w = 30 * 8;
	gameoptions_window.h = ly - gameoptions_window.y;

	// line cursor
	M_DrawCharacter (144, gameoptions_cursor_table[gameoptions_cursor], FLASHINGARROW());
}

void M_NetStart_Change (int dir) {
	int count;

	switch (gameoptions_cursor) {
		case 1:
			_deathmatch += dir;
			if (_deathmatch < 0) _deathmatch = 5;
			else if (_deathmatch > 5) _deathmatch = 0;
			break;

		case 2:
			_teamplay += dir;
			if (_teamplay < 0) _teamplay = 2;
			else if (_teamplay > 2) _teamplay = 0;
			break;

		case 3:
			if (_deathmatch == 0) {
				_skill += dir;
				if (_skill < 0) _skill = 3;
				else if (_skill > 3) _skill = 0;
			} else {
				_fraglimit += dir * 10;
				if (_fraglimit < 0) _fraglimit = 100;
				else if (_fraglimit > 100) _fraglimit = 0;
			}
			break;

		case 4:
			_timelimit += dir*5;
			if (_timelimit < 0) _timelimit = 60;
			else if (_timelimit > 60) _timelimit = 0;
			break;

		case 5:
			_maxclients += dir;
			if (_maxclients > 16)
				_maxclients = 2;
			else if (_maxclients < 2)
				_maxclients = 16;
			break;

		case 6:
			_maxspectators += dir;
			if (_maxspectators > 8)
				_maxspectators = 0;
			else if (_maxspectators < 0)
				_maxspectators = 8;
			break;

		case 7:
			startepisode += dir;
			count = 7;

			if (startepisode < 0)
				startepisode = count - 1;

			if (startepisode >= count)
				startepisode = 0;

			startlevel = 0;
			break;

		case 8:
			startlevel += dir;
			count = episodes[startepisode].levels;

			if (startlevel < 0)
				startlevel = count - 1;

			if (startlevel >= count)
				startlevel = 0;
			break;
	}
}

void M_GameOptions_Key (int key) {

	if (key == K_MOUSE1 && gameoptions_cursor != 0) {
		M_NetStart_Change (1);
		return;
	}

	switch (key) {
		case K_BACKSPACE:
			m_topmenu = m_none;    // intentional fallthrough
		case K_MOUSE2:
		case K_ESCAPE:
			M_LeaveMenu (m_multiplayer);
			break;

		case '`':
		case '~':
			key_dest = key_console;
			m_state = m_none;
			break;

		case K_UPARROW:
		case K_MWHEELUP:
			S_LocalSound ("misc/menu1.wav");
			gameoptions_cursor--;
			if (!_deathmatch && gameoptions_cursor == 4)
				gameoptions_cursor--;
			if (gameoptions_cursor < 0)
				gameoptions_cursor = NUM_GAMEOPTIONS-1;
			break;

		case K_DOWNARROW:
		case K_MWHEELDOWN:
			S_LocalSound ("misc/menu1.wav");
			gameoptions_cursor++;
			if (!_deathmatch && gameoptions_cursor == 4)
				gameoptions_cursor++;
			if (gameoptions_cursor >= NUM_GAMEOPTIONS)
				gameoptions_cursor = 0;
			break;

		case K_HOME:
			S_LocalSound ("misc/menu1.wav");
			gameoptions_cursor = 0;
			break;

		case K_END:
			S_LocalSound ("misc/menu1.wav");
			gameoptions_cursor = NUM_GAMEOPTIONS-1;
			break;

		case K_LEFTARROW:
			if (gameoptions_cursor == 0)
				break;
			S_LocalSound ("misc/menu3.wav");
			M_NetStart_Change (-1);
			break;

		case K_RIGHTARROW:
			if (gameoptions_cursor == 0)
				break;
			S_LocalSound ("misc/menu3.wav");
			M_NetStart_Change (1);
			break;

		case K_MOUSE1:
		case K_ENTER:
			if (key == K_MOUSE1 && gameoptions_cursor != 0) break;
			S_LocalSound ("misc/menu2.wav");
			//        if (gameoptions_cursor == 0)
			{
				key_dest = key_game;

				// Kill the server, unless we continue playing
				// deathmatch on another level
				if (!_deathmatch || !deathmatch.value)
					Cbuf_AddText ("disconnect\n");

				if (_deathmatch == 0) {
					_coop = 1;
					_timelimit = 0;
					_fraglimit = 0;
				} else {
					_coop = 0;
				}

				Cvar_Set (&deathmatch, va("%i", _deathmatch));
				Cvar_Set (&skill, va("%i", _skill));
				Cvar_Set (&coop, va("%i", _coop));
				Cvar_Set (&fraglimit, va("%i", _fraglimit));
				Cvar_Set (&timelimit, va("%i", _timelimit));
				Cvar_Set (&teamplay, va("%i", _teamplay));
				Cvar_Set (&maxclients, va("%i", _maxclients));
				Cvar_Set (&maxspectators, va("%i", _maxspectators));

				// Cbuf_AddText ("gamedir qw\n");
				Cbuf_AddText ( va ("map %s\n", levels[episodes[startepisode].firstLevel + startlevel].name) );
				return;
			}

			//        M_NetStart_Change (1);
			break;
	}
}

qbool M_GameOptions_Mouse_Event(const mouse_state_t *ms)
{
	M_Mouse_Select(&gameoptions_window, ms, NUM_GAMEOPTIONS, &gameoptions_cursor);

    if (ms->button_up == 1) M_GameOptions_Key(K_MOUSE1);
    if (ms->button_up == 2) M_GameOptions_Key(K_MOUSE2);

    return true;
}

#endif    // !CLIENTONLY

// SLIST -->

#define MENU_X 50
#define TITLE_Y 4
#define MENU_Y 21
#define STAT_Y 166

//int m_multip_cursor = 0, m_multip_mins = 0, m_multip_maxs = 16, m_multip_state;

void M_Menu_ServerList_f (void) {
	M_EnterMenu (m_slist);
}

void M_ServerList_Draw (void) {
	Browser_Draw();
}

void M_ServerList_Key (key) {
	Browser_Key(key);
}

// <-- SLIST

void M_Quit_Draw (void) {
	M_DrawTextBox (0, 0, 38, 26);
	M_PrintWhite (16, 12,  "  Quake version 1.09 by id Software\n\n");
	M_PrintWhite (16, 28,  "Programming        Art \n");
	M_Print (16, 36,  " John Carmack       Adrian Carmack\n");
	M_Print (16, 44,  " Michael Abrash     Kevin Cloud\n");
	M_Print (16, 52,  " John Cash          Paul Steed\n");
	M_Print (16, 60,  " Dave 'Zoid' Kirsch\n");
	M_PrintWhite (16, 68,  "Design             Biz\n");
	M_Print (16, 76,  " John Romero        Jay Wilbur\n");
	M_Print (16, 84,  " Sandy Petersen     Mike Wilson\n");
	M_Print (16, 92,  " American McGee     Donna Jackson\n");
	M_Print (16, 100,  " Tim Willits        Todd Hollenshead\n");
	M_PrintWhite (16, 108, "Support            Projects\n");
	M_Print (16, 116, " Barrett Alexander  Shawn Green\n");
	M_PrintWhite (16, 124, "Sound Effects\n");
	M_Print (16, 132, " Trent Reznor and Nine Inch Nails\n\n");
	M_PrintWhite (16, 148, "Quake is a trademark of Id Software,\n");
	M_PrintWhite (16, 156, "inc., (c)1996 Id Software, inc. All\n");
	M_PrintWhite (16, 164, "rights reserved. NIN logo is a\n");
	M_PrintWhite (16, 172, "registered trademark licensed to\n");
	M_PrintWhite (16, 180, "Nothing Interactive, Inc. All rights\n");
	M_PrintWhite (16, 188, "reserved.\n\n");
	M_Print (16, 204, "          Press y to exit\n");
}

//=============================================================================
/* Menu Subsystem */

void M_Init (void) {
	extern cvar_t menu_marked_bgcolor;
#ifdef GLQUAKE
	extern cvar_t menu_marked_fade;
#endif

	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);
	Cvar_Register (&scr_centerMenu);
#ifdef GLQUAKE
	Cvar_Register (&scr_scaleMenu);
	Cvar_Register (&menu_marked_fade);
#endif

	Cvar_Register (&menu_marked_bgcolor);

	Cvar_ResetCurrentGroup();
	Browser_Init();
	Menu_Help_Init();	// help_files module
	Menu_Demo_Init();	// menu_demo module
	Menu_Options_Init(); // menu_options module
	Menu_Ingame_Init();

	Cmd_AddCommand ("togglemenu", M_ToggleMenu_f);
	Cmd_AddCommand ("toggleproxymenu", M_ToggleProxyMenu_f);

	Cmd_AddCommand ("menu_main", M_Menu_Main_f);
#ifndef CLIENTONLY
	Cmd_AddCommand ("menu_singleplayer", M_Menu_SinglePlayer_f);
	Cmd_AddCommand ("menu_load", M_Menu_Load_f);
	Cmd_AddCommand ("menu_save", M_Menu_Save_f);
#endif
	Cmd_AddCommand ("menu_multiplayer", M_Menu_MultiPlayer_f);
	Cmd_AddCommand ("menu_slist", M_Menu_ServerList_f);
#ifdef WITH_MP3_PLAYER
	Cmd_AddCommand ("menu_mp3_control", M_Menu_MP3_Control_f);
	Cmd_AddCommand ("menu_mp3_playlist", M_Menu_MP3_Playlist_f);
#endif
	Cmd_AddCommand ("menu_demos", M_Menu_Demos_f);
	Cmd_AddCommand ("menu_options", M_Menu_Options_f);
	Cmd_AddCommand ("help", M_Menu_Help_f);
	Cmd_AddCommand ("menu_help", M_Menu_Help_f);
	Cmd_AddCommand ("menu_quit", M_Menu_Quit_f);
}

void M_Draw (void) {
	if (m_state == m_none || key_dest != key_menu || m_state == m_proxy)
		return;

	if (!m_recursiveDraw) {
		scr_copyeverything = 1;

		if (SCR_NEED_CONSOLE_BACKGROUND) {
			Draw_ConsoleBackground (scr_con_current);
#if (!defined GLQUAKE && defined _WIN32)
			VID_UnlockBuffer ();
#endif
			S_ExtraUpdate ();
#if (!defined GLQUAKE && defined _WIN32)
			VID_LockBuffer ();
#endif
		} else {
			// if you don't like fade in ingame menu, uncomment this
			// if (m_state != m_ingame && m_state != m_democtrl)
			Draw_FadeScreen ();
		}

		scr_fullupdate = 0;
	} else {
		m_recursiveDraw = false;
	}

#ifdef GLQUAKE
	if (scr_scaleMenu.value) {
		menuwidth = 320;
		menuheight = min (vid.height, 240);
		glMatrixMode(GL_PROJECTION);
		glLoadIdentity ();
		glOrtho  (0, menuwidth, menuheight, 0, -99999, 99999);
	} else {
		menuwidth = vid.width;
		menuheight = vid.height;
	}
#endif

	if (scr_centerMenu.value)
		m_yofs = (menuheight - 200) / 2;
	else
		m_yofs = 0;

	switch (m_state) {
		case m_none:
			break;

		case m_main:
			M_Main_Draw ();
			break;

		case m_singleplayer:
			M_SinglePlayer_Draw ();
			break;

#ifndef CLIENTONLY
		case m_load:
			M_Load_Draw ();
			break;

		case m_save:
			M_Save_Draw ();
			break;
#endif

		case m_multiplayer:
			M_MultiPlayer_Draw ();
			break;

		case m_options:
			M_Options_Draw ();
			break;

		case m_proxy:
			M_Proxy_Draw ();
			break;

		case m_ingame: M_Ingame_Draw(); break;
		case m_democtrl: M_Democtrl_Draw(); break;

		case m_help:
			M_Help_Draw ();
			break;

		case m_quit:
			M_Quit_Draw ();
			break;

#ifndef CLIENTONLY
		case m_gameoptions:
			M_GameOptions_Draw ();
			break;
#endif

		case m_slist:
			M_ServerList_Draw ();
			break;
			/*
			   case m_sedit:
			   M_SEdit_Draw ();
			   break;
			 */
		case m_demos:
			M_Demo_Draw ();
			break;

#ifdef WITH_MP3_PLAYER
		case m_mp3_control:
			M_MP3_Control_Draw ();
			break;

		case m_mp3_playlist:
			M_Menu_MP3_Playlist_Draw();
			break;
#endif
	}

#ifdef GLQUAKE
	if (scr_scaleMenu.value) {
		glMatrixMode (GL_PROJECTION);
		glLoadIdentity ();
		glOrtho  (0, vid.width, vid.height, 0, -99999, 99999);
	}
#endif

	if (m_entersound) {
		S_LocalSound ("misc/menu2.wav");
		m_entersound = false;
	}
#if (!defined GLQUAKE && defined _WIN32)
	VID_UnlockBuffer ();
#endif
	S_ExtraUpdate ();
#if (!defined GLQUAKE && defined _WIN32)
	VID_LockBuffer ();
#endif
}

void M_Keydown (int key, int unichar) {
	switch (m_state) {
		case m_none:
			return;

		case m_main:
			M_Main_Key (key);
			return;

		case m_singleplayer:
			M_SinglePlayer_Key (key);
			return;

#ifndef CLIENTONLY
		case m_load:
			M_Load_Key (key);
			return;

		case m_save:
			M_Save_Key (key);
			return;
#endif

		case m_multiplayer:
			M_MultiPlayer_Key (key);
			return;

		case m_options:
			M_Options_Key (key, unichar);
			return;

		case m_proxy:
			M_Proxy_Key (key);
			return;

		case m_ingame: M_Ingame_Key(key); return;
		case m_democtrl: M_Democtrl_Key(key); return;

		case m_help:
			M_Help_Key (key);
			return;

		case m_quit:
			M_Quit_Key (key);
			return;

#ifndef CLIENTONLY
		case m_gameoptions:
			M_GameOptions_Key (key);
			return;
#endif

		case m_slist:
			M_ServerList_Key (key);
			return;
			/*
			   case m_sedit:
			   M_SEdit_Key (key);
			   break;
			 */
		case m_demos:
			M_Demo_Key (key);
			break;

#ifdef WITH_MP3_PLAYER
		case m_mp3_control:
			M_Menu_MP3_Control_Key (key);
			break;

		case m_mp3_playlist:
			M_Menu_MP3_Playlist_Key (key);
			break;
#endif
	}
}

qbool Menu_Mouse_Event(const mouse_state_t* ms)
{
#ifdef WITH_MP3_PLAYER
    // an exception: mp3 player handles only mouse2 as a "go back"
    if (ms->button_up == 2 && (m_state == m_mp3_control || m_state == m_mp3_playlist)) {
        if (m_state == m_mp3_control)       M_Menu_MP3_Control_Key(K_MOUSE2);
        else if (m_state == m_mp3_playlist) M_Menu_MP3_Playlist_Key(K_MOUSE2);
        return true;
    }
#endif

	if (ms->button_down == K_MWHEELDOWN || ms->button_up == K_MWHEELDOWN ||
		ms->button_down == K_MWHEELUP   || ms->button_up == K_MWHEELUP)
	{
		// menus do not handle this type of mouse wheel event, they accept it as a key event	
		return false;
	}

	// send the mouse state to appropriate modules here
    // functions should report if they handled the event or not
    switch (m_state) {
	case m_main:			return M_Main_Mouse_Event(ms);
	case m_singleplayer:	return M_SinglePlayer_Mouse_Event(ms);
	case m_multiplayer:		return M_MultiPlayer_Mouse_Event(ms);
#ifndef CLIENTONLY
	case m_load:			return M_Load_Mouse_Event(ms);
	case m_save:			return M_Load_Mouse_Event(ms);
	case m_gameoptions:		return M_GameOptions_Mouse_Event(ms);
#endif
	case m_options:			return Menu_Options_Mouse_Event(ms);
	case m_slist:			return Browser_Mouse_Event(ms);
	case m_demos:			return Menu_Demo_Mouse_Event(ms);
	case m_ingame:			return Menu_Ingame_Mouse_Event(ms);
	case m_democtrl:		return Menu_Democtrl_Mouse_Event(ms);
	case m_help:			return Menu_Help_Mouse_Event(ms);
	case m_none: default:	return false;
	}
}
