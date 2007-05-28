/*

	Options Menu module

	Uses Ctrl_Tab and Settings modules

	Naming convention:
	function mask | usage    | purpose
	--------------|----------|-----------------------------
	Menu_Opt_*    | external | interface for menu.c
	CT_Opt_*      | external | interface for Ctrl_Tab.c

	made by:
		johnnycz, Jan 2006
	last edit:
		$Id: menu_options.c,v 1.65 2007-05-28 10:47:34 johnnycz Exp $

*/

#include "quakedef.h"
#include "settings.h"
#include "settings_page.h"
#include "Ctrl_EditBox.h"
#include "vx_stuff.h"
#ifdef GLQUAKE
#include "gl_model.h"
#include "gl_local.h"
#include "tr_types.h"
#else
#include "r_model.h"
#include "r_local.h"
#endif
#include "teamplay.h"
#include "EX_FileList.h"
#include "Ctrl.h"
#include "Ctrl_Tab.h"
#include "input.h"
#include "qsound.h"
#include "menu.h"
#include "keys.h"
#include "hud.h"
#include "hud_common.h"


typedef enum {
	OPTPG_SETTINGS,
	OPTPG_PLAYER,
	OPTPG_FPS,
	OPTPG_HUD,
	OPTPG_MULTIVIEW,
	OPTPG_BINDS,
	OPTPG_VIDEO,
	OPTPG_CONFIG,
}	options_tab_t;

CTab_t options_tab;
int options_unichar;	// variable local to this module

#ifdef GLQUAKE
extern cvar_t     scr_scaleMenu;
extern int        menuwidth;
extern int        menuheight;
#else
#define menuwidth vid.width
#define menuheight vid.height
#endif

extern qbool    m_entersound; // todo - put into menu.h
void M_Menu_Help_f (void);	// todo - put into menu.h
extern cvar_t scr_scaleMenu;
qbool OnMenuAdvancedChange(cvar_t*, char*);
cvar_t menu_advanced = {"menu_advanced", "0"};

//=============================================================================
// <SETTINGS>

#ifdef GLQUAKE
enum {mode_fastest, mode_faithful, mode_eyecandy, mode_movies, mode_undef} fps_mode = mode_undef;
#else
enum {mode_fastest, mode_default, mode_undef} fps_mode = mode_default;
#endif

extern cvar_t scr_fov, scr_newHud, cl_staticsounds, r_fullbrightSkins, cl_deadbodyfilter, cl_muzzleflash;
extern cvar_t scr_sshot_format;
void ResetConfigs_f(void);

static qbool AlwaysRun(void) { return cl_forwardspeed.value > 200; }
const char* AlwaysRunRead(void) { return AlwaysRun() ? "on" : "off"; }
void AlwaysRunToggle(qbool back) {
	if (AlwaysRun()) {
		Cvar_SetValue (&cl_forwardspeed, 200);
		Cvar_SetValue (&cl_backspeed, 200);
		Cvar_SetValue (&cl_sidespeed, 200);
	} else {
		Cvar_SetValue (&cl_forwardspeed, 400);
		Cvar_SetValue (&cl_backspeed, 400);
		Cvar_SetValue (&cl_sidespeed, 400);
	}
}

static qbool InvertMouse(void) { return m_pitch.value < 0; }
const char* InvertMouseRead(void) { return InvertMouse() ? "on" : "off"; }
void InvertMouseToggle(qbool back) { Cvar_SetValue(&m_pitch, -m_pitch.value); }

static qbool AutoSW(void) { return (w_switch.value > 2) || (!w_switch.value) || (b_switch.value > 2) || (!b_switch.value); }
const char* AutoSWRead(void) { return AutoSW() ? "on" : "off"; }
void AutoSWToggle(qbool back) {
	if (AutoSW()) {
		Cvar_SetValue (&w_switch, 1);
		Cvar_SetValue (&b_switch, 1);
	} else {
		Cvar_SetValue (&w_switch, 8);
		Cvar_SetValue (&b_switch, 8);
	}
}

static int GFXPreset(void) {
	if (fps_mode == mode_undef) {
		switch ((int) cl_deadbodyfilter.value) {
#ifdef GLQUAKE
		case 0: fps_mode = mode_eyecandy; break;
		case 1: fps_mode = cl_muzzleflash.value ? mode_faithful : mode_movies; break;
		default: fps_mode = mode_fastest; break;
#else
		case 0: fps_mode = mode_default; break;
		default: fps_mode = mode_fastest; break;
#endif
		}
	}
	return fps_mode;
}
const char* GFXPresetRead(void) {
	switch (GFXPreset()) {
	case mode_fastest: return "fastest";
#ifdef GLQUAKE
	case mode_eyecandy: return "eyecandy";
	case mode_faithful: return "faithful";
	default: return "movies";
#else
	default: return "default";
#endif
	}
}
void GFXPresetToggle(qbool back) {
	if (back) fps_mode--; else fps_mode++;
	if (fps_mode < 0) fps_mode = mode_undef - 1;
	if (fps_mode >= mode_undef) fps_mode = 0;

	switch (GFXPreset()) {
#ifdef GLQUAKE
	case mode_fastest: Cbuf_AddText ("exec cfg/gfx_gl_fast.cfg\n"); return;
	case mode_eyecandy: Cbuf_AddText ("exec cfg/gfx_gl_eyecandy.cfg\n"); return;
	case mode_faithful: Cbuf_AddText ("exec cfg/gfx_gl_faithful.cfg\n"); return;
	case mode_movies: Cbuf_AddText ("exec cfg/gfx_gl_movies.cfg\n"); return;
#else
	case mode_fastest: Cbuf_AddText ("exec cfg/gfx_sw_fast.cfg\n"); return;
	case mode_default: Cbuf_AddText ("exec cfg/gfx_sw_default.cfg\n"); return;
#endif
	}
}

const char* mvdautohud_enum[] = { "off", "simple", "customizable" };
const char* mvdautotrack_enum[] = { "off", "auto", "custom", "multitrack" };
const char* funcharsmode_enum[] = { "ctrl+key", "ctrl+y" };
const char* ignoreopponents_enum[] = { "off", "always", "on match" };
const char* msgfilter_enum[] = { "off", "say+spec", "team", "say+team+spec" };
const char* allowscripts_enum[] = { "off", "simple", "all" };
const char* scrautoid_enum[] = { "off", "nick", "health+armor", "health+armor+type", "all (rl)", "all (best gun)" };
const char* coloredtext_enum[] = { "off", "simple", "frag messages" };
const char* autorecord_enum[] = { "off", "don't save", "auto save" };
const char* hud_enum[] = { "classic", "new", "combined" };
const char* ignorespec_enum[] = { "off", "on (as player)", "on (always)" };

const char* SshotformatRead(void) {
	return scr_sshot_format.string;
}
void SshotformatToggle(qbool back) {
    if (back) SshotformatToggle(false);  // a trick: scroll forward twice = once back
	if (!strcmp(scr_sshot_format.string, "jpg")) Cvar_Set(&scr_sshot_format, "png");
	else if (!strcmp(scr_sshot_format.string, "png")) Cvar_Set(&scr_sshot_format, "tga");
	else if (!strcmp(scr_sshot_format.string, "tga")) Cvar_Set(&scr_sshot_format, "jpg");
}

extern cvar_t mvd_autotrack, mvd_moreinfo, mvd_status, cl_weaponpreselect, cl_weaponhide, con_funchars_mode, con_notifytime, scr_consize, ignore_opponents, _con_notifylines,
	ignore_qizmo_spec, ignore_spec, msg_filter, crosshair, crosshairsize, cl_smartjump, scr_coloredText,
	cl_rollangle, cl_rollspeed, v_gunkick, v_kickpitch, v_kickroll, v_kicktime, v_viewheight, match_auto_sshot, match_auto_record, match_auto_logconsole,
	r_fastturb, r_grenadetrail, cl_drawgun, r_viewmodelsize, r_viewmodeloffset, scr_clock, scr_gameclock, show_fps, rate, cl_c2sImpulseBackup,
	name, team, skin, topcolor, bottomcolor, cl_teamtopcolor, cl_teambottomcolor, cl_teamquadskin, cl_teampentskin, cl_teambothskin, /*cl_enemytopcolor, cl_enemybottomcolor, */
	cl_enemyquadskin, cl_enemypentskin, cl_enemybothskin, demo_dir, qizmo_dir, qwdtools_dir, cl_fakename,
	cl_chatsound, con_sound_mm1_volume, con_sound_mm2_volume, con_sound_spec_volume, con_sound_other_volume, s_khz,
	ruleset, scr_sshot_dir, log_dir, cl_nolerp
