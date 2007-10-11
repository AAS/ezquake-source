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

$Id: cl_main.c,v 1.193 2007-10-11 18:29:32 cokeman1982 Exp $
*/
// cl_main.c  -- client main loop

#include "quakedef.h"
#include "winquake.h"
#include "cdaudio.h"
#include "cl_slist.h"
#include "movie.h"
#include "logging.h"
#include "ignore.h"
#include "fchecks.h"
#include "config_manager.h"
#include "mp3_player.h"
#include "mvd_utils.h"
#include "EX_browser.h"
#include "qtv.h"
#include "keys.h"
#include "hud.h"
#include "hud_common.h"
#include "hud_editor.h"
#include "auth.h"
#include "input.h"
#ifdef GLQUAKE
#include "gl_model.h"
#include "gl_local.h"
#else
#include "r_model.h"
#include "r_local.h"
#endif
#include "teamplay.h"
#include "tp_triggers.h"
#include "rulesets.h"
#include "version.h"
#include "stats_grid.h"
#include "fmod.h"
#include "modules.h"
#include "sbar.h"
#include "utils.h"
#include "qsound.h"
#include "menu.h"
#include "image.h"
#ifndef _WIN32
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif
#ifndef CLIENTONLY
#include "server.h"
#endif
#include "fs.h"
#include "help.h"


void OnChange_allow_scripts (cvar_t *var, char *value, qbool *cancel);
cvar_t	allow_scripts = {"allow_scripts", "2", 0, OnChange_allow_scripts};
cvar_t	rcon_password = {"rcon_password", ""};
cvar_t	rcon_address = {"rcon_address", ""};
cvar_t	cl_crypt_rcon = {"cl_crypt_rcon", "1"};

cvar_t	cl_timeout = {"cl_timeout", "60"};

cvar_t	cl_shownet = {"cl_shownet", "0"};	// can be 0, 1, or 2
#ifdef PROTOCOL_VERSION_FTE
cvar_t  cl_nopext  = {"cl_nopext", "0"};
#endif

#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
cvar_t  cl_chunksperframe  = {"cl_chunksperframe", "2"};
#endif

cvar_t	cl_sbar		= {"cl_sbar", "0", CVAR_ARCHIVE};
cvar_t	cl_hudswap	= {"cl_hudswap", "0", CVAR_ARCHIVE};
cvar_t	cl_maxfps	= {"cl_maxfps", "0", CVAR_ARCHIVE};
cvar_t	cl_physfps	= {"cl_physfps", "0"};	//#fps
void OnChange_indphys (cvar_t *var, char *value, qbool *cancel);
cvar_t  cl_independentPhysics = {"cl_independentPhysics", "1", 0, OnChange_indphys};

cvar_t	cl_predict_players = {"cl_predict_players", "1"};
cvar_t	cl_solid_players = {"cl_solid_players", "1"};
cvar_t	cl_predict_half = {"cl_predict_half", "0"};

cvar_t  show_fps2 = {"scr_scoreboard_drawfps","0"};

cvar_t  localid = {"localid", ""};

static qbool allowremotecmd = true;

cvar_t	cl_deadbodyfilter = {"cl_deadbodyFilter", "0"};
cvar_t	cl_gibfilter = {"cl_gibFilter", "0"};
cvar_t	cl_backpackfilter = {"cl_backpackfilter", "0"};
cvar_t	cl_muzzleflash = {"cl_muzzleflash", "1"};
cvar_t	cl_rocket2grenade = {"cl_r2g", "0"};
cvar_t	cl_demospeed = {"cl_demospeed", "1"};
cvar_t	cl_staticsounds = {"cl_staticSounds", "1"};
void OnChange_cl_fakeshaft (cvar_t *var, char *value, qbool *cancel);
cvar_t	cl_fakeshaft = {"cl_fakeshaft", "0", 0, OnChange_cl_fakeshaft};
cvar_t	cl_parseWhiteText = {"cl_parseWhiteText", "1"};
cvar_t	cl_filterdrawviewmodel = {"cl_filterdrawviewmodel", "0"};
cvar_t	cl_demoPingInterval = {"cl_demoPingInterval", "5"};
cvar_t  demo_getpings      = {"demo_getpings",    "1"};
cvar_t	cl_chatsound = {"s_chat_custom", "1"};
cvar_t	cl_confirmquit = {"cl_confirmquit", "0"}; // , CVAR_INIT
cvar_t	cl_fakename = {"cl_fakename", ""};
cvar_t	qizmo_dir = {"qizmo_dir", "qizmo"};
cvar_t	qwdtools_dir = {"qwdtools_dir", "qwdtools"};

cvar_t	cl_restrictions = {"cl_restrictions", "0"}; // 1 is FuhQuake and QW262 defaults

cvar_t cl_floodprot			= {"cl_floodprot", "0"};
cvar_t cl_fp_messages		= {"cl_fp_messages", "4"};
cvar_t cl_fp_persecond		= {"cl_fp_persecond", "4"};
cvar_t cl_cmdline			= {"cl_cmdline", "", CVAR_ROM};
cvar_t cl_useproxy			= {"cl_useproxy", "0"};
cvar_t cl_window_caption	= {"cl_window_caption", "1"};

cvar_t cl_model_bobbing		= {"cl_model_bobbing", "1"};
// START shaman :: balancing variables
cvar_t cl_nolerp			= {"cl_nolerp", "0"}; // 0 is good for indep-phys, 1 is good for old-phys
// END shaman :: balancing variables
cvar_t cl_newlerp				= {"cl_newlerp", "0.1"};
cvar_t cl_lerp_monsters			= {"cl_lerp_monsters", "1"};
cvar_t cl_fix_mvd				= {"cl_fix_mvd", "0", CVAR_ARCHIVE};

cvar_t r_rocketlight			= {"r_rocketLight", "1"};
cvar_t r_rocketlightcolor		= {"r_rocketLightColor", "0"};
cvar_t r_explosionlightcolor	= {"r_explosionLightColor", "0"};
cvar_t r_explosionlight			= {"r_explosionLight", "1"};
cvar_t r_flagcolor				= {"r_flagColor", "0"};
cvar_t r_lightflicker			= {"r_lightflicker", "1"};
cvar_t r_powerupglow			= {"r_powerupGlow", "1"};
cvar_t cl_novweps				= {"cl_novweps", "0"};
cvar_t r_drawvweps				= {"r_drawvweps", "1"};
// START shaman :: balancing variables
cvar_t r_rockettrail			= {"r_rocketTrail", "1"}; // 9
cvar_t r_grenadetrail			= {"r_grenadeTrail", "1"}; // 3
cvar_t r_explosiontype			= {"r_explosionType", "1"}; // 7
// END shaman :: balancing variables
cvar_t r_telesplash				= {"r_telesplash", "1"}; // disconnect
cvar_t r_shaftalpha				= {"r_shaftalpha", "1"};