;
#ifdef _WIN32
extern cvar_t demo_format, sys_highpriority, cl_window_caption;
#endif
#ifdef GLQUAKE
extern cvar_t scr_autoid, gl_crosshairalpha, gl_smoothfont, amf_hidenails, amf_hiderockets, gl_anisotropy, gl_lumaTextures, gl_textureless, gl_colorlights;
#endif

const char* BandwidthRead(void) {
	if (rate.value < 4000) return "Modem (33k)";
	else if (rate.value < 6000) return "Modem (56k)";
	else if (rate.value < 8000) return "ISDN (112k)";
	else if (rate.value < 15000) return "Cable (128k)";
	else return "ADSL (> 256k)";
}
void BandwidthToggle(qbool back) {
    if (back) {
        int i; for (i = 0; i < 3; i++) BandwidthToggle(false);
    }
	if (rate.value < 4000) Cvar_SetValue(&rate, 5670);
	else if (rate.value < 6000) Cvar_SetValue(&rate, 7168);
	else if (rate.value < 8000) Cvar_SetValue(&rate, 14336);
	else if (rate.value < 15000) Cvar_SetValue(&rate, 30000);
	else Cvar_SetValue(&rate, 3800);
}
const char* ConQualityRead(void) {
	int q = (int) cl_c2sImpulseBackup.value;
	if (!q) return "Perfect";
	else if (q < 3) return "Low packetloss";
	else if (q < 5) return "Medium packetloss";
	else return "High packetloss";
}
void ConQualityToggle(qbool back) {
	int q = (int) cl_c2sImpulseBackup.value;
	if (!q) Cvar_SetValue(&cl_c2sImpulseBackup, 2);
	else if (q < 3) Cvar_SetValue(&cl_c2sImpulseBackup, 4);
	else if (q < 5) Cvar_SetValue(&cl_c2sImpulseBackup, 6);
	else Cvar_SetValue(&cl_c2sImpulseBackup, 0);
}
#ifdef _WIN32
const char* DemoformatRead(void) {
	return demo_format.string;
}
void DemoformatToggle(qbool back) {
    if (back) DemoformatToggle(false); // trick
	if (!strcmp(demo_format.string, "qwd")) Cvar_Set(&demo_format, "qwz");
	else if (!strcmp(demo_format.string, "qwz")) Cvar_Set(&demo_format, "mvd");
	else if (!strcmp(demo_format.string, "mvd")) Cvar_Set(&demo_format, "qwd");
}
#endif
const char* SoundqualityRead(void) {
	static char buf[7];
	snprintf(buf, sizeof(buf), "%.2s kHz", s_khz.string);
	return buf;
}
void SoundqualityToggle(qbool back) {
	switch ((int) s_khz.value) {
	case 11: Cvar_SetValue(&s_khz, 22); break;
	case 22: Cvar_SetValue(&s_khz, 44); break;
	default: Cvar_SetValue(&s_khz, 11); break;
	}
}
const char* RulesetRead(void) {
	return ruleset.string;
}
void RulesetToggle(qbool back) {
	if (back) RulesetToggle(false);
	if (!strcmp(ruleset.string, "default")) Cvar_Set(&ruleset, "smackdown");
	else if (!strcmp(ruleset.string, "smackdown")) Cvar_Set(&ruleset, "mtfl");
	else if (!strcmp(ruleset.string, "mtfl")) Cvar_Set(&ruleset, "default");
}
const char *mediaroot_enum[] = { "relative to exe", "relative to home", "full path" };

#ifdef _WIN32
void PriorityToggle(qbool back) {
    int newp = sys_highpriority.value + (back ? -1 : 1);
    if (newp < -1) newp = 1;
    if (newp > 1) newp = -1;
    Cvar_SetValue(&sys_highpriority, newp);
}
const char* PriorityRead(void) {
    switch ((int) sys_highpriority.value) {
    case -1: return "low";
    case 1: return "high";
    default: return "normal";
    }
}
#endif

// START contents of Menu-> Options-> Main tab

void DefaultConfig(void) { Cbuf_AddText("cfg_reset\n"); }

setting settgeneral_arr[] = {
	ADDSET_SEPARATOR("Miscellaneous"),
	ADDSET_ACTION	("QuakeWorld Help", M_Menu_Help_f, "Browse the \"QuakeWorld for Freshies\" guide by Apollyon."),
	ADDSET_ACTION	("Go To Console", Con_ToggleConsole_f, "Opens the console."),
	ADDSET_ACTION	("Reset To Defaults", DefaultConfig, "Reset all settings to defaults"),
#ifdef _WIN32
    ADDSET_CUSTOM	("Process Priority", PriorityRead, PriorityToggle, "Change client process priority. If you experience tearing or lagging, change this value to something that works for you."),
#endif
	ADDSET_BOOL		("Advanced Options", menu_advanced),
	
	//Connection
	ADDSET_SEPARATOR("Connection"),
	ADDSET_CUSTOM	("Bandwidth Limit", BandwidthRead, BandwidthToggle, "Select a speed close to your internet connection link speed."),
	ADDSET_CUSTOM	("Quality", ConQualityRead, ConQualityToggle, "Ensures that packets with weapon switch command don't get lost."),
	
	//Sound & Volume
	ADDSET_SEPARATOR("Sound & Volume"),
	ADDSET_NUMBER	("Primary Volume", s_volume, 0, 1, 0.05),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL		("Self Volume Levels", cl_chatsound),
	ADDSET_NUMBER	("Chat Volume", con_sound_mm1_volume, 0, 1, 0.1),
	ADDSET_NUMBER	("Team Chat Volume", con_sound_mm2_volume, 0, 1, 0.1),
	ADDSET_NUMBER	("Spectator Volume", con_sound_spec_volume, 0, 1, 0.1),
	ADDSET_NUMBER	("Other Volume", con_sound_other_volume, 0, 1, 0.1),
	ADDSET_BOOL		("Static Sounds", cl_staticsounds),
	ADDSET_CUSTOM	("Quality", SoundqualityRead, SoundqualityToggle, "Sound sampling rate."),
	ADDSET_BASIC_SECTION(),
	
	//Chat Settings
	ADDSET_SEPARATOR("Chat settings"),
	ADDSET_NAMED	("Ignore Opponents", ignore_opponents, ignoreopponents_enum),
	ADDSET_BOOL		("Ignore Observers", ignore_qizmo_spec),
	ADDSET_NAMED	("Ignore Spectators", ignore_spec, ignorespec_enum),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_NAMED	("Message Filtering", msg_filter, msgfilter_enum),
	ADDSET_BASIC_SECTION(),
	//Match Tools
	ADDSET_SEPARATOR("Match Tools"),
	ADDSET_BOOL		("Auto Screenshot", match_auto_sshot),
	ADDSET_NAMED	("Auto Record Demo", match_auto_record, autorecord_enum),
	ADDSET_NAMED	("Auto Log Match", match_auto_logconsole, autorecord_enum),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_CUSTOM	("Sshot Format", SshotformatRead, SshotformatToggle, "Screenshot image format"),
#ifdef _WIN32
	ADDSET_CUSTOM	("Demo Format", DemoformatRead, DemoformatToggle, "QWD is original QW demo format, QWZ is compressed demo format and MVD contains multiview data; You need Qizmo and Qwdtools for this to work."),
#endif
	ADDSET_BASIC_SECTION(),
	ADDSET_ADVANCED_SECTION(), // I think all of paths should be advanced? -Up2
	//Paths
	ADDSET_SEPARATOR("Paths"),
	ADDSET_NAMED    ("Media Paths Type", cl_mediaroot, mediaroot_enum),
	ADDSET_STRING   ("Screenshots Path", scr_sshot_dir),
	ADDSET_STRING	("Demos Path", demo_dir),
	ADDSET_STRING   ("Logs Path", log_dir),
	ADDSET_STRING	("Qizmo Path", qizmo_dir),
	ADDSET_STRING	("QWDTools Path", qwdtools_dir),
	ADDSET_BASIC_SECTION(),
};

settings_page settgeneral;

void CT_Opt_Settings_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x, y, w, h, &settgeneral);
}

int CT_Opt_Settings_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settgeneral, k);
}

void OnShow_SettMain(void) { Settings_OnShow(&settgeneral); }

qbool CT_Opt_Settings_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settgeneral, ms);
}

// </SETTINGS>
//=============================================================================


settings_page settmultiview;
setting settmultiview_arr[] = {
	ADDSET_SEPARATOR("Multiview"),
	ADDSET_NUMBER	("Multiview", cl_multiview, 0, 4, 1),
	ADDSET_BOOL		("Display HUD", cl_mvdisplayhud),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL		("HUD Flip", cl_mvhudflip),
	ADDSET_BOOL		("HUD Vertical", cl_mvhudvertical),
	ADDSET_BOOL		("Inset View", cl_mvinset),
	ADDSET_BOOL		("Inset HUD", cl_mvinsethud),
	ADDSET_BOOL		("Inset Cross", cl_mvinsetcrosshair),
	ADDSET_BASIC_SECTION(),
	ADDSET_SEPARATOR("Multiview Demos"),
	ADDSET_NAMED	("Autohud", mvd_autohud, mvdautohud_enum),
	ADDSET_NAMED	("Autotrack", mvd_autotrack, mvdautotrack_enum),
	ADDSET_BOOL		("Moreinfo", mvd_moreinfo), 
	ADDSET_BOOL     ("Status", mvd_status),
};

void CT_Opt_Multiview_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x, y, w, h, &settmultiview);
}

int CT_Opt_Multiview_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settmultiview, k);
}

void OnShow_SettMultiview(void) { Settings_OnShow(&settmultiview); }

qbool CT_Opt_Multiview_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settmultiview, ms);
}


settings_page setthud;
setting setthud_arr[] = {
	ADDSET_SEPARATOR("Head Up Display"),
	ADDSET_NAMED	("HUD Type", scr_newHud, hud_enum),
	ADDSET_NUMBER	("Crosshair", crosshair, 0, 7, 1),
	ADDSET_NUMBER	("Crosshair size", crosshairsize, 0.2, 3, 0.2),
#ifdef GLQUAKE
	ADDSET_NUMBER	("Crosshair alpha", gl_crosshairalpha, 0.1, 1, 0.1),
	ADDSET_NAMED	("Overhead Name", scr_autoid, scrautoid_enum),
#endif
	ADDSET_SEPARATOR("New HUD"),
	ADDSET_BOOLLATE	("Gameclock", hud_gameclock_show),
	ADDSET_BOOLLATE ("Big Gameclock", hud_gameclock_big),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOLLATE ("Teamholdbar", hud_teamholdbar_show),
	ADDSET_BOOLLATE ("Teamholdinfo", hud_teamholdinfo_show),
	ADDSET_BASIC_SECTION(),
	ADDSET_BOOLLATE ("FPS", hud_fps_show),
	ADDSET_BOOLLATE ("Clock", hud_clock_show),
#ifdef GLQUAKE
	ADDSET_BOOLLATE ("Radar", hud_radar_show),
#endif
	ADDSET_SEPARATOR("Quake Classic HUD"),
	ADDSET_BOOL		("Status Bar", cl_sbar),
	ADDSET_BOOL		("HUD Left", cl_hudswap),
	ADDSET_BOOL		("Show FPS", show_fps),
	ADDSET_BOOL		("Show Clock", scr_clock),
	ADDSET_BOOL		("Show Gameclock", scr_gameclock),
#ifdef GLQUAKE
	ADDSET_SEPARATOR("Tracker Messages"),
	ADDSET_BOOL		("Flags", amf_tracker_flags),
	ADDSET_BOOL		("Frags", amf_tracker_frags),
	ADDSET_NUMBER	("Messages", amf_tracker_messages, 0, 10, 1),
	ADDSET_BOOL		("Streaks", amf_tracker_streaks),
	ADDSET_NUMBER	("Time", amf_tracker_time, 0.5, 6, 0.5),
	ADDSET_NUMBER	("Scale", amf_tracker_scale, 0.1, 2, 0.1),
	ADDSET_BOOL		("Align Right", amf_tracker_align_right),
	ADDSET_SEPARATOR("Console"),
	ADDSET_NAMED	("Colored Text", scr_coloredText, coloredtext_enum),
	ADDSET_NAMED	("Fun Chars More", con_funchars_mode, funcharsmode_enum),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_NUMBER	("Notify Lines", _con_notifylines, 0, 16, 1),
	ADDSET_NUMBER	("Notify Time", con_notifytime, 0.5, 16, 0.5),
	ADDSET_BOOL		("Timestamps", con_timestamps),
	ADDSET_BASIC_SECTION(),
	ADDSET_BOOL		("Font Smoothing", gl_smoothfont),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_NUMBER	("Console height", scr_consize, 0.1, 1.0, 0.05),
	ADDSET_BASIC_SECTION(),

#endif

};

void CT_Opt_HUD_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x, y, w, h, &setthud);
}

int CT_Opt_HUD_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&setthud, k);
}

void OnShow_SettHUD(void) { Settings_OnShow(&setthud); }

qbool CT_Opt_HUD_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&setthud, ms);
}
//// PLAYER SETTINGS /////
settings_page settplayer;
setting settplayer_arr[] = {
	ADDSET_SEPARATOR("Player Settings"),
	ADDSET_STRING	("Name", name),
	ADDSET_STRING	("Teamchat Prefix", cl_fakename),
	ADDSET_STRING	("Team", team),
	ADDSET_SKIN		("Skin", skin),
	ADDSET_COLOR	("Shirt Color", topcolor),
	ADDSET_COLOR	("Pants Color", bottomcolor),
	ADDSET_CUSTOM	("Ruleset", RulesetRead, RulesetToggle, "If you are taking part in a tournament, you usually need to set this to smackdown; (this will limit some client features."),
	ADDSET_SEPARATOR("Team Colors"),
	ADDSET_COLOR	("Shirt Color", cl_teamtopcolor),
	ADDSET_COLOR	("Pants Color", cl_teambottomcolor),
	ADDSET_SKIN		("Skin", cl_teamskin),
	ADDSET_SKIN		("Quad Skin", cl_teamquadskin),
	ADDSET_SKIN		("Pent Skin", cl_teampentskin),
	ADDSET_SKIN		("Quad+Pent Skin", cl_teambothskin),
	ADDSET_SEPARATOR("Enemy Colors"),
	ADDSET_COLOR	("Shirt Color", cl_enemytopcolor),
	ADDSET_COLOR	("Pants Color", cl_enemybottomcolor),
	ADDSET_SKIN		("Skin", cl_enemyskin),
	ADDSET_SKIN		("Quad Skin", cl_enemyquadskin),
	ADDSET_SKIN		("Pent Skin", cl_enemypentskin),
	ADDSET_SKIN		("Quad+Pent Skin", cl_enemybothskin),
};

void CT_Opt_Player_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x, y, w, h, &settplayer);
}

int CT_Opt_Player_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settplayer, k);
}

void OnShow_SettPlayer(void) { Settings_OnShow(&settplayer); }

qbool CT_Opt_Player_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settplayer, ms);
}


//=============================================================================
// <BINDS>

extern cvar_t in_mouse, in_m_smooth, m_rate, in_m_os_parameters;
const char* in_mouse_enum[] = { "off", "system", "Direct Input" };
const char* in_m_os_parameters_enum[] = { "off", "Keep accel settings", "Keep speed settings", "Keep all settings" };

void Menu_Input_Restart(void) { Cbuf_AddText("in_restart\n"); }