// info mirrors
cvar_t	password = {"password", "", CVAR_USERINFO};
cvar_t	spectator = {"spectator", "", CVAR_USERINFO};
cvar_t	name = {"name", "player", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	team = {"team", "", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	topcolor = {"topcolor","", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	bottomcolor = {"bottomcolor","", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	skin = {"skin", "", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	rate = {"rate", "5760", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	msg = {"msg", "1", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t  noaim = {"noaim", "1", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	w_switch = {"w_switch", "", CVAR_ARCHIVE|CVAR_USERINFO};
cvar_t	b_switch = {"b_switch", "", CVAR_ARCHIVE|CVAR_USERINFO};

cvar_t  cl_mediaroot = {"cl_mediaroot", "0", CVAR_ARCHIVE};

// START shaman RFE 1022306
cvar_t  msg_filter = {"msg_filter", "0"};
// END shaman RFE 1022306

cvar_t cl_onload = {"cl_onload", "menu", CVAR_ARCHIVE};

cvar_t demo_autotrack = {"demo_autotrack", "0", CVAR_ARCHIVE}; // use or not autotrack info from mvd demos

clientPersistent_t	cls;
clientState_t		cl;

centity_t       cl_entities[CL_MAX_EDICTS];
efrag_t			cl_efrags[MAX_EFRAGS];
entity_t		cl_static_entities[MAX_STATIC_ENTITIES];
lightstyle_t	cl_lightstyle[MAX_LIGHTSTYLES];
dlight_t		cl_dlights[MAX_DLIGHTS];

// refresh list
#ifdef GLQUAKE
visentlist_t	cl_firstpassents, cl_visents, cl_alphaents;
#else
visentlist_t	cl_visents, cl_visbents;
#endif

double		connect_time = 0;		// for connection retransmits
float nViewsizeExit=100;

qbool	host_skipframe;			// used in demo playback

byte		*host_basepal;
byte		*host_colormap;

int		fps_count;
double		lastfps;

void CL_Multiview(void);

// emodel and pmodel are encrypted to prevent llamas from easily hacking them
char emodel_name[] = { 'e'^0xe5, 'm'^0xe5, 'o'^0xe5, 'd'^0xe5, 'e'^0xe5, 'l'^0xe5, 0 };
char pmodel_name[] = { 'p'^0xe5, 'm'^0xe5, 'o'^0xe5, 'd'^0xe5, 'e'^0xe5, 'l'^0xe5, 0 };

static void simple_crypt (char *buf, int len) {
	while (len--)
		*buf++ ^= 0xe5;
}

static void CL_FixupModelNames (void) {
	simple_crypt (emodel_name, sizeof(emodel_name) - 1);
	simple_crypt (pmodel_name, sizeof(pmodel_name) - 1);
}

//============================================================================

char *CL_Macro_ConnectionType(void) {
	char *s;
	static char macrobuf[16];

	s = (cls.state < ca_connected) ? "disconnected" : cl.spectator ? "spectator" : "player";
	strlcpy(macrobuf, s, sizeof(macrobuf));
	return macrobuf;
}

char *CL_Macro_Demoplayback(void) {
	char *s;
	static char macrobuf[16];

	s = cls.mvdplayback ? "mvdplayback" : cls.demoplayback ? "qwdplayback" : "0";
	strlcpy(macrobuf, s, sizeof(macrobuf));
	return macrobuf;
}

char *CL_Macro_Demotime(void)
{   // intended for scripted & timed camera movement
	static char macrobuf[16];

	snprintf(macrobuf, sizeof(macrobuf), "%f", (float) cls.demotime);
	return macrobuf;
}

char *CL_Macro_Rand(void)
{	// returns a number in range <0..1)
	static char macrobuf[16];

	snprintf(macrobuf, sizeof(macrobuf), "%f", (double) rand() / RAND_MAX);
	return macrobuf;
}

char *CL_Macro_Serverstatus(void) {
	char *s;
	static char macrobuf[16];

	s = (cls.state < ca_connected) ? "disconnected" : cl.standby ? "standby" : "normal";
	strlcpy(macrobuf, s, sizeof(macrobuf));
	return macrobuf;
}

char *CL_Macro_ServerIp(void) {
	return NET_AdrToString(cls.server_adr);
}

char *CL_Macro_Conwidth(void) {
	static char macrobuf[16];
	snprintf(macrobuf, sizeof(macrobuf), "%i", vid.conwidth);
	return macrobuf;
}

char *CL_Macro_Conheight(void) {
	static char macrobuf[16];
	snprintf(macrobuf, sizeof(macrobuf), "%i", vid.conheight);
	return macrobuf;
}

int CL_ClientState (void) {
	return cls.state;
}

void CL_MakeActive(void) {
	cls.state = ca_active;
	if (cls.demoplayback) {
		host_skipframe = true;
		demostarttime = cls.demotime;
	}

	Con_ClearNotify ();
	TP_ExecTrigger ("f_spawn");
}

//Cvar system calls this when a CVAR_USERINFO cvar changes
void CL_UserinfoChanged (char *key, char *string) {
	char *s;

	s = TP_ParseFunChars (string, false);

	if (strcmp(s, Info_ValueForKey (cls.userinfo, key))) {
		Info_SetValueForKey (cls.userinfo, key, s, MAX_INFO_STRING);

		if (cls.state >= ca_connected)
		{
			if (cls.mvdplayback == QTV_PLAYBACK)
			{
				QTV_Cmd_Printf(QTV_VER_1_3, "setinfo \"%s\" \"%s\"", key, s);
			}
			else
			{
				MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
				SZ_Print (&cls.netchan.message, va("setinfo \"%s\" \"%s\"\n", key, s));
			}
		}
	}
}

#ifdef PROTOCOL_VERSION_FTE
unsigned int CL_SupportedFTEExtensions (void)
{
	unsigned int fteprotextsupported = 0;

#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	fteprotextsupported |= FTE_PEXT_CHUNKEDDOWNLOADS;
#endif
#ifdef FTE_PEXT_HLBSP
	fteprotextsupported |= FTE_PEXT_HLBSP;
#endif
#ifdef FTE_PEXT_MODELDBL
	fteprotextsupported |= FTE_PEXT_MODELDBL;
#endif
#ifdef FTE_PEXT_ENTITYDBL
	fteprotextsupported |= FTE_PEXT_ENTITYDBL;
#endif
#ifdef FTE_PEXT_ENTITYDBL2
	fteprotextsupported |= FTE_PEXT_ENTITYDBL2;
#endif
#ifdef FTE_PEXT_SPAWNSTATIC2
	fteprotextsupported |= FTE_PEXT_SPAWNSTATIC2;
#endif
//#ifdef FTE_PEXT_256PACKETENTITIES
//	fteprotextsupported |= FTE_PEXT_256PACKETENTITIES;
//#endif

	return fteprotextsupported;
}
#endif

//called by CL_Connect_f and CL_CheckResend
static void CL_SendConnectPacket(
#ifdef PROTOCOL_VERSION_FTE
							unsigned int ftepext
#endif
								) {
	char data[2048];
	char biguserinfo[MAX_INFO_STRING + 32];
	int extensions;
	extern cvar_t cl_novweps;

	if (cls.state != ca_disconnected)
		return;

#ifdef PROTOCOL_VERSION_FTE
	if (cl_nopext.integer)	//imagine it's an unenhanced server
		ftepext        		   = 0;

	cls.fteprotocolextensions  = (ftepext & CL_SupportedFTEExtensions());
#endif

	connect_time = cls.realtime;	// for retransmit requests
	cls.qport = Cvar_Value("qport");

	// let the server know what extensions we support
	strlcpy (biguserinfo, cls.userinfo, sizeof (biguserinfo));
	extensions = CLIENT_EXTENSIONS &~ (cl_novweps.value ? Z_EXT_VWEP : 0);
	Info_SetValueForStarKey (biguserinfo, "*z_ext", va("%i", extensions), sizeof(biguserinfo));

	snprintf(data, sizeof(data), "\xff\xff\xff\xff" "connect %i %i %i \"%s\"\n", PROTOCOL_VERSION, cls.qport, cls.challenge, biguserinfo);

#ifdef PROTOCOL_VERSION_FTE
	if (cls.fteprotocolextensions) {
		char tmp[128];
		snprintf(tmp, sizeof(tmp), "0x%x 0x%x\n", PROTOCOL_VERSION_FTE, cls.fteprotocolextensions);
		strlcat(data, tmp, sizeof(data));
	}
#endif

	NET_SendPacket(NS_CLIENT, strlen(data), data, cls.server_adr);
}

//Resend a connect message if the last one has timed out
void CL_CheckForResend (void) {
	char data[2048];
	double t1, t2;

	if (cls.state == ca_disconnected && com_serveractive) {
		// if the local server is running and we are not, then connect
		strlcpy (cls.servername, "local", sizeof(cls.servername));
		NET_StringToAdr("local", &cls.server_adr);
#ifdef PROTOCOL_VERSION_FTE
		CL_SendConnectPacket (svs.fteprotocolextensions);	// we don't need a challenge on the local server
#else
		CL_SendConnectPacket ();	// we don't need a challenge on the local server
#endif
		// FIXME: cls.state = ca_connecting so that we don't send the packet twice?
		return;
	}

	if (cls.state != ca_disconnected || !connect_time)
		return;
	if (cls.realtime - connect_time < 5.0)
		return;

	t1 = Sys_DoubleTime();
	if (!NET_StringToAdr(cls.servername, &cls.server_adr)) {
		Com_Printf("Bad server address\n");
		connect_time = 0;
		return;
	}
	t2 = Sys_DoubleTime();
	connect_time = cls.realtime + t2 - t1;	// for retransmit requests

	if (cls.server_adr.port == 0)
		cls.server_adr.port = BigShort(PORT_SERVER);

	Com_Printf("Connecting to %s...\n", cls.servername);
	snprintf(data, sizeof(data), "\xff\xff\xff\xff" "getchallenge\n");
	NET_SendPacket(NS_CLIENT, strlen(data), data, cls.server_adr);
}

void CL_BeginServerConnect(void) 
{
	connect_time = -999;	// CL_CheckForResend() will fire immediately
	CL_CheckForResend();
}

//
// Parses a QW-URL of the following format 
// (this can be associated with ezquake in windows by setting some reg info):
// qw://server:port/command
//
// Supported commands:
// - join/connect
// - spectate/observe
// - qtv
//
void CL_QWURL_f (void)
{
	char *connection_str = NULL;
	char *command = NULL;

	if (Cmd_Argc() != 2) 
	{
		Com_Printf ("Usage: %s <qw-url>\n", Cmd_Argv(0));
		return;
	}

	// Strip the leading qw:// first.
	{
		char qws_str[] = "qw://";
		int qws_len	= sizeof(qws_str) - 1;

		connection_str = Cmd_Argv(1);

		if (!strncasecmp(qws_str, connection_str, qws_len))
		{
			connection_str += qws_len;
		}
		else
		{
			Com_Printf("%s: The QW-URL must start with qw://\n", Cmd_Argv(0));
			return;
		}
	}

	command = connection_str;

	// Find the first "/" and treat what's after it as the command.	
	{
		while ((*command) && (*command) != '/')
		{
			command++;
		}

		// Get rid of the leading "/".
		if ((*command) && (*command) == '/')
		{
			command++;
		}
	}

	// Null terminate the server name string.
	connection_str[command - connection_str - 1] = 0;

	// Default to connecting.
	if (!strcmp(command, "") || !strncasecmp(command, "join", 4) || !strncasecmp(command, "connect", 7))
	{
		Cbuf_AddText(va("connect %s", connection_str));
	}
	else if (!strncasecmp(command, "spectate", 8) || !strncasecmp(command, "observe", 7))
	{
		Cbuf_AddText(va("observe %s", connection_str));
	}
	else if (!strncasecmp(command, "qtv", 3))
	{
		char *password = command + 4;

		Cbuf_AddText(va("qtvplay %s%s", connection_str, ((*password) ? va(" %s", password) : "")));
	}
	else
	{
		Com_Printf("%s: Illegal command %s\n", Cmd_Argv(0), command);
	}
}

void CL_Connect_f (void) 
{
	qbool proxy;

	if (Cmd_Argc() != 2) 
	{
		Com_Printf ("Usage: %s <server>\n", Cmd_Argv(0));
		return;
	}

	proxy = cl_useproxy.value && CL_ConnectedToProxy();

	if (proxy)
	{
		Cbuf_AddText(va("say ,connect %s", Cmd_Argv(1)));
	} 
	else
	{
		Host_EndGame();
		strlcpy(cls.servername, Cmd_Argv(1), sizeof(cls.servername));
		CL_BeginServerConnect();
	}
}

// TCPCONNECT -->
void CL_TCPConnect_f (void)
{
	char buffer[6];
	int newsocket;
	int len;
	int _true = true;

	float giveuptime;

	char *server;

	if (Cmd_Argc() != 2) {
		Com_Printf ("Usage: %s <server>\n", Cmd_Argv(0));
		return;
	}

	server = Cmd_Argv (1);

	Host_EndGame (); // CL_Disconnect_f();

	strlcpy(cls.servername, Cmd_Argv (1), sizeof(cls.servername));

	NET_StringToAdr(cls.servername, &cls.sockettcpdest);

	if (cls.sockettcp != INVALID_SOCKET)
		closesocket(cls.sockettcp);

	cls.sockettcp = INVALID_SOCKET;
	cls.tcpinlen = 0;

	newsocket = TCP_OpenStream(cls.sockettcpdest);
	if (newsocket == INVALID_SOCKET)
	{
		//failed
		Com_Printf("Failed to connect, server is either down, firewalled, or on a different port\n");
		return;
	}


	Com_Printf("Waiting for confirmation of server (10 secs)\n");

	giveuptime = Sys_DoubleTime() + 10;

	while(giveuptime > Sys_DoubleTime())
	{
		len = recv(newsocket, buffer, sizeof(buffer), 0);
		if (!strncmp(buffer, "qizmo\n", 6))
		{
			cls.sockettcp = newsocket;
			break;
		}
		SCR_UpdateScreen();
	}

	if (cls.sockettcp == INVALID_SOCKET)
	{
		Com_Printf("Timeout - wrong server type\n");
		closesocket(newsocket);
		return;
	}

	Com_Printf("Confirmed\n");

	send(cls.sockettcp, buffer, sizeof(buffer), 0);
	if (setsockopt(cls.sockettcp, IPPROTO_TCP, TCP_NODELAY, (char *)&_true, sizeof(_true)) == -1) {
		Com_Printf ("CL_TCPConnect_f: setsockopt: (%i): %s\n", qerrno, strerror(qerrno));
	}

	CL_BeginServerConnect();
}
// <--TCPCONNECT

qbool CL_ConnectedToProxy(void) {
	cmd_alias_t *alias = NULL;
	qbool found = true;
	char **s;
	char *qizmo_aliases[] = {	"ezcomp", "ezcomp2", "ezcomp3",
									"f_sens", "f_fps", "f_tj", "f_ta", NULL};
	char *fteqtv_aliases[] = { "+proxleft", "+proxright", NULL }; // who would need more?

	if (cls.state < ca_active)
		return false;

	for (s = qizmo_aliases; *s; s++) {
		if (!(alias = Cmd_FindAlias(*s)) || !(alias->flags & ALIAS_SERVER)) {
			found = false; break;
		}
	}

	if (found)
		return true;

	found = true;
	for (s = fteqtv_aliases; *s; s++) {
		if (!(alias = Cmd_FindAlias(*s)) || !(alias->flags & ALIAS_SERVER)) {
			found = false; break;
		}
	}

	return found;
}

void CL_Join_f (void) {
	qbool proxy;

	proxy = cl_useproxy.value && CL_ConnectedToProxy();

	if (Cmd_Argc() > 2) {
		Com_Printf ("Usage: %s [server]\n", Cmd_Argv(0));
		return;
	}

	Cvar_Set(&spectator, "");

	if (Cmd_Argc() == 2) {
		// a server name was given, connect directly or through Qizmo
		Cvar_Set(&spectator, "");
		Cbuf_AddText(va("%s %s\n", proxy ? "say ,connect" : "connect", Cmd_Argv(1)));
		return;
	}

	if (!cls.demoplayback && (cl.z_ext & Z_EXT_JOIN_OBSERVE)) {
		// server supports the 'join' command, good
		Cmd_ExecuteString("cmd join");
		return;
	}

	Cbuf_AddText(va("%s\n", proxy ? "say ,reconnect" : "reconnect"));
}


void CL_Observe_f (void) {
	qbool proxy;

	proxy = cl_useproxy.value && CL_ConnectedToProxy();

	if (Cmd_Argc() > 2) {
		Com_Printf ("Usage: %s [server]\n", Cmd_Argv(0));
		return;
	}

	Cvar_SetValue(&spectator, 1);

	if (Cmd_Argc() == 2) {
		// a server name was given, connect directly or through Qizmo
		Cbuf_AddText(va("%s %s\n", proxy ? "say ,connect" : "connect", Cmd_Argv(1)));
		return;
	}

	if (!cls.demoplayback && (cl.z_ext & Z_EXT_JOIN_OBSERVE)) {
		// server supports the 'join' command, good
		Cmd_ExecuteString("cmd observe");
		return;
	}

	Cbuf_AddText(va("%s\n", proxy ? "say ,reconnect" : "reconnect"));
}

// just toggle mode between spec and player
void Cl_ToggleSpec_f (void)
{
	if (spectator.string[0])
		CL_Join_f();
	else
		CL_Observe_f();
}

void CL_DNS_f (void) {
	char address[128], *s;
	struct hostent *h;
	struct in_addr addr;

	if (Cmd_Argc() != 2) {
		Com_Printf("Usage: %s <address>\n", Cmd_Argv(0));
		return;
	}
	strlcpy(address, Cmd_Argv(1), sizeof(address));
	if ((s = strchr(address, ':')))
		*s = 0;
	addr.s_addr = inet_addr(address);
	if (inet_addr(address) == INADDR_NONE) {
		//forward lookup
		if (!(h = gethostbyname(address))) {
			Com_Printf("Couldn't resolve %s\n", address);
		} else {
			addr.s_addr = *(int *) h->h_addr_list[0];
			Com_Printf("Resolved %s to %s\n", address, inet_ntoa(addr));
		}
		return;
	}
	//reverse lookup ip address
	if (!(h = gethostbyaddr((char *) &addr, sizeof(addr), AF_INET)))
		Com_Printf("Couldn't resolve %s\n", address);
	else
		Com_Printf("Resolved %s to %s\n", address, h->h_name);
}

#ifdef GLQUAKE
void SCR_ClearTeamInfo(void);
#endif

void CL_ClearState (void) {
	int i;
	extern float scr_centertime_off;
	extern cshift_t	cshift_empty;
	extern void CL_ProcessServerInfo (void);

	S_StopAllSounds (true);

	Com_DPrintf ("Clearing memory\n");

	if (!com_serveractive)
		Host_ClearMemory();

	CL_ClearTEnts ();
	CL_ClearScene ();

	CL_ClearPredict();

	// wipe the entire cl structure
	memset (&cl, 0, sizeof(cl));

	SZ_Clear (&cls.netchan.message);

	// clear other arrays
	memset (cl_efrags, 0, sizeof(cl_efrags));
	memset (cl_dlights, 0, sizeof(cl_dlights));
	memset (cl_lightstyle, 0, sizeof(cl_lightstyle));
	memset (cl_entities, 0, sizeof(cl_entities));

	cl.viewheight = DEFAULT_VIEWHEIGHT;

	// make sure no centerprint messages are left from previous level
	scr_centertime_off = 0;

	// allocate the efrags and chain together into a free list
	cl.free_efrags = cl_efrags;
	for (i = 0; i < MAX_EFRAGS - 1; i++)
		cl.free_efrags[i].entnext = &cl.free_efrags[i + 1];
	cl.free_efrags[i].entnext = NULL;

	memset (&cshift_empty, 0, sizeof(cshift_empty));	// Tonik

#ifdef GLQUAKE
	// clear teaminfo structs
	SCR_ClearTeamInfo();
#endif

	if (!com_serveractive)
		Cvar_ForceSet (&host_mapname, ""); // notice mapname not valid yet

	CL_ProcessServerInfo(); // force set some default variables, because server may not sent fullserverinfo
}

//Sends a disconnect message to the server
//This is also called on Host_Error, so it shouldn't cause any errors
void CL_Disconnect (void) {

	extern cvar_t r_lerpframes, cl_fakeshaft;

#ifdef GLQUAKE
	extern cvar_t gl_polyblend, gl_clear;
#endif

#ifndef GLQUAKE
	extern cvar_t r_waterwarp;
	extern cvar_t v_contentblend, v_quadcshift, v_ringcshift, v_pentcshift,
		v_damagecshift, v_suitcshift, v_bonusflash;
#endif

	byte final[10];

	connect_time = 0;
	con_addtimestamp = true;

	if (cl.teamfortress)
		V_TF_ClearGrenadeEffects();
	cl.teamfortress = false;

	//
	// Reset values changed by Multiview.
	//
	CURRVIEW = 0;
	scr_viewsize.value		= nViewsizeExit;
	v_contrast.value		= nContrastExit;
	cl_fakeshaft.value		= nfakeshaft;

	#ifdef GLQUAKE
	gl_polyblend.value		= nPolyblendExit;
	gl_clear.value			= nGlClearExit;
	#else
	r_waterwarp.value		= nWaterwarp;
	v_contentblend.value	= nContentblend;
	v_quadcshift.value		= nQuadshift;
	v_ringcshift.value		= nRingshift;
	v_pentcshift.value		= nPentshift;
	v_damagecshift.value	= nDamageshift;
	v_suitcshift.value		= nSuitshift;
	v_bonusflash.value		= nBonusflash;
	#endif

	r_lerpframes.value		= nLerpframesExit;
	nTrack1duel = nTrack2duel = 0;
	bExitmultiview = false;

	// stop sounds (especially looping!)
	S_StopAllSounds (true);

	MT_Disconnect();

	if (cls.demorecording && cls.state != ca_disconnected)
		CL_Stop_f();

	if (cls.demoplayback) {
		CL_StopPlayback();
	} else if (cls.state != ca_disconnected) {
		final[0] = clc_stringcmd;
		strlcpy ((char *)(final + 1), "drop", sizeof (final) - 1);
		Netchan_Transmit (&cls.netchan, 6, final);
		Netchan_Transmit (&cls.netchan, 6, final);
		Netchan_Transmit (&cls.netchan, 6, final);
	}

	memset(&cls.netchan, 0, sizeof(cls.netchan));
	memset(&cls.server_adr, 0, sizeof(cls.server_adr));
	cls.state = ca_disconnected;
	connect_time = 0;

	Cam_Reset();

	CL_FinishDownload(false);

	CL_StopUpload();
	DeleteServerAliases();
	CL_RE_Trigger_ResetLasttime();

// TCPCONNECT -->
	if (cls.sockettcp != INVALID_SOCKET)
	{
		closesocket(cls.sockettcp);
		cls.sockettcp = INVALID_SOCKET;
	}
// <--TCPCONNECT

	cls.qport++; //a hack I picked up from qizmo

	SZ_Clear(&cls.cmdmsg);

// { // so join/observe not confused
	Info_SetValueForStarKey (cl.serverinfo, "*z_ext", "", sizeof(cl.serverinfo));
	cl.z_ext = 0;
// }

	Cvar_ForceSet (&host_mapname, ""); // notice mapname not valid yet
}

void CL_Disconnect_f (void) {
	extern int demo_playlist_started;
	extern int mvd_demo_track_run ;
	cl.intermission = 0;
	demo_playlist_started= 0;
	mvd_demo_track_run = 0;

	Host_EndGame();
}

//The server is changing levels
void CL_Reconnect_f (void) {

	if (cls.download)  // don't change when downloading
		return;

	S_StopAllSounds (true);

	if (cls.mvdplayback == QTV_PLAYBACK) {  // change map during qtv playback
//		QTV_Cmd_Printf(QTV_VER_1_2, "qtvnew");
		return;
	}

	if (cls.state == ca_connected) {
		Com_Printf ("reconnecting...\n");
		MSG_WriteChar (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message, "new");
		return;
	}

	if (!*cls.servername) {
		Com_Printf ("No server to reconnect to.\n");
		return;
	}

	Host_EndGame();
	CL_BeginServerConnect();
}

extern double qstat_senttime;
extern void CL_PrintQStatReply (char *s);
//Responses to broadcasts, etc
void CL_ConnectionlessPacket (void) {
	int c;
	char *s, cmdtext[2048];
#ifdef PROTOCOL_VERSION_FTE
	unsigned int pext = 0;
#endif

    MSG_BeginReading ();
    MSG_ReadLong ();        // skip the -1

	c = MSG_ReadByte ();

	if (msg_badread)
		return;			// runt packet

	switch(c) {
	case S2C_CHALLENGE:
		if (!NET_CompareAdr(net_from, cls.server_adr))
			return;
		Com_Printf("%s: challenge\n", NET_AdrToString(net_from));
		cls.challenge = atoi(MSG_ReadString());

#ifdef PROTOCOL_VERSION_FTE
		for(;;)
		{
			c = MSG_ReadLong();
			if (msg_badread)
				break;
			if (c == PROTOCOL_VERSION_FTE)
				pext = MSG_ReadLong();
			else
				MSG_ReadLong();
		}
		CL_SendConnectPacket(pext);
#else
		CL_SendConnectPacket();
#endif
		break;
	case S2C_CONNECTION:
		if (!NET_CompareAdr(net_from, cls.server_adr))
			return;
		if (!com_serveractive || developer.value)
			Com_Printf("%s: connection\n", NET_AdrToString(net_from));

		if (cls.state >= ca_connected) {
			if (!cls.demoplayback)
				Com_Printf("Dup connect received.  Ignored.\n");
			break;
		}
		Netchan_Setup(NS_CLIENT, &cls.netchan, net_from, cls.qport);
		MSG_WriteChar (&cls.netchan.message, clc_stringcmd);
		MSG_WriteString (&cls.netchan.message, "new");
		cls.state = ca_connected;
		if (!com_serveractive || developer.value)
			Com_Printf("Connected.\n");
		allowremotecmd = false; // localid required now for remote cmds
		break;

	case A2C_CLIENT_COMMAND:	// remote command from gui front end
		Com_Printf ("%s: client command\n", NET_AdrToString (net_from));

		if (net_from.type != net_local_cl_ipadr.type
			|| ((*(unsigned *)net_from.ip != *(unsigned *)net_local_cl_ipadr.ip)
			&& (*(unsigned *)net_from.ip != htonl(INADDR_LOOPBACK))))
		{
			Com_Printf ("Command packet from remote host.  Ignored.\n");
			return;
		}
#ifdef _WIN32
		ShowWindow (mainwindow, SW_RESTORE);
		SetForegroundWindow (mainwindow);
#endif
		s = MSG_ReadString ();
		strlcpy (cmdtext, s, sizeof(cmdtext));
		s = MSG_ReadString ();

		while (*s && isspace(*s))
			s++;
		while (*s && isspace(s[strlen(s) - 1]))
			s[strlen(s) - 1] = 0;

		if (!allowremotecmd && (!*localid.string || strcmp(localid.string, s))) {
			if (!*localid.string) {
				Com_Printf ("===========================\n");
				Com_Printf ("Command packet received from local host, but no "
					"localid has been set.  You may need to upgrade your server "
					"browser.\n");
				Com_Printf ("===========================\n");
			} else {
				Com_Printf ("===========================\n");
				Com_Printf ("Invalid localid on command packet received from local host. "
					"\n|%s| != |%s|\n"
					"You may need to reload your server browser and ezQuake.\n",
					s, localid.string);
				Com_Printf ("===========================\n");
				Cvar_Set(&localid, "");
			}
		} else {
			Cbuf_AddText (cmdtext);
			Cbuf_AddText ("\n");
			allowremotecmd = false;
		}
		break;

	case A2C_PRINT:		// print command from somewhere
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
		if (net_message.cursize > 100 && !strncmp((char *)net_message.data + 5, "\\chunk", sizeof("\\chunk")-1)) {
			CL_Parse_OOB_ChunkedDownload();
			return;
		}
#endif

		if (net_message.data[msg_readcount] == '\\') {
			if (qstat_senttime && curtime - qstat_senttime < 10) {
				CL_PrintQStatReply (MSG_ReadString());
				return;
			}
		}

		Com_Printf("%s: print\n", NET_AdrToString(net_from));
		Com_Printf("%s", MSG_ReadString());
		break;

	case svc_disconnect:
		if (cls.demoplayback) {
			Com_Printf("\n======== End of demo ========\n\n");
			Host_EndGame();
			Host_Abort();
		}
		break;
	}
}

//Handles playback of demos, on top of NET_ code
qbool CL_GetMessage (void) {
#ifdef _WIN32
	CL_CheckQizmoCompletion ();
#endif

	if (cls.demoplayback)
		return CL_GetDemoMessage();

	if (!NET_GetPacket(NS_CLIENT))
		return false;

	return true;
}

void CL_ReadPackets (void) {

	if (cls.nqdemoplayback) {
		NQD_ReadPackets ();
		return;
	}

	while (CL_GetMessage()) {
		// remote command packet
		if (*(int *)net_message.data == -1)	{
			CL_ConnectionlessPacket ();
			continue;
		}

		if (net_message.cursize < 8 && !cls.mvdplayback) {
			Com_DPrintf ("%s: Runt packet\n", NET_AdrToString(net_from));
			continue;
		}

		// packet from server
		if (!cls.demoplayback && !NET_CompareAdr (net_from, cls.netchan.remote_address)) {
			Com_DPrintf ("%s: sequenced packet without connection\n", NET_AdrToString(net_from));
			continue;
		}

		if (cls.mvdplayback) {
			MSG_BeginReading ();
		} else {
			if (!Netchan_Process(&cls.netchan))
				continue;			// wasn't accepted for some reason
		}
		CL_ParseServerMessage ();
	}

	// check timeout
	if (!cls.demoplayback && cls.state >= ca_connected ) {
		if (curtime - cls.netchan.last_received > (cl_timeout.value > 0 ? cl_timeout.value : 60)) {
			Com_Printf("\nServer connection timed out.\n");
			Host_EndGame();
			return;
		}
	}
}

void CL_SendToServer (void) {
	// when recording demos, request new ping times every cl_demoPingInterval.value seconds
	if (cls.demorecording && !cls.demoplayback && cls.state == ca_active && cl_demoPingInterval.value > 0) {
		if (cls.realtime - cl.last_ping_request > cl_demoPingInterval.value) {
			cl.last_ping_request = cls.realtime;
			MSG_WriteByte (&cls.netchan.message, clc_stringcmd);
			SZ_Print (&cls.netchan.message, "pings");
		}
	}

	// send intentions now
	// resend a connection request if necessary
	if (cls.state == ca_disconnected)
		CL_CheckForResend ();
	else
		CL_SendCmd ();
}

//=============================================================================

void CL_InitCommands (void);

#ifdef GLQUAKE
void CL_Fog_f (void) {

	extern cvar_t gl_fogred, gl_foggreen, gl_fogblue, gl_fogenable;
	if (Cmd_Argc() == 1) {
		Com_Printf ("\"fog\" is \"%f %f %f\"\n", gl_fogred.value, gl_foggreen.value, gl_fogblue.value);
		return;
	}
	Cvar_SetValue (&gl_fogenable, 1);
	Cvar_SetValue (&gl_fogred, atof(Cmd_Argv(1)));
	Cvar_SetValue (&gl_foggreen, atof(Cmd_Argv(2)));
	Cvar_SetValue (&gl_fogblue, atof(Cmd_Argv(3)));
}
#endif

void CL_InitLocal (void) {
	extern cvar_t baseskin, noskins, cl_name_as_skin, enemyforceskins, teamforceskins;
	char st[256];
	extern void CL_Messages_f(void);//Tei, cl_messages

	Cvar_SetCurrentGroup(CVAR_GROUP_CHAT);
	Cvar_Register (&cl_parseWhiteText);
	Cvar_Register (&cl_chatsound);
	Cmd_AddLegacyCommand ("cl_chatsound", "s_chat_custom");
	Cvar_Register (&cl_fakename);

	Cvar_Register (&cl_restrictions);

	Cvar_Register (&cl_floodprot);
	Cvar_Register (&cl_fp_messages);
	Cvar_Register (&cl_fp_persecond);

	// START shaman RFE 1022306
	Cvar_Register (&msg_filter);
	// END shaman RFE 1022306


	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);
	Cvar_Register (&cl_shownet);
	Cvar_Register (&show_fps2);
	Cmd_AddLegacyCommand ("draw_fps", "scr_scoreboard_drawfps");
	Cvar_Register (&cl_confirmquit);
	Cvar_Register (&cl_window_caption);
	Cvar_Register (&cl_onload);

	Cvar_SetCurrentGroup(CVAR_GROUP_SBAR);
	Cvar_Register (&cl_sbar);
	Cvar_Register (&cl_hudswap);

	Cvar_SetCurrentGroup(CVAR_GROUP_VIEWMODEL);
	Cvar_Register (&cl_filterdrawviewmodel);

	Cvar_SetCurrentGroup(CVAR_GROUP_EYECANDY);
	Cvar_Register (&cl_model_bobbing);
	Cvar_Register (&cl_nolerp);
	Cvar_Register (&cl_newlerp);
	Cvar_Register (&cl_lerp_monsters);
	Cvar_Register (&cl_maxfps);
	Cvar_Register (&cl_physfps);	//#fps
	Cvar_Register (&cl_independentPhysics);	//#fps
	Cvar_Register (&cl_deadbodyfilter);
	Cvar_Register (&cl_gibfilter);
	Cvar_Register (&cl_backpackfilter);
	Cvar_Register (&cl_muzzleflash);
	Cvar_Register (&cl_rocket2grenade);
	Cvar_Register (&r_explosiontype);
	Cvar_Register (&r_lightflicker);
	Cvar_Register (&r_rockettrail);
	Cvar_Register (&r_grenadetrail);
	Cvar_Register (&r_powerupglow);
	Cvar_Register (&cl_novweps);
	Cvar_Register (&r_drawvweps);
	Cvar_Register (&r_rocketlight);
	Cvar_Register (&r_explosionlight);
	Cvar_Register (&r_rocketlightcolor);
	Cvar_Register (&r_explosionlightcolor);
	Cvar_Register (&r_flagcolor);
	Cvar_Register (&cl_fakeshaft);
	Cmd_AddLegacyCommand ("cl_truelightning", "cl_fakeshaft");
	Cvar_Register (&r_telesplash);
	Cvar_Register (&r_shaftalpha);

	Cvar_SetCurrentGroup(CVAR_GROUP_SKIN);
	Cvar_Register (&noskins);
	Cvar_Register (&baseskin);
	Cvar_Register (&cl_name_as_skin);
	Cvar_Register (&enemyforceskins);
	Cvar_Register (&teamforceskins);

	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
	Cvar_Register (&cl_demospeed);
	Cvar_Register (&cl_demoPingInterval);
	Cvar_Register (&qizmo_dir);
	Cvar_Register (&qwdtools_dir);
	Cvar_Register (&demo_getpings);
	Cvar_Register (&demo_autotrack);

	Cvar_SetCurrentGroup(CVAR_GROUP_SOUND);
	Cvar_Register (&cl_staticsounds);

	Cvar_SetCurrentGroup(CVAR_GROUP_USERINFO);
	Cvar_Register (&team);
	Cvar_Register (&spectator);
	Cvar_Register (&skin);
	Cvar_Register (&rate);
	Cvar_Register (&name);
	Cvar_Register (&msg);
	Cvar_Register (&noaim);
	Cvar_Register (&topcolor);
	Cvar_Register (&bottomcolor);
	Cvar_Register (&w_switch);
	Cvar_Register (&b_switch);

	Cvar_SetCurrentGroup(CVAR_GROUP_NETWORK);
	Cvar_Register (&cl_predict_players);
	Cvar_Register (&cl_solid_players);
	Cvar_Register (&cl_predict_half);
	Cvar_Register (&cl_timeout);
	Cvar_Register (&cl_useproxy);
	Cvar_Register (&cl_crypt_rcon);
	Cvar_Register (&cl_fix_mvd);
#ifdef PROTOCOL_VERSION_FTE
	Cvar_Register (&cl_nopext);
#endif
#ifdef FTE_PEXT_CHUNKEDDOWNLOADS
	Cvar_Register (&cl_chunksperframe);
#endif

	Cvar_SetCurrentGroup(CVAR_GROUP_INPUT_KEYBOARD);
	Cvar_Register (&allow_scripts);

	Cvar_SetCurrentGroup(CVAR_GROUP_SYSTEM_SETTINGS);
	Cvar_Register (&cl_mediaroot);

	Cvar_SetCurrentGroup(CVAR_GROUP_NO_GROUP);
	Cvar_Register (&password);
	Cvar_Register (&rcon_password);
	Cvar_Register (&rcon_address);
	Cvar_Register (&localid);
	Cvar_Register (&cl_warncmd);
	Cvar_Register (&cl_cmdline);
	Cvar_ForceSet (&cl_cmdline, com_args_original);
	Cvar_ResetCurrentGroup();

	snprintf(st, sizeof(st), "ezQuake %i", build_number());

	if (COM_CheckParm ("-norjscripts") || COM_CheckParm ("-noscripts"))
		Cvar_SetValue (&allow_scripts, 0);

 	Info_SetValueForStarKey (cls.userinfo, "*client", st, MAX_INFO_STRING);

	if (COM_CheckParm("-noindphys")) {
		Cvar_SetValue(&cl_independentPhysics, 0);
		Cvar_SetValue(&cl_nolerp, 1);
	}

	Cmd_AddLegacyCommand ("demotimescale", "cl_demospeed");

	CL_InitCommands ();

	Cmd_AddCommand ("disconnect", CL_Disconnect_f);
	Cmd_AddCommand ("connect", CL_Connect_f);

	Cmd_AddCommand ("qwurl", CL_QWURL_f);

	Cmd_AddCommand ("tcpconnect", CL_TCPConnect_f);

	Cmd_AddCommand ("join", CL_Join_f);
	Cmd_AddCommand ("observe", CL_Observe_f);
	Cmd_AddCommand ("togglespec", Cl_ToggleSpec_f);

	Cmd_AddCommand ("dns", CL_DNS_f);
	Cmd_AddCommand ("reconnect", CL_Reconnect_f);

	Cmd_AddMacro("connectiontype", CL_Macro_ConnectionType);
	Cmd_AddMacro("demoplayback", CL_Macro_Demoplayback);
	Cmd_AddMacro("demotime", CL_Macro_Demotime);
	Cmd_AddMacro("rand", CL_Macro_Rand);
	Cmd_AddMacro("matchstatus", CL_Macro_Serverstatus);
	Cmd_AddMacro("serverip", CL_Macro_ServerIp);
	Cmd_AddMacro("conwidth", CL_Macro_Conwidth);
	Cmd_AddMacro("conheight", CL_Macro_Conheight);

	Cmd_AddCommand ("cl_messages", CL_Messages_f);//Tei, cl_messages
}

void GFX_Init (void) {
	Draw_Init ();	// safe re-init imo (in gl)
	SCR_Init ();	// safe re-init imo (in gl)
	R_Init ();		// safe re-init imo (in gl)
	Sbar_Init ();	// safe re-init imo (in gl)
	HUD_Editor_Init(); // Need to reload some textures.
}

void CL_Init (void) {
	int filesize;
	if (dedicated)
		return;

	cls.state = ca_disconnected;
	cls.min_fps = 999999; // HUD -> hexum

	SZ_Init(&cls.cmdmsg, cls.cmdmsg_data, sizeof(cls.cmdmsg_data));
	cls.cmdmsg.allowoverflow = true;

	strlcpy (cls.gamedirfile, com_gamedirfile, sizeof (cls.gamedirfile));
	strlcpy (cls.gamedir, com_gamedir, sizeof (cls.gamedir));

	Modules_Init();
	FChecks_Init();

	host_basepal = (byte *) FS_LoadHunkFile ("gfx/palette.lmp", &filesize);
	if (!host_basepal)
		Sys_Error ("Couldn't load gfx/palette.lmp");
	FMod_CheckModel("gfx/palette.lmp", host_basepal, filesize);

	host_colormap = (byte *) FS_LoadHunkFile ("gfx/colormap.lmp", &filesize);
	if (!host_colormap)
		Sys_Error ("Couldn't load gfx/colormap.lmp");
	FMod_CheckModel("gfx/colormap.lmp", host_colormap, filesize);

	Sys_mkdir(va("%s/qw", com_basedir));
	Sys_mkdir(va("%s/ezquake", com_basedir));

	History_Init();
	V_Init ();
	MVD_Utils_Init ();

#ifdef __linux__
	IN_Init ();
#endif

	VID_Init (host_basepal);

#ifndef __linux__
	IN_Init ();
#endif

	Image_Init();

	GFX_Init ();

#if defined(FRAMEBUFFERS) && defined(GLQUAKE)
	Framebuffer_Init();
#endif

	S_Init ();

	CDAudio_Init ();

	CL_InitLocal ();
	CL_FixupModelNames ();
	CL_InitInput ();
	CL_InitEnts ();
	CL_InitTEnts ();
	CL_InitPrediction ();
	CL_InitCam ();
	TP_Init ();
	Hud_262Init();
	HUD_Init();
	Help_Init();
	M_Init ();

	SList_Init ();
	SList_Load ();

	MT_Init();
	CL_Demo_Init();
	Ignore_Init();
	Auth_Init();
	Log_Init();
	Movie_Init();
	ConfigManager_Init();
	Stats_Init();
	MP3_Init();
	SB_RootInit();

	QTV_Init();

	Sys_InitIPC();

	Rulesets_Init();
}

//============================================================================

void CL_BeginLocalConnection (void) {
	S_StopAllSounds (true);

	// make sure we're not connected to an external server,
	// and demo playback is stopped
	if (!com_serveractive)
		CL_Disconnect ();

	cl.worldmodel = NULL;

	if (cls.state == ca_active)
		cls.state = ca_connected;
}


extern void SV_TogglePause (const char *msg);
extern cvar_t sv_paused,maxclients;

// automatically pause the game when going into the menus in single player
static void CL_CheckAutoPause (void) {
#ifndef CLIENTONLY
	if (com_serveractive && cls.state == ca_active && !cl.deathmatch && maxclients.value == 1
		&& (key_dest == key_menu /*|| key_dest == key_console*/))
	{
		if (!((int)sv_paused.value & 2))
			SV_TogglePause (NULL);
	}
	else {
		if ((int)sv_paused.value & 2)
			SV_TogglePause (NULL);
	}
#endif
}


static double CL_MinFrameTime (void) {
	double fps, fpscap;

	if (cls.timedemo || Movie_IsCapturing())
		return 0;

	if (cls.demoplayback)
	{
		if (!cl_maxfps.value)
			return 0;

		// Multiview.
		if (cl_multiview.value > 0 && cls.mvdplayback)
		{
			fps = max (30.0, cl_maxfps.value * nNumViews);
		}
		else
		{
			fps = max (30.0, cl_maxfps.value);
		}

	}
	else
	{
		if (cl_independentPhysics.value == 0)
		{
			fpscap = cl.maxfps ? max (30.0, cl.maxfps) : Rulesets_MaxFPS();
			fps = cl_maxfps.value ? bound (30.0, cl_maxfps.value, fpscap) : com_serveractive ? fpscap : bound (30.0, rate.value / 80.0, fpscap);
		}
		else
			fps = cl_maxfps.value ? max(cl_maxfps.value, 30) : 99999; //#fps:
	}

	return 1 / fps;
}

//#fps
static double MinPhysFrameTime ()
{
	// server policy
	float fpscap = (cl.maxfps ? cl.maxfps : 72.0);

	// the user can lower it for testing (or really shit connection)
	if (cl_physfps.value)
		fpscap = min(fpscap, cl_physfps.value);

	// not less than this no matter what
	fpscap = max(fpscap, 10);

	return 1 / fpscap;
}

void CL_CalcFPS(void)
{ // HUD -> hexum
	double t;
	static double lastframetime;

	t = Sys_DoubleTime();
	if ((t - lastframetime) >= 1.0)
	{
		lastfps = (double)fps_count / (t - lastframetime);
		fps_count = 0;
		lastframetime = t;
	}

	cls.fps = lastfps;
	if (lastfps > 10.0 && lastfps < cls.min_fps) cls.min_fps = lastfps;
}

void CL_QTVPoll (void);

//#fps:
qbool physframe;
double physframetime;

void CL_Frame (double time) {

	static double extratime = 0.001;
	double minframetime;
	static double	extraphysframetime;	//#fps

	extern cvar_t r_lerpframes;
#ifdef GLQUAKE
	extern cvar_t gl_clear;
	extern cvar_t gl_polyblend;
#else
	extern cvar_t r_waterwarp;
	extern cvar_t v_contentblend, v_quadcshift, v_ringcshift, v_pentcshift,
		v_damagecshift, v_suitcshift, v_bonusflash;
#endif // GLQUAKE

	extratime += time;
	minframetime = CL_MinFrameTime();

	if (extratime < minframetime) {

		extern cvar_t sys_yieldcpu;
		if (sys_yieldcpu.integer)
#ifdef _WIN32
			Sys_MSleep(0);
#else
		// that work bad on linux, at least on my system, dunno why.
		usleep( bound( 0, sys_yieldcpu.integer, 1000 ) );
#endif
		return;
	}

	cls.trueframetime = extratime - 0.001;
	cls.trueframetime = max(cls.trueframetime, minframetime);
	extratime -= cls.trueframetime;

	if (Movie_IsCapturing())
		cls.frametime = Movie_StartFrame();
	else
		cls.frametime = min(0.2, cls.trueframetime);

	//#fps:
	if (cl_independentPhysics.value != 0 /*&& !cls.demoplayback*/)
	{
		double minphysframetime = MinPhysFrameTime();

		extraphysframetime += cls.frametime;
		if (extraphysframetime < minphysframetime)
			physframe = false;
		else
		{
			physframe = true;

			if (extraphysframetime > minphysframetime*2)// FIXME: this is for the case when
				physframetime = extraphysframetime;		// actual fps is too low
			else										// Dunno how to do it right
				physframetime = minphysframetime;
			extraphysframetime -= physframetime;
		}
	} else {
		extraphysframetime = 0;
		physframe = true;
	}

	if (cls.demoplayback) {
		if (cl.paused & PAUSED_DEMO)
			cls.frametime = 0;
		else if (!cls.timedemo)
			cls.frametime *= bound(0, cl_demospeed.value, 20);

		if (!host_skipframe)
			cls.demotime += cls.frametime;
		host_skipframe = false;
	}

	cls.realtime += cls.frametime;

	if (!ISPAUSED) {
		cl.time += cls.frametime;
		cl.servertime += cls.frametime;
		cl.stats[STAT_TIME] = (int) (cl.servertime * 1000);
		if (cls.demoplayback)
			cl.gametime += cls.frametime;
		else
			cl.gametime = Sys_DoubleTime() - cl.gamestarttime - cl.gamepausetime;
	}
	else
	{
		// we hope here, that pause doesn't take long so we don't get too much de-synced
		// if pause takes too much, we can get into usual clock sync-problems as we did before
		cl.gamepausetime += cls.frametime;
	}

	r_refdef2.time = cl.time;

	// get new key events
	if (cl_independentPhysics.value == 0)
	{
		Sys_SendKeyEvents();

		// allow mice or other external controllers to add commands
		IN_Commands();

		// process console commands
		Cbuf_Execute();
		CL_CheckAutoPause ();

		if (com_serveractive)
			SV_Frame(cls.frametime);

		// fetch results from server
		CL_ReadPackets();

		TP_UpdateSkins();

		if (cls.mvdplayback)
		{
			MVD_Interpolate();
			MVD_Mainhook_f();
			
			if (!cl.standby)
			{
				StatsGrid_Gather();
			}
		}

		// process stuffed commands
		Cbuf_ExecuteEx(&cbuf_svc);

		CL_SendToServer();

		// We need to move the mouse also when disconnected
		// to get the cursor working properly.
		if(cls.state == ca_disconnected)
		{
			usercmd_t dummy;
			IN_Move (&dummy);
		}
	}
	else {

		//#fps
		if (physframe)
		{
			Sys_SendKeyEvents();

			// allow mice or other external controllers to add commands
			IN_Commands();

			// process console commands
			Cbuf_Execute();
			CL_CheckAutoPause ();
		}

		if (physframe)	//FIXME?
			if (com_serveractive)
				SV_Frame (physframetime);

		if (physframe)
		{
			// fetch results from server
			CL_ReadPackets();

			TP_UpdateSkins();

			// Gather MVD stats and interpolate.
			if (cls.mvdplayback)
			{
				MVD_Interpolate();
				MVD_Mainhook_f();
			
				if (!cl.standby)
				{
					StatsGrid_Gather();
				}
			}

			// process stuffed commands
			Cbuf_ExecuteEx(&cbuf_svc);

			CL_SendToServer();

			if (cls.state == ca_disconnected) // We need to move the mouse also when disconnected
			{
				usercmd_t dummy;
				IN_Move (&dummy);
			}
		}
		else
		{
			if (   (!cls.demoplayback && !cl.spectator) // not demo playback and not a spec
				|| (!cls.demoplayback &&  cl.spectator && Cam_TrackNum() == -1) // not demo, spec free fly
				|| ( cls.demoplayback && cls.mvdplayback && Cam_TrackNum() == -1) // mvd demo and free fly
				|| cls.state == ca_disconnected // We need to move the mouse also when disconnected
				) {
				usercmd_t dummy;
				Sys_SendKeyEvents();
				IN_Move (&dummy);
			}
		}
	}

	{ // chat icons
		char char_flags[64] = {0};
		int cif_flags = 0;

		if (key_dest != key_game) // add chat flag if in console, menus, mm1, mm2 etc...
			cif_flags |= CIF_CHAT;

#ifdef _WIN32
		// add AFK flag if app minimized, or not the focus
		// FIXME: i dunno how to check the same for *nix
		// TODO: may be add afk flag on idle? if no user input in 45 seconds for example?
		if (!ActiveApp || Minimized)
			cif_flags |= CIF_AFK;
#endif

		if (cif_flags && cls.state >= ca_connected) // put key in userinfo only then we are connected, remove key if we not connected yet
			snprintf(char_flags, sizeof(char_flags), "%d", cif_flags);

		CL_UserinfoChanged ("chat", char_flags);
	}

	if (cls.state >= ca_onserver)
	{
		Cam_SetViewPlayer();

		// Set up prediction for other players
		if ((physframe && cl_independentPhysics.value != 0) || cl_independentPhysics.value == 0)
		{
			CL_SetUpPlayerPrediction(false);
		}

		// do client side motion prediction
		CL_PredictMove();

		// Set up prediction for other players
		if ((physframe && cl_independentPhysics.value != 0) || cl_independentPhysics.value == 0)
		{
			CL_SetUpPlayerPrediction(true);
		}

		// build a refresh entity list
		CL_EmitEntities();
	}

	//
	// Multiview is enabled so save some values for effects that
	// needs to be turned off.
	//
	if (!bExitmultiview)
	{
		nContrastExit		= v_contrast.value;
		nViewsizeExit		= scr_viewsize.value;
		nfakeshaft			= cl_fakeshaft.value;

		#ifdef GLQUAKE
		nPolyblendExit		= gl_polyblend.value;
		nGlClearExit		= gl_clear.value;
		#else
		nWaterwarp			= r_waterwarp.value;
		nContentblend		= v_contentblend.value;
		nQuadshift			= v_quadcshift.value;
		nRingshift			= v_ringcshift.value;
		nPentshift			= v_pentcshift.value;
		nDamageshift		= v_damagecshift.value;
		nSuitshift			= v_suitcshift.value;
		nBonusflash			= v_bonusflash.value;
		#endif

		nLerpframesExit		= r_lerpframes.value;
		CURRVIEW = 0;
	}

	if (bExitmultiview && !cl_multiview.value)
	{
		scr_viewsize.value =  nViewsizeExit;
		v_contrast.value = nContrastExit;
		cl_fakeshaft.value = nfakeshaft;

		#ifdef GLQUAKE
		gl_polyblend.value = nPolyblendExit;
		gl_clear.value = nGlClearExit;
		#else
		r_waterwarp.value = nWaterwarp;
		v_contentblend.value = nContentblend;
		v_quadcshift.value = nQuadshift;
		v_ringcshift.value = nRingshift;
		v_pentcshift.value = nPentshift;
		v_damagecshift.value = nDamageshift;
		v_suitcshift.value = nSuitshift;
		v_bonusflash.value = nBonusflash;
		#endif

		r_lerpframes.value = nLerpframesExit;
		bExitmultiview = false;
	}

	if (cl_multiview.value > 0 && cls.mvdplayback)
	{
		CL_Multiview();
	}

	// update video
	SCR_UpdateScreen();

	CL_DecayLights();

	// update audio
	if ((CURRVIEW == 2 && cl_multiview.value && cls.mvdplayback) || (!cls.mvdplayback || cl_multiview.value < 2))
	{
		if (cls.state == ca_active)
		{
			S_Update (r_origin, vpn, vright, vup);
			CL_DecayLights ();
		}
		else
		{
			S_Update (vec3_origin, vec3_origin, vec3_origin, vec3_origin);
		}

		CDAudio_Update();
	}

	MT_Frame();

	if (Movie_IsCapturing())
	{
		Movie_FinishFrame();
	}

	cls.framecount++;

	fps_count++;

	CL_CalcFPS(); // HUD -> hexum

	VFS_TICK(); // VFS hook for updating some systems

	Sys_ReadIPC();

	CL_QTVPoll();

	CL_UpdateCaption(false);
}

//============================================================================

void CL_Shutdown (void) {
	CL_Disconnect();

	CL_WriteConfiguration();

	SList_Shutdown();
	CDAudio_Shutdown();
	S_Shutdown();
	MP3_Shutdown();
	IN_Shutdown ();
	Modules_Shutdown();
	Log_Shutdown();
	if (host_basepal)
		VID_Shutdown();
	History_Shutdown();
}

int CL_IncrLoop(int cview, int max)
{
	return (cview >= max) ? 1 : ++cview;
}

int CL_NextPlayer(int plr)
{
	if (plr < -1)
	{
		plr = -1;
	}

	plr++;

	while (cl.players[plr].spectator || !strcmp(cl.players[plr].name, ""))
	{
		plr++;
		if (plr >= MAX_CLIENTS)
		{
			plr = 0;
		}
	}
	return plr;
}

void CL_Multiview(void)
{
	static int playernum = 0;

	#ifdef GLQUAKE
	extern cvar_t gl_polyblend;
	extern cvar_t gl_clear;
	#else
	extern cvar_t r_waterwarp;
	extern cvar_t v_contentblend, v_quadcshift, v_ringcshift, v_pentcshift,
		v_damagecshift, v_suitcshift, v_bonusflash;
	#endif
	extern cvar_t r_lerpframes;

	if (!cls.mvdplayback)
	{
		return;
	}

	nNumViews = cl_multiview.value;

	// Only refresh skins once (to start with), I think this is the best solution for multiview
	// eg when viewing 4 players in a 2v2.
	// Also refresh them when the player changes what team to track.
	if ((!CURRVIEW && cls.state >= ca_connected) || nSwapPov)
	{
		TP_RefreshSkins();
	}

	// contrast was disabled for OpenGL build with the note "blanks all but 1 view"
	// this was due to gl_ztrick in R_Clear(void) that would clear these. FIXED
	// v_contrast.value = 1;

	// stop fakeshaft as it lerps with the other views
	if (cl_fakeshaft.value < 1 && cl_fakeshaft.value > 0)
	{
		cl_fakeshaft.value = 0;
	}

	// allow mvinset 1 to use viewsize value
	if ((!cl_mvinset.value && cl_multiview.value == 2) || cl_multiview.value != 2)
	{
		scr_viewsize.value = 120;
	}
	else
	{
		scr_viewsize.value = nViewsizeExit;
	}

	// stop small screens
	if (cl_mvinset.value && cl_multiview.value == 2 && scr_viewsize.value < 100)
	{
		scr_viewsize.value = 100;
	}

	#ifdef GLQUAKE
	gl_polyblend.value = 0;
	gl_clear.value = 0;
	#else
	// disable these because they don't restrict the change to just the new viewport
	r_waterwarp.value = 0;
	v_contentblend.value = 0;
	v_quadcshift.value = 0;
	v_ringcshift.value = 0;
	v_pentcshift.value = 0;
	v_damagecshift.value = 0;
	v_suitcshift.value = 0;
	v_bonusflash.value = 0;
	#endif

	// stop weapon model lerping as it lerps with the other view
	r_lerpframes.value = 0;

	nPlayernum = playernum;

	// Copy the stats for the player we're about to draw in the next
	// view to the client state, so that the correct stats are drawn
	// in the multiview mini-HUD.
	memcpy(cl.stats, cl.players[playernum].stats, sizeof(cl.stats));

	//
	// Increase the current view being rendered.
	//
	CURRVIEW = CL_IncrLoop(CURRVIEW, (int)cl_multiview.value);

	if (cl_mvinset.value && cl_multiview.value == 2)
	{
		//
		// Special case for mvinset and tracking 2 people
		// this is meant for spectating duels primarily.
		// Lets the user swap which player is shown in the
		// main view and the mvinset by pressing jump.
		//

		// If both the mvinset and main view is set to show
		// the same player, pick the first player for the main view
		// and the next after that for the mvinset.
		if (nTrack1duel == nTrack2duel)
		{
			nTrack1duel = CL_NextPlayer(-1);
			nTrack2duel = CL_NextPlayer(nTrack1duel);
		}

		// The user pressed jump so we need to swap the pov.
		if (nSwapPov)
		{
			nTrack1duel = CL_NextPlayer(nTrack1duel);
			nTrack2duel = CL_NextPlayer(nTrack2duel);
			nSwapPov = false;
		}
		else
		{
			// Set the playernum based on if we're drawing the mvinset
			// or the main view
			// (nTrack1duel = main view)
			// (nTrack2duel = mvinset)
			playernum = (CURRVIEW == 1) ? nTrack1duel : nTrack2duel;
		}
	}
	else
	{
		//
		// Normal multiview.
		//

		// Start from the first player on each new frame.
		playernum = ((CURRVIEW == 1) ? 0 : playernum);

		//
		// The player pressed jump and wants to change what team is spectated.
		//
		if (nSwapPov && cl_multiview.value >= 2 && cl.teamplay)
		{
			int j;
			int team_slot_count = 0;
			int last_mv_trackslots[4];

			// Save the old track values and reset them.
			for(j = 0; j < 4; j++)
			{
				last_mv_trackslots[j] = mv_trackslots[j];
				mv_trackslots[j] = -1;
			}

			// Find the new team.
			for(j = 0; j < MAX_CLIENTS; j++)
			{
				if(!cl.players[j].spectator && cl.players[j].name[0])
				{
					// Find the opposite team from the one we are tracking now.
					if(!currteam[0] || (strcmp(currteam, cl.players[j].team) && strcmp(cl.players[j].name, "")))
					{
						strlcpy(currteam, cl.players[j].team, sizeof(currteam));
						break;
					}
				}
			}

			// Find the team members.
			for(j = 0; j < MAX_CLIENTS; j++)
			{
				if(!cl.players[j].spectator
					&& strcmp(cl.players[j].name, "")
					&& !strcmp(currteam, cl.players[j].team))
				{
					// Find the player slot to track.
					mv_trackslots[team_slot_count] = Player_StringtoSlot (cl.players[j].name);
					team_slot_count++;
				}

				// Don't go out of bounds in the mv_trackslots array.
				if(team_slot_count == 4)
				{
					break;
				}
			}

			if(cl_multiview.value == 2 && team_slot_count == 2)
			{
				// Switch between 2on2 teams.
			}
			else if(cl_multiview.value < team_slot_count || team_slot_count >= 3)
			{
				// We don't want to show all from one team and then one of the enemies...
				cl_multiview.value = team_slot_count;
			}
			else if(team_slot_count == 2)
			{
				// 2on2... one team on top, on on the bottom
				// Swap the teams between the top and bottom in a 4 view setup.
				cl_multiview.value = 4;
				mv_trackslots[MV_VIEW3] = last_mv_trackslots[MV_VIEW1];
				mv_trackslots[MV_VIEW4] = last_mv_trackslots[MV_VIEW2];
			}

			nSwapPov = false;
		}
		else
		{
			// Check if the track* values have been set by the user,
			// otherwise show the first 4 players.

			if(CURRVIEW >= 1 && CURRVIEW <= 4)
			{
				// If the value of mv_trackslots[i] is negative, it means that view
				// doesn't have any track value set so we need to find someone to track using CL_NextPlayer().
				playernum = ((mv_trackslots[CURRVIEW - 1] < 0) ? CL_NextPlayer(playernum) : mv_trackslots[CURRVIEW - 1]);
			}
		}
	}

	// BUGFIX - Make sure the player we're tracking is still left, might have disconnected since
	// we picked him for tracking (This would result in being thrown into freefly mode and not being able
	// to go back into tracking mode when a player disconnected).
	if (cl.players[playernum].spectator || !strcmp (cl.players[playernum].name, ""))
	{
		mv_trackslots[CURRVIEW - 1] = -1;
	}

	// Set the current player we're tracking for the next view to be drawn.
	spec_track = playernum;

	// Make sure we reset variables we suppressed during multiview drawing.
	bExitmultiview = true;
}

void CL_UpdateCaption(qbool force)
{
	static char caption[512] = {0};
	char str[512] = {0};

	if (!cl_window_caption.value)
	{
		if (!cls.demoplayback && (cls.state == ca_active))
			snprintf(str, sizeof(str), "ezQuake: %s", cls.servername);
		else
			snprintf(str, sizeof(str), "ezQuake");
	}
	else
	{
		snprintf(str, sizeof(str), "%s - %s", CL_Macro_Serverstatus(), MT_ShortStatus());
	}

	if (force || strcmp(str, caption))
	{
		VID_SetCaption(str);
		strlcpy(caption, str, sizeof(caption));
	}
}