settings_page settbinds;
setting settbinds_arr[] = {
	ADDSET_SEPARATOR("Movement"),
	ADDSET_BIND("Attack", "+attack"),
	ADDSET_BIND("Jump/Swim up", "+jump"),
	ADDSET_BIND("Move Forward", "+forward"),
	ADDSET_BIND("Move Backward", "+back"),
	ADDSET_BIND("Move Left", "+moveleft"),
	ADDSET_BIND("Move Right", "+moveright"),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BIND("Swim Up", "+moveup"),
	ADDSET_BASIC_SECTION(),
	ADDSET_BIND("Swim Down", "+movedown"),
	ADDSET_BIND("Zoom In/Out", "+zoom"),

	ADDSET_SEPARATOR("Weapons"),
	ADDSET_BIND("Previous Weapon", "impulse 12"),
	ADDSET_BIND("Next Weapon", "impulse 10"),
	ADDSET_BIND("Axe", "weapon 1"),
	ADDSET_BIND("Shotgun", "weapon 2"),
	ADDSET_BIND("Super Shotgun", "weapon 3"),
	ADDSET_BIND("Nailgun", "weapon 4"),
	ADDSET_BIND("Super Nailgun", "weapon 5"),
	ADDSET_BIND("Grenade Launcher", "weapon 6"),
	ADDSET_BIND("Rocket Launcher", "weapon 7"),
	ADDSET_BIND("Thunderbolt", "weapon 8"),
	
	ADDSET_SEPARATOR("Teamplay"),
	ADDSET_BIND("Report Status", "tp_msgreport"),
	ADDSET_BIND("Lost location", "tp_msglost"),
	ADDSET_BIND("Location safe", "tp_msgsafe"),
	ADDSET_BIND("Point at item", "tp_msgpoint"),
	ADDSET_BIND("Took item", "tp_msgtook"),
	ADDSET_BIND("Need items", "tp_msgneed"),
	ADDSET_BIND("Coming from location", "tp_msgcoming"),
	ADDSET_BIND("Help location", "tp_msghelp"),
	ADDSET_BIND("Enemy Quad Dead", "tp_msgquaddead"),
	ADDSET_BIND("Enemy has Powerup", "tp_msgenemypwr"),
	ADDSET_BIND("Get Quad", "tp_msggetquad"),
	ADDSET_BIND("Get Pent", "tp_msggetpent"),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BIND("Trick at location", "tp_msgtrick"),
	ADDSET_BIND("Replace at location", "tp_msgreplace"),
	ADDSET_BASIC_SECTION(),

	ADDSET_SEPARATOR("Communication"),
	ADDSET_BIND("Chat", "messagemode"),
	ADDSET_BIND("Teamchat", "messagemode2"),	
	ADDSET_BIND("Proxy Menu", "toggleproxymenu"),

	ADDSET_SEPARATOR("Miscellaneous"),
	ADDSET_BIND("Show Scores", "+showscores"),
	ADDSET_BIND("Screenshot", "screenshot"),
	ADDSET_BIND("Quit", "quit"),

	ADDSET_SEPARATOR("Mouse Settings"),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL		("Freelook", freelook),
	ADDSET_BASIC_SECTION(),
	ADDSET_NUMBER	("Sensitivity", sensitivity, 1, 15, 0.25),
	ADDSET_NUMBER	("Acceleration", m_accel, 0, 1, 0.1),
	ADDSET_CUSTOM	("Invert Mouse", InvertMouseRead, InvertMouseToggle, "Invert mouse will make you look down when the mouse is moved up."),
#ifdef _WIN32
    ADDSET_ADVANCED_SECTION(),
    ADDSET_STRING   ("X sensitivity", m_yaw),
    ADDSET_STRING   ("Y sensitivity", m_pitch),
    ADDSET_NAMED    ("Mouse Input Type", in_mouse, in_mouse_enum),
    ADDSET_BOOL     ("DInput: Smoothing", in_m_smooth),
    ADDSET_STRING   ("DInput: Rate (Hz)", m_rate),
    ADDSET_NAMED    ("OS Mouse: Parms.", in_m_os_parameters, in_m_os_parameters_enum),
    ADDSET_ACTION   ("Apply", Menu_Input_Restart, "Will restart the mouse input module and apply settings."),
    ADDSET_BASIC_SECTION(),
#endif
    ADDSET_SEPARATOR("Weapon Handling"),
	ADDSET_CUSTOM	("Gun Autoswitch", AutoSWRead, AutoSWToggle, "Switches to picked up weapon if more powerful than what you're holding."),
	ADDSET_BOOL		("Gun Preselect", cl_weaponpreselect),
	ADDSET_BOOL		("Gun Auto hide", cl_weaponhide),
    ADDSET_SEPARATOR("Movement"),
	ADDSET_CUSTOM	("Always Run", AlwaysRunRead, AlwaysRunToggle, "Maximum forward speed at all times."),
	ADDSET_ADVANCED_SECTION(),
    ADDSET_BOOL		("Smart Jump", cl_smartjump),
	ADDSET_NAMED	("Movement Scripts", allow_scripts, allowscripts_enum),
	ADDSET_BASIC_SECTION(),
	
	ADDSET_SEPARATOR("Demo Playback"),
	ADDSET_BIND("Stop", "disconnect"),
	ADDSET_BIND("Play", "cl_demospeed 1;echo Playing demo."),
	ADDSET_BIND("Pause", "cl_demospeed 0;echo Demo paused.")
};

void CT_Opt_Binds_Draw (int x2, int y2, int w, int h, CTab_t *tab, CTabPage_t *page) {
	Settings_Draw(x2, y2, w, h, &settbinds);
}

int CT_Opt_Binds_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settbinds, k);
}

void OnShow_SettBinds(void) { Settings_OnShow(&settbinds); }

qbool CT_Opt_Binds_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settbinds, ms);
}

// </BINDS>
//=============================================================================



//=============================================================================
// <FPS>
extern cvar_t v_bonusflash;
extern cvar_t cl_rocket2grenade;
extern cvar_t v_damagecshift;
extern cvar_t r_fastsky;
extern cvar_t r_drawflame;
#ifdef GLQUAKE
extern cvar_t gl_part_inferno;
extern cvar_t amf_lightning;
#endif
extern cvar_t r_drawflat;

const char* explosiontype_enum[] =
{ "fire + sparks", "fire only", "teleport", "blood", "big blood", "dbl gunshot", "blob effect", "big explosion", "plasma" };

const char* muzzleflashes_enum[] =
{ "off", "on", "own off" };

const char* deadbodyfilter_enum[] = 
{ "off", "decent", "instant" };

const char* rocketmodel_enum[] =
{ "rocket", "grenade" };

const char* rockettrail_enum[] =
{ "off", "normal", "grenade", "alt normal", "slight blood", "big blood", "tracer 1", "tracer 2", "plasma" };

const char* powerupglow_enum[] =
{ "off", "on", "own off" };

void LoadFastPreset(void) {
	Cvar_SetValue (&r_explosiontype, 1);
	Cvar_SetValue (&r_explosionlight, 0);
	Cvar_SetValue (&cl_muzzleflash, 0);
	Cvar_SetValue (&cl_gibfilter, 1);
	Cvar_SetValue (&cl_deadbodyfilter, 1);
	Cvar_SetValue (&r_rocketlight, 0);
	Cvar_SetValue (&r_powerupglow, 0);
	Cvar_SetValue (&r_drawflame, 0);
	Cvar_SetValue (&r_fastsky, 1);
	Cvar_SetValue (&r_rockettrail, 1);
	Cvar_SetValue (&v_damagecshift, 0);
#ifdef GLQUAKE
	Cvar_SetValue (&gl_flashblend, 1);
	Cvar_SetValue (&r_dynamic, 0);
	Cvar_SetValue (&gl_part_explosions, 0);
	Cvar_SetValue (&gl_part_trails, 0);
	Cvar_SetValue (&gl_part_spikes, 0);
	Cvar_SetValue (&gl_part_gunshots, 0);
	Cvar_SetValue (&gl_part_blood, 0);
	Cvar_SetValue (&gl_part_telesplash, 0);
	Cvar_SetValue (&gl_part_blobs, 0);
	Cvar_SetValue (&gl_part_lavasplash, 0);
	Cvar_SetValue (&gl_part_inferno, 0);
#endif
}

void LoadHQPreset(void) {
	Cvar_SetValue (&r_explosiontype, 0);
	Cvar_SetValue (&r_explosionlight, 1);
	Cvar_SetValue (&cl_muzzleflash, 1);
	Cvar_SetValue (&cl_gibfilter, 0);
	Cvar_SetValue (&cl_deadbodyfilter, 0);
	Cvar_SetValue (&r_rocketlight, 1);
	Cvar_SetValue (&r_powerupglow, 2);
	Cvar_SetValue (&r_drawflame, 1);
	Cvar_SetValue (&r_fastsky, 0);
	Cvar_SetValue (&r_rockettrail, 1);
	Cvar_SetValue (&v_damagecshift, 1);
#ifdef GLQUAKE
	Cvar_SetValue (&gl_flashblend, 0);
	Cvar_SetValue (&r_dynamic, 1);
	Cvar_SetValue (&gl_part_explosions, 1);
	Cvar_SetValue (&gl_part_trails, 1);
	Cvar_SetValue (&gl_part_spikes, 1);
	Cvar_SetValue (&gl_part_gunshots, 1);
	Cvar_SetValue (&gl_part_blood, 1);
	Cvar_SetValue (&gl_part_telesplash, 1);
	Cvar_SetValue (&gl_part_blobs, 1);
	Cvar_SetValue (&gl_part_lavasplash, 1);
	Cvar_SetValue (&gl_part_inferno, 1);
#endif
}

const char* grenadetrail_enum[] = { "off", "normal", "grenade", "alt normal", "slight blood", "big blood", "tracer 1", "tracer 2", "plasma", "lavaball", "fuel rod", "plasma rocket" };

extern cvar_t cl_maxfps;
const char* FpslimitRead(void) {
	switch ((int) cl_maxfps.value) {
	case 0: return "no limit";
	case 72: return "72 (old std)";
	case 77: return "77 (standard)";
	default: return "custom";
	}
}
void FpslimitToggle(qbool back) {
	if (back) {
		switch ((int) cl_maxfps.value) {
		case 0: Cvar_SetValue(&cl_maxfps, 77); return;
		case 72: Cvar_SetValue(&cl_maxfps, 0); return;
		case 77: Cvar_SetValue(&cl_maxfps, 72); return;
		}
	} else {
		switch ((int) cl_maxfps.value) {
		case 0: Cvar_SetValue(&cl_maxfps, 72); return;
		case 72: Cvar_SetValue(&cl_maxfps, 77); return;
		case 77: Cvar_SetValue(&cl_maxfps, 0); return;
		}
	}
}

#ifdef GLQUAKE
const char* TexturesdetailRead(void) { // 1, 8, 256, 2048
	if (gl_max_size.value < 8) return "low";
	else if (gl_max_size.value < 200) return "medium";
	else if (gl_max_size.value < 1025) return "high";
	else return "max";
}
void TexturesdetailToggle(qbool back) {
	if (gl_max_size.value < 8) Cvar_SetValue(&gl_max_size, 8);
	else if (gl_max_size.value < 200) Cvar_SetValue(&gl_max_size, 256);
	else if (gl_max_size.value < 1025) Cvar_SetValue(&gl_max_size, 2048);
	else Cvar_SetValue(&gl_max_size, 1);
}
extern cvar_t gl_texturemode;
static int Texturesquality(void) {
	if (!strcmp(gl_texturemode.string, "GL_NEAREST")) return 0;
	else if (!strcmp(gl_texturemode.string, "GL_NEAREST_MIPMAP_NEAREST")) return 1;
	else if (!strcmp(gl_texturemode.string, "GL_LINEAR")) return 2;
	else if (!strcmp(gl_texturemode.string, "GL_LINEAR_MIPMAP_NEAREST")) return 3;
	else return 4;
}
const char* TexturesqualityRead(void) {
	switch (Texturesquality()) {
	case 0: return "very low"; case 1: return "low"; case 2: return "medium"; case 3: return "high"; default: return "very high";
	}
}
void TexturesqualityToggle(qbool back) {
	switch (Texturesquality()) {
	case 0: Cvar_Set(&gl_texturemode, "GL_NEAREST_MIPMAP_NEAREST"); return;
	case 1: Cvar_Set(&gl_texturemode, "GL_LINEAR"); return;
	case 2: Cvar_Set(&gl_texturemode, "GL_LINEAR_MIPMAP_NEAREST"); return;
	case 3: Cvar_Set(&gl_texturemode, "GL_LINEAR_MIPMAP_LINEAR"); return;
	default: Cvar_Set(&gl_texturemode, "GL_NEAREST"); return;
	}
}
#endif

// START contents of Menu -> Options -> Graphics tab

setting settfps_arr[] = {
	ADDSET_SEPARATOR("Presets"),
	ADDSET_ACTION	("Load Fast Preset", LoadFastPreset, "Adjust for high performance."),
	ADDSET_ACTION	("Load HQ preset", LoadHQPreset, "Adjust for high image-quality."),
	ADDSET_CUSTOM	("GFX Preset", GFXPresetRead, GFXPresetToggle, "Select different graphics effects presets here."),
	
	ADDSET_SEPARATOR("Miscellaneous"),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL		("Disable lin. interp.", cl_nolerp),
	ADDSET_BASIC_SECTION(),
	ADDSET_NAMED	("Muzzleflashes", cl_muzzleflash, muzzleflashes_enum),
	ADDSET_NUMBER	("Damage Flash", v_damagecshift, 0, 1, 0.1),
	ADDSET_BOOL		("Pickup Flashes", v_bonusflash),
	ADDSET_BOOL		("Fullbright skins", r_fullbrightSkins),
	
	ADDSET_SEPARATOR("Environment"),

	ADDSET_BOOL		("Simple Sky", r_fastsky),
	ADDSET_BOOL		("Simple walls", r_drawflat),
	ADDSET_BOOL		("Simple turbs", r_fastturb), 
	ADDSET_BOOL		("Draw flame", r_drawflame),
	ADDSET_BOOL		("Gib Filter", cl_gibfilter),
	ADDSET_NAMED	("Dead Body Filter", cl_deadbodyfilter, deadbodyfilter_enum),
	ADDSET_SEPARATOR("Projectiles"),
	
	ADDSET_NAMED	("Explosion Type", r_explosiontype, explosiontype_enum),
	ADDSET_NAMED	("Rocket Model", cl_rocket2grenade, rocketmodel_enum),
	ADDSET_NAMED	("Rocket Trail", r_rockettrail, rockettrail_enum),
	ADDSET_BOOL		("Rocket Light", r_rocketlight),
	ADDSET_NAMED	("Grenade Trail", r_grenadetrail, grenadetrail_enum),
	ADDSET_NUMBER	("Fakeshaft", cl_fakeshaft, 0, 1, 0.05),
#ifdef GLQUAKE
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL		("Hide Nails", amf_hidenails),
	ADDSET_BOOL		("Hide Rockets", amf_hiderockets),
	ADDSET_BASIC_SECTION(),
#endif
	ADDSET_SEPARATOR("Lighting"),
	ADDSET_NAMED	("Powerup Glow", r_powerupglow, powerupglow_enum),
#ifdef GLQUAKE
	ADDSET_BOOL		("Colored Lights", gl_colorlights),
	ADDSET_BOOL		("Fast Lights", gl_flashblend),
	ADDSET_BOOL		("Dynamic Ligts", r_dynamic),
	ADDSET_NUMBER	("Light mode", gl_lightmode, 0, 2, 1),
	ADDSET_BOOL		("Particle Shaft", amf_lightning),
#endif
	ADDSET_SEPARATOR("Weapon Model"),
#ifdef GLQUAKE
	ADDSET_NUMBER	("Opacity", cl_drawgun, 0, 1, 0.05),
#else
	ADDSET_BOOL		("Show", cl_drawgun),
#endif
	ADDSET_NUMBER	("Size", r_viewmodelsize, 0.1, 1, 0.05),
	ADDSET_NUMBER	("Shift", r_viewmodeloffset, -10, 10, 1),
#ifdef GLQUAKE
	ADDSET_ADVANCED_SECTION(),
	ADDSET_SEPARATOR("Textures"),
	ADDSET_BOOL		("Luma", gl_lumaTextures),
	ADDSET_CUSTOM	("Detail", TexturesdetailRead, TexturesdetailToggle, "Determines the texture quality; resolution of the textures in memory."),
	ADDSET_NUMBER	("Miptex", gl_miptexLevel, 0, 3, 1),
	ADDSET_BOOL		("No Textures", gl_textureless),
	
	ADDSET_BASIC_SECTION(),
	ADDSET_SEPARATOR("Field of View"),
	ADDSET_NUMBER	("View Size (fov)", scr_fov, 40, 140, 2),
	ADDSET_NUMBER	("Screen Size", scr_viewsize, 30, 120, 5),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_NUMBER	("Rollangle", cl_rollangle, 0, 30, 2),
	ADDSET_NUMBER	("Rollspeed", cl_rollspeed, 0, 30, 2),
	ADDSET_BOOL		("Gun Kick", v_gunkick),
	ADDSET_NUMBER	("Kick Pitch", v_kickpitch, 0, 10, 0.5),
	ADDSET_NUMBER	("Kick Roll", v_kickroll, 0, 10, 0.5),
	ADDSET_NUMBER	("Kick Time", v_kicktime, 0, 10, 0.5),
	ADDSET_NUMBER	("View Height", v_viewheight, -7, 6, 0.5),
	ADDSET_BASIC_SECTION(),
#endif
};

settings_page settfps;

void CT_Opt_FPS_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
	Settings_Draw(x, y, w, h, &settfps);
}

int CT_Opt_FPS_Key (int k, CTab_t *tab, CTabPage_t *page) {
	return Settings_Key(&settfps, k);
}

void OnShow_SettFPS(void) { Settings_OnShow(&settfps); }

qbool CT_Opt_FPS_Mouse_Event(const mouse_state_t *ms)
{
	return Settings_Mouse_Event(&settfps, ms);
}


// </FPS>
//=============================================================================


//=============================================================================
// <VIDEO>

#ifdef GLQUAKE

// these variables serve as a temporary storage for user selected settings
// they get initialized with current settings when the page is showed
typedef struct menu_video_settings_s {
	int res;
	int bpp;
	qbool fullscreen;
	cvar_t freq;	// this is not an int just because we need to fool settings_page module
} menu_video_settings_t;
qbool mvs_askmode = false;

// here we store the configuration that user selected in menu
menu_video_settings_t mvs_selected;	

// here we store the current video config in case the new video settings weren't successfull
menu_video_settings_t mvs_previous;	

// will apply given video settings
static void ApplyVideoSettings(const menu_video_settings_t *s) {
#ifndef __APPLE__
	Cvar_SetValue(&r_mode, s->res);
	Cvar_SetValue(&r_colorbits, s->bpp);
	Cvar_SetValue(&r_displayRefresh, s->freq.value);
	Cvar_SetValue(&r_fullscreen, s->fullscreen);
#endif
	Cbuf_AddText("vid_restart\n");
    Com_Printf("askmode: %s\n", mvs_askmode ? "on" : "off");
}

// will store current video settings into the given structure
static void StoreCurrentVideoSettings(menu_video_settings_t *out) {
#ifndef __APPLE__
	out->res = (int) r_mode.value;
	out->bpp = (int) r_colorbits.value;
	Cvar_SetValue(&out->freq, r_displayRefresh.value);
	out->fullscreen = (int) r_fullscreen.value;
#endif
}

// performed when user hits the "apply" button
void VideoApplySettings (void)
{ 
	StoreCurrentVideoSettings(&mvs_previous);

	ApplyVideoSettings(&mvs_selected);

	mvs_askmode = true;
}

// two possible results of the "keep these video settings?" dialogue
static void KeepNewVideoSettings (void) { mvs_askmode = false; }
static void CancelNewVideoSettings (void) { 
	mvs_askmode = false;
	ApplyVideoSettings(&mvs_previous);
}

// this is a duplicate from tr_init.c!
const char* glmodes[] = { "320x240", "400x300", "512x384", "640x480", "800x600", "960x720", "1024x768", "1152x864", "1280x1024", "1600x1200", "2048x1536" };
int glmodes_size = sizeof(glmodes) / sizeof(char*);

const char* BitDepthRead(void) { return mvs_selected.bpp == 32 ? "32 bit" : mvs_selected.bpp == 16 ? "16 bit" : "use desktop settings"; }
const char* ResolutionRead(void) { return glmodes[bound(0, mvs_selected.res, glmodes_size-1)]; }
const char* FullScreenRead(void) { return mvs_selected.fullscreen ? "on" : "off"; }

void ResolutionToggle(qbool back) {
	if (back) mvs_selected.res--; else mvs_selected.res++;
	mvs_selected.res = (mvs_selected.res + glmodes_size) % glmodes_size;
}
void BitDepthToggle(qbool back) {
	if (back) {
		switch (mvs_selected.bpp) {
		case 0: mvs_selected.bpp = 32; return;
		case 16: mvs_selected.bpp = 0; return;
		default: mvs_selected.bpp = 16; return;
		}
	} else {
		switch (mvs_selected.bpp) {
		case 0: mvs_selected.bpp = 16; return;
		case 16: mvs_selected.bpp = 32; return;
		case 32: mvs_selected.bpp = 0; return;
		}
	}
}
void FullScreenToggle(qbool back) { mvs_selected.fullscreen = mvs_selected.fullscreen ? 0 : 1; }

setting settvideo_arr[] = {
	//Video
	ADDSET_SEPARATOR("Video"),
	ADDSET_NUMBER	("Gamma", v_gamma, 0.1, 2.0, 0.1),
	ADDSET_NUMBER	("Contrast", v_contrast, 1, 5, 0.1),
	ADDSET_NUMBER	("Anisotropy filter", gl_anisotropy, 0, 16, 1),
	ADDSET_CUSTOM	("Quality Mode", TexturesqualityRead, TexturesqualityToggle, "Determines the texture quality; rendering quality."),
	
	ADDSET_SEPARATOR("Screen settings"),
	ADDSET_CUSTOM("Resolution", ResolutionRead, ResolutionToggle, "Change your screen resolution."),
#ifdef GLQUAKE
#ifndef __APPLE__
	ADDSET_BOOL("Vertical sync", r_swapInterval),
#endif
#endif
	ADDSET_CUSTOM("Bit depth", BitDepthRead, BitDepthToggle, "Choose 16bit or 32bit color mode for your screen."),
	ADDSET_CUSTOM("Fullscreen", FullScreenRead, FullScreenToggle, "Toggle between fullscreen and windowed mode."),
	ADDSET_STRING("Refresh frequency", mvs_selected.freq),
	ADDSET_ACTION("Apply changes", VideoApplySettings, "Restarts the renderer and applies the selected resolution."),
	
	ADDSET_SEPARATOR("Font Size"),
#ifndef __APPLE__
	ADDSET_NUMBER("Width", r_conwidth, 320, 2048, 8),
	ADDSET_NUMBER("Height", r_conheight, 240, 1538, 4),
#endif

	ADDSET_SEPARATOR("Miscellaneous"),
	
	ADDSET_CUSTOM	("FPS Limit", FpslimitRead, FpslimitToggle, "Limits the amount of frames rendered per second. May help with lag; best to consult forums about the best value for your setup."),
	ADDSET_ADVANCED_SECTION(),
#ifdef GLQUAKE
	ADDSET_NUMBER("Draw Distance", r_farclip, 4096, 8192, 4096),
	ADDSET_BASIC_SECTION(),
#ifdef _WIN32
	ADDSET_BOOL("Activity Flash", vid_flashonactivity),
	ADDSET_BOOL("New Caption", cl_window_caption)
#endif
#endif
};
settings_page settvideo;

#endif

void CT_Opt_Video_Draw (int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page) {
#ifndef GLQUAKE

	// Software Rendering version menu

	(*vid_menudrawfn) ();

#else

	// (Open)GL version menu

#define ASKBOXWIDTH 300

	if (mvs_askmode) {
		UI_DrawBox((w-ASKBOXWIDTH)/2, h/2 - 16, ASKBOXWIDTH, 32);
		UI_Print_Center((w-ASKBOXWIDTH)/2, h/2 - 8, ASKBOXWIDTH, "Do you wish to keep these settings?", false);
		UI_Print_Center((w-ASKBOXWIDTH)/2, h/2, ASKBOXWIDTH, "(y/n)", true);
	} else
		Settings_Draw(x,y,w,h, &settvideo);

#endif
}

int CT_Opt_Video_Key (int key, CTab_t *tab, CTabPage_t *page) {
#ifndef GLQUAKE
	
	// Software Rendering version menu

	(*vid_menukeyfn) (key);
	
	// i was too lazy&scared to change vid_menukeyfn functions out there
	// so because i know what keys have some function in there, i list them here:
	return key == K_LEFTARROW || key == K_RIGHTARROW || key == K_DOWNARROW || key == K_UPARROW || key == K_ENTER || key == 'd';

#else

	// (Open)GL version menu
	
	if (mvs_askmode) {
		if (key == 'y' || key == K_ENTER)
			KeepNewVideoSettings();
		else if (key == 'n' || key == K_ESCAPE)
			CancelNewVideoSettings();

		return true;
	} else
		return Settings_Key(&settvideo, key);

#endif
}

void OnShow_SettVideo(void) {
#ifdef GLQUAKE

	StoreCurrentVideoSettings(&mvs_selected);

#endif
}

qbool CT_Opt_Video_Mouse_Event(const mouse_state_t *ms)
{
#ifdef GLQUAKE
	return Settings_Mouse_Event(&settvideo, ms);
#else
	return false;
#endif
}


// </VIDEO>

// *********
// <CONFIG>

// Menu Options Config Page Mode

filelist_t configs_filelist;
cvar_t  cfg_browser_showsize		= {"cfg_browser_showsize",		"1"};
cvar_t  cfg_browser_showdate		= {"cfg_browser_showdate",		"1"};
cvar_t  cfg_browser_showtime		= {"cfg_browser_showtime",		"1"};
cvar_t  cfg_browser_sortmode		= {"cfg_browser_sortmode",		"1"};
cvar_t  cfg_browser_showstatus		= {"cfg_browser_showstatus",	"1"};
cvar_t  cfg_browser_stripnames		= {"cfg_browser_stripnames",	"1"};
cvar_t  cfg_browser_interline		= {"cfg_browser_interline",	"0"};
cvar_t  cfg_browser_scrollnames	= {"cfg_browser_scrollnames",	"1"};
cvar_t	cfg_browser_democolor		= {"cfg_browser_democolor",	"255 255 255 255"};	// White.
cvar_t	cfg_browser_selectedcolor	= {"cfg_browser_selectedcolor","0 150 235 255"};	// Light blue.
cvar_t	cfg_browser_dircolor		= {"cfg_browser_dircolor",		"170 80 0 255"};	// Redish.
#ifdef WITH_ZIP
cvar_t	cfg_browser_zipcolor		= {"cfg_browser_zipcolor",		"255 170 0 255"};	// Orange.
#endif

CEditBox filenameeb;

enum { MOCPM_SETTINGS, MOCPM_CHOOSECONFIG, MOCPM_CHOOSESCRIPT, MOCPM_ENTERFILENAME } MOpt_configpage_mode = MOCPM_SETTINGS;

extern cvar_t cfg_backup, cfg_legacy_exec, cfg_legacy_write, cfg_save_aliases, cfg_save_binds, cfg_save_cmdline,
	cfg_save_cmds, cfg_save_cvars, cfg_save_unchanged, cfg_save_userinfo, cfg_use_home;

void MOpt_ImportConfig(void) { 
	MOpt_configpage_mode = MOCPM_CHOOSECONFIG;
    if (cfg_use_home.value)
        FL_SetCurrentDir(&configs_filelist, com_homedir);
    else
	    FL_SetCurrentDir(&configs_filelist, "./ezquake/configs");
}
void MOpt_ExportConfig(void) { 
	MOpt_configpage_mode = MOCPM_ENTERFILENAME;
	filenameeb.text[0] = 0;
	filenameeb.pos = 0;
}

void MOpt_LoadScript(void) {
	MOpt_configpage_mode = MOCPM_CHOOSESCRIPT;
	FL_SetCurrentDir(&configs_filelist, "./ezquake/cfg");
}

void MOpt_CfgSaveAllOn(void) {
	Cvar_SetValue(&cfg_backup, 1);
	Cvar_SetValue(&cfg_legacy_exec, 1);
	Cvar_SetValue(&cfg_legacy_write, 0);
	Cvar_SetValue(&cfg_save_aliases, 1);
	Cvar_SetValue(&cfg_save_binds, 1);
	Cvar_SetValue(&cfg_save_cmdline, 1);
	Cvar_SetValue(&cfg_save_cmds, 1);
	Cvar_SetValue(&cfg_save_cvars, 1);
	Cvar_SetValue(&cfg_save_unchanged, 1);
	Cvar_SetValue(&cfg_save_userinfo, 2);
}

const char* MOpt_legacywrite_enum[] = { "off", "non-qw dir frontend.cfg", "also config.cfg", "non-qw config.cfg" };
const char* MOpt_userinfo_enum[] = { "off", "all but player", "all" };

void MOpt_LoadCfg(void) { Cbuf_AddText("cfg_load\n"); }
void MOpt_SaveCfg(void) { Cbuf_AddText("cfg_save\n"); }

settings_page settconfig;
setting settconfig_arr[] = {
    ADDSET_SEPARATOR("Load & Save"),
    ADDSET_ACTION("Reload settings", MOpt_LoadCfg, "Reset the settings to last saved configuration."),
    ADDSET_ACTION("Save settings", MOpt_SaveCfg, "Save the settings"),
	ADDSET_ADVANCED_SECTION(),
    ADDSET_BOOL("Save to profile dir", cfg_use_home),
    ADDSET_BASIC_SECTION(),
    ADDSET_SEPARATOR("Export & Import"),
	ADDSET_ACTION("Import config ...", MOpt_ImportConfig, "You can load a configuration from a file here."),
	ADDSET_ACTION("Export config ...", MOpt_ExportConfig, "Will export your current configuration to a file."),
    ADDSET_SEPARATOR("Scripts"),
	ADDSET_ACTION("Load Script", MOpt_LoadScript, "Choose and load quake scripts here."),
	ADDSET_SEPARATOR("Config Saving Options"),
    ADDSET_ACTION("Default Save Opt.", MOpt_CfgSaveAllOn, "Configuration saving settings will be reset to defaults."),
    ADDSET_BOOL("Save Unchanged Opt.", cfg_save_unchanged),
	ADDSET_ADVANCED_SECTION(),
	ADDSET_BOOL("Backup old file", cfg_backup),
	ADDSET_NAMED("Load Legacy", cfg_legacy_exec, MOpt_legacywrite_enum),
	ADDSET_BOOL("Save Legacy", cfg_legacy_write),
	ADDSET_BOOL("Aliases", cfg_save_aliases),
	ADDSET_BOOL("Binds", cfg_save_binds),
	ADDSET_BOOL("Cmdline", cfg_save_cmdline),
	ADDSET_BOOL("Init Commands", cfg_save_cmds),
	ADDSET_BOOL("Variables", cfg_save_cvars),
	ADDSET_NAMED("Userinfo", cfg_save_userinfo, MOpt_userinfo_enum)
};

#define INPUTBOXWIDTH 300
#define INPUTBOXHEIGHT 48

void MOpt_FilenameInputBoxDraw(int x, int y, int w, int h)
{
	UI_DrawBox(x + (w-INPUTBOXWIDTH) / 2, y + (h-INPUTBOXHEIGHT) / 2, INPUTBOXWIDTH, INPUTBOXHEIGHT);
	UI_Print_Center(x, y + h/2 - 8, w, "Enter the config file name", false);
	CEditBox_Draw(&filenameeb, x + (w-INPUTBOXWIDTH)/2 + 16, y + h/2 + 8, true);
}

qbool MOpt_FileNameInputBoxKey(int key)
{
	CEditBox_Key(&filenameeb, key);
	return true;
}

char *MOpt_FileNameInputBoxGetText(void)
{
	return filenameeb.text;
}

void CT_Opt_Config_Draw(int x, int y, int w, int h, CTab_t *tab, CTabPage_t *page)
{
	switch (MOpt_configpage_mode) {
	case MOCPM_SETTINGS:
		Settings_Draw(x,y,w,h,&settconfig);
		break;

	case MOCPM_CHOOSESCRIPT:
	case MOCPM_CHOOSECONFIG:
		FL_Draw(&configs_filelist, x,y,w,h);
		break;

	case MOCPM_ENTERFILENAME:
		MOpt_FilenameInputBoxDraw(x,y,w,h);
		break;
	}
}

int CT_Opt_Config_Key(int key, CTab_t *tab, CTabPage_t *page)
{
	switch (MOpt_configpage_mode) {
	case MOCPM_SETTINGS:
		return Settings_Key(&settconfig, key);
		break;
	
	case MOCPM_CHOOSECONFIG:
		if (key == K_ENTER || key == K_MOUSE1) {
			Cbuf_AddText(va("cfg_load \"%s\"\n", COM_SkipPath(FL_GetCurrentEntry(&configs_filelist)->name)));
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
		} else if (key == K_ESCAPE || key == K_MOUSE2) {
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
		} else return FL_Key(&configs_filelist, key);
		
	case MOCPM_CHOOSESCRIPT:
		if (key == K_ENTER || key == K_MOUSE1) {
			Cbuf_AddText(va("exec \"cfg/%s\"\n", COM_SkipPath(FL_GetCurrentEntry(&configs_filelist)->name)));
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
		} else if (key == K_ESCAPE || key == K_MOUSE2) {
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
		} else return FL_Key(&configs_filelist, key);

	case MOCPM_ENTERFILENAME:
		if (key == K_ENTER || key == K_MOUSE1) {
			Cbuf_AddText(va("cfg_save \"%s\"\n", MOpt_FileNameInputBoxGetText()));
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
        } else if (key == K_ESCAPE || key == K_MOUSE2) {
			MOpt_configpage_mode = MOCPM_SETTINGS;
			return true;
		} else return MOpt_FileNameInputBoxKey(key);
	}

	return false;
}

void OnShow_SettConfig(void) { Settings_OnShow(&settconfig); }

qbool CT_Opt_Config_Mouse_Event(const mouse_state_t *ms)
{
    if (MOpt_configpage_mode == MOCPM_CHOOSECONFIG || MOpt_configpage_mode == MOCPM_CHOOSESCRIPT) {
        if (FL_Mouse_Event(&configs_filelist, ms))
            return true;
        else if (ms->button_up == 1 || ms->button_up == 2)
            return CT_Opt_Config_Key(K_MOUSE1 - 1 + ms->button_up, &options_tab, options_tab.pages + OPTPG_CONFIG);

        return true;
    }
    else
    {
        return Settings_Mouse_Event(&settconfig, ms);
    }
}


// </CONFIG>
// *********

CTabPage_Handlers_t options_main_handlers = {
	CT_Opt_Settings_Draw,
	CT_Opt_Settings_Key,
	OnShow_SettMain,
	CT_Opt_Settings_Mouse_Event
};

CTabPage_Handlers_t options_player_handlers = {
	CT_Opt_Player_Draw,
	CT_Opt_Player_Key,
	OnShow_SettPlayer,
	CT_Opt_Player_Mouse_Event
};

CTabPage_Handlers_t options_graphics_handlers = {
	CT_Opt_FPS_Draw,
	CT_Opt_FPS_Key,
	OnShow_SettFPS,
	CT_Opt_FPS_Mouse_Event
};

CTabPage_Handlers_t options_hud_handlers = {
	CT_Opt_HUD_Draw,
	CT_Opt_HUD_Key,
	OnShow_SettHUD,
	CT_Opt_HUD_Mouse_Event
};

CTabPage_Handlers_t options_multiview_handlers = {
	CT_Opt_Multiview_Draw,
	CT_Opt_Multiview_Key,
	OnShow_SettMultiview,
	CT_Opt_Multiview_Mouse_Event
};

CTabPage_Handlers_t options_controls_handlers = {
	CT_Opt_Binds_Draw,
	CT_Opt_Binds_Key,
	OnShow_SettBinds,
	CT_Opt_Binds_Mouse_Event
};

CTabPage_Handlers_t options_video_handlers = {
	CT_Opt_Video_Draw,
	CT_Opt_Video_Key,
	OnShow_SettVideo,
	CT_Opt_Video_Mouse_Event
};

CTabPage_Handlers_t options_config_handlers = {
	CT_Opt_Config_Draw,
	CT_Opt_Config_Key,
	OnShow_SettConfig,
	CT_Opt_Config_Mouse_Event
};

void Menu_Options_Key(int key, int unichar) {
    int handled = CTab_Key(&options_tab, key);
	options_unichar = unichar;

	if (!handled && (key == K_ESCAPE || key == K_MOUSE2))
		M_Menu_Main_f();
}

#define OPTPADDING 4

void Menu_Options_Draw(void) {
	int x, y, w, h;

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

    // this will add top, left and bottom padding
    // right padding is not added because it causes annoying scrollbar behaviour
    // when mouse gets off the scrollbar to the right side of it
	w = vid.width - OPTPADDING; // here used to be a limit to 512x... size
	h = vid.height - OPTPADDING*2;
	x = OPTPADDING;
	y = OPTPADDING;

	CTab_Draw(&options_tab, x, y, w, h);
}

qbool Menu_Options_Mouse_Event(const mouse_state_t *ms)
{
	mouse_state_t nms = *ms;

    if (ms->button_up == 2) {
        Menu_Options_Key(K_MOUSE2, 0);
        return true;
    }

	// we are sending relative coordinates
	nms.x -= OPTPADDING;
	nms.y -= OPTPADDING;
	nms.x_old -= OPTPADDING;
	nms.y_old -= OPTPADDING;

	if (nms.x < 0 || nms.y < 0) return false;

	return CTab_Mouse_Event(&options_tab, &nms);
}

void Menu_Options_Init(void) {
	Settings_MainInit();

	Settings_Page_Init(settgeneral, settgeneral_arr);
	Settings_Page_Init(settfps, settfps_arr);
	Settings_Page_Init(settmultiview, settmultiview_arr);
	Settings_Page_Init(setthud, setthud_arr);
	Settings_Page_Init(settplayer, settplayer_arr);
	Settings_Page_Init(settbinds, settbinds_arr);
#ifdef GLQUAKE
	Settings_Page_Init(settvideo, settvideo_arr);
#endif
	Settings_Page_Init(settconfig, settconfig_arr);

	Cvar_Register(&menu_advanced);
#ifdef GLQUAKE
	// this is here just to not get a crash in Cvar_Set
    mvs_selected.freq.name = "menu_tempval_video_freq";
	mvs_previous.freq.name = mvs_selected.freq.name;
    mvs_selected.freq.string = NULL;
    mvs_previous.freq.string = NULL;
    mvs_selected.freq.next = &mvs_selected.freq;
    mvs_previous.freq.next = &mvs_previous.freq;
#endif

	Cvar_SetCurrentGroup(CVAR_GROUP_CONFIG);
	Cvar_Register(&cfg_browser_showsize);
    Cvar_Register(&cfg_browser_showdate);
    Cvar_Register(&cfg_browser_showtime);
    Cvar_Register(&cfg_browser_sortmode);
    Cvar_Register(&cfg_browser_showstatus);
    Cvar_Register(&cfg_browser_stripnames);
    Cvar_Register(&cfg_browser_interline);
	Cvar_Register(&cfg_browser_scrollnames);
	Cvar_Register(&cfg_browser_selectedcolor);
	Cvar_Register(&cfg_browser_democolor);
	Cvar_Register(&cfg_browser_dircolor);
#ifdef WITH_ZIP
	Cvar_Register(&cfg_browser_zipcolor);
#endif
	Cvar_ResetCurrentGroup();


	FL_Init(&configs_filelist,
        &cfg_browser_sortmode,
        &cfg_browser_showsize,
        &cfg_browser_showdate,
        &cfg_browser_showtime,
        &cfg_browser_stripnames,
        &cfg_browser_interline,
        &cfg_browser_showstatus,
		&cfg_browser_scrollnames,
		&cfg_browser_democolor,
		&cfg_browser_selectedcolor,
		&cfg_browser_dircolor,
#ifdef WITH_ZIP
		&cfg_browser_zipcolor,
#endif
		"./ezquake/configs");

	FL_AddFileType(&configs_filelist, 0, ".cfg");
	FL_AddFileType(&configs_filelist, 1, ".txt");

	CEditBox_Init(&filenameeb, 32, 64);

	CTab_Init(&options_tab);
	CTab_AddPage(&options_tab, "main", OPTPG_SETTINGS, &options_main_handlers);
	CTab_AddPage(&options_tab, "player", OPTPG_PLAYER, &options_player_handlers);
	CTab_AddPage(&options_tab, "graphics", OPTPG_FPS, &options_graphics_handlers);
	CTab_AddPage(&options_tab, "hud", OPTPG_HUD, &options_hud_handlers);
	CTab_AddPage(&options_tab, "multiview", OPTPG_MULTIVIEW, &options_multiview_handlers);
	CTab_AddPage(&options_tab, "controls", OPTPG_BINDS, &options_controls_handlers);
	CTab_AddPage(&options_tab, "video", OPTPG_VIDEO, &options_video_handlers);
	CTab_AddPage(&options_tab, "config", OPTPG_CONFIG, &options_config_handlers);
	CTab_SetCurrentId(&options_tab, OPTPG_SETTINGS);
}
