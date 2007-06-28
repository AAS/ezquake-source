/*
Copyright (C) 1996-1997 Id Software, Inc.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  

See the included (GNU.txt) GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

	$Id: cl_demo.c,v 1.72 2007-06-28 18:30:43 qqshka Exp $
*/

#include "quakedef.h"
#include "winquake.h"
#include "movie.h"
#include "menu_demo.h"
#include "qtv.h"
#ifdef GLQUAKE
#include "gl_model.h"
#include "gl_local.h"
#else
#include "r_model.h"
#include "r_local.h"
#endif
#include "teamplay.h"
#include "pmove.h"
#include "fs.h"
#include "utils.h"


float olddemotime, nextdemotime;

double bufferingtime; // if we stream from QTV, this is non zero when we trying fill our buffer

//
// Vars related to QIZMO compressed demos. 
// (Only available in Win32 since we need to use an external app)
//
#ifdef _WIN32
static qbool	qwz_unpacking = false;
static qbool	qwz_playback = false;
static qbool	qwz_packing = false;

static qbool	OnChange_demo_format(cvar_t*, char*);
cvar_t			demo_format = {"demo_format", "qwz", 0, OnChange_demo_format};

static HANDLE hQizmoProcess = NULL;
static char tempqwd_name[256] = {0}; // This file must be deleted after playback is finished.
int CL_Demo_Compress(char*);
#endif

static qbool OnChange_demo_dir(cvar_t *var, char *string);
cvar_t demo_dir = {"demo_dir", "", 0, OnChange_demo_dir};

char Demos_Get_Trackname(void);
int FindBestNick(char *s,int use);

//=============================================================================
//								DEMO WRITING
//=============================================================================

static FILE *recordfile = NULL;		// File used for recording demos.
static float playback_recordtime;	// Time when in demo playback and recording.

#define DEMORECORDTIME	((float) (cls.demoplayback ? playback_recordtime : cls.realtime))
#define DEMOCACHE_MINSIZE	(2 * 1024 * 1024)
#define DEMOCACHE_FLUSHSIZE	(1024 * 1024)

static sizebuf_t democache;
static qbool democache_available = false;	// Has the user opted to use a demo cache?

//
// Opens a demo for writing.
//
static qbool CL_Demo_Open(char *name) 
{
	// Clear the demo cache and open the demo file for writing.
	if (democache_available)
		SZ_Clear(&democache);
	recordfile = fopen (name, "wb");
	return recordfile ? true : false;
}

//
// Closes a demo.
//
static void CL_Demo_Close(void) 
{
	// Flush the demo cache and close the demo file.
	if (democache_available)
		fwrite(democache.data, democache.cursize, 1, recordfile);
	fclose(recordfile);
	recordfile = NULL;
}

//
// Writes a chunk of data to the currently opened demo record file.
//
static void CL_Demo_Write(void *data, int size) 
{
	if (democache_available) 
	{
		//
		// Write to the demo cache.
		//
		if (size > democache.maxsize) 
		{
			// The size of the data to be written is bigger than the demo cache, fatal error.
			Sys_Error("CL_Demo_Write: size > democache.maxsize");
		} 
		else if (size > democache.maxsize - democache.cursize) 
		{
			//
			// Flushes part of the demo cache (enough to fit the new data) if it has been overflowed.
			//
			int overflow_size = size - (democache.maxsize - democache.cursize);

			Com_Printf("Warning: democache overflow...flushing\n");			
			
			if (overflow_size <= DEMOCACHE_FLUSHSIZE)
				overflow_size = min(DEMOCACHE_FLUSHSIZE, democache.cursize);	
			
			// Write as much data as overflowed from the current 
			// contents of the demo cache to the demo file.
			fwrite(democache.data, overflow_size, 1, recordfile);

			// Shift the cache contents (remove what was just written).
			memmove(democache.data, democache.data + overflow_size, democache.cursize - overflow_size);	
			democache.cursize -= overflow_size;

			// Write the new data to the demo cache.
			SZ_Write(&democache, data, size);
		} 
		else 
		{
			// Write to the demo cache.
			SZ_Write(&democache, data, size);
		}
	} 
	else 
	{
		//
		// Write directly to the file.
		//
		fwrite (data, size, 1, recordfile);
	}
}

//
// Flushes any pending write operations to the recording file.
//
static void CL_Demo_Flush(void) 
{
	fflush(recordfile);
}

//
// Writes a user cmd to the open demo file.
//
void CL_WriteDemoCmd (usercmd_t *pcmd) 
{
	int i;
	float fl, t[3];
	byte c;
	usercmd_t cmd;

	// Write the current time.
	fl = LittleFloat(DEMORECORDTIME);
	CL_Demo_Write(&fl, sizeof(fl));

	// Write the message type. A user cmd (movement and such).
	c = dem_cmd;
	CL_Demo_Write(&c, sizeof(c));

	// Correct for byte order, bytes don't matter.
	cmd = *pcmd;

	// Convert the movement angles / vectors to the correct byte order.
	for (i = 0; i < 3; i++)
		cmd.angles[i] = LittleFloat(cmd.angles[i]);
	cmd.forwardmove = LittleShort(cmd.forwardmove);
	cmd.sidemove    = LittleShort(cmd.sidemove);
	cmd.upmove      = LittleShort(cmd.upmove);

	// Write the actual user command to the demo.
	CL_Demo_Write(&cmd, sizeof(cmd));

	// Write the view angles with the correct byte order.
	t[0] = LittleFloat (cl.viewangles[0]);
	t[1] = LittleFloat (cl.viewangles[1]);
	t[2] = LittleFloat (cl.viewangles[2]);
	CL_Demo_Write(t, sizeof(t));

	// Flush the changes to file.
	CL_Demo_Flush();
}

//
// Dumps the current net message, prefixed by the length and view angles
//
void CL_WriteDemoMessage (sizebuf_t *msg) 
{
	int len;
	float fl;
	byte c;

	// Write time of cmd.
	fl = LittleFloat(DEMORECORDTIME);
	CL_Demo_Write(&fl, sizeof(fl));

	// Write the message type. Net message.
	c = dem_read;
	CL_Demo_Write(&c, sizeof(c));

	// Write the length of the data.
	len = LittleLong (msg->cursize);
	CL_Demo_Write(&len, 4);

	// Write the data.
	CL_Demo_Write(msg->data, msg->cursize);

	// Flush the changes to the recording file.
	CL_Demo_Flush();

	// Request pings from the net chan if the user has set it.
    {
        extern void Request_Pings(void);
        extern cvar_t demo_getpings;
        
        if (demo_getpings.value)
            Request_Pings();
    }
}


//
// Writes the entities list to a demo.
//
void CL_WriteDemoEntities (void) 
{
	int ent_index, ent_total;
	entity_state_t *ent;

	// Write the ID byte for a delta entity operation to the demo.
	MSG_WriteByte (&cls.demomessage, svc_packetentities);

	// Get the entities list from the frame.
	ent = cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK].packet_entities.entities;
	ent_total = cl.frames[cls.netchan.incoming_sequence & UPDATE_MASK].packet_entities.num_entities;

	// Write all the entity changes since last packet entity message. 
	for (ent_index = 0; ent_index < ent_total; ent_index++, ent++)
		MSG_WriteDeltaEntity (&cl_entities[ent->number].baseline, ent, &cls.demomessage, true);

	// End of packetentities.
	MSG_WriteShort (&cls.demomessage, 0);    
}

//
// Writes a startup demo message to the demo being recorded.
//
static void CL_WriteStartupDemoMessage (sizebuf_t *msg, int seq) 
{
	int len, i;
	float fl;
	byte c;

	// Don't write, we're not recording.
	if (!cls.demorecording)
		return;

	// Write the time.
	fl = LittleFloat(DEMORECORDTIME);
	CL_Demo_Write(&fl, sizeof(fl));

	// Message type = net message.
	c = dem_read;
	CL_Demo_Write(&c, sizeof(c));

	// Write the length of the message.
	len = LittleLong (msg->cursize + 8);
	CL_Demo_Write(&len, 4);

	// Write the sequence number twice. Why?
	i = LittleLong(seq);
	CL_Demo_Write(&i, 4);
	CL_Demo_Write(&i, 4);

	// Write the actual message data to the demo.
	CL_Demo_Write(msg->data, msg->cursize);

	// Flush the message to file.
	CL_Demo_Flush();
}

//
// Writes a set demo message. The outgoing / incoming sequence at the start of the demo.
// This is only written once at startup.
//
static void CL_WriteSetDemoMessage (void) 
{
	int len;
	float fl;
	byte c;

	// We're not recording.
	if (!cls.demorecording)
		return;

	// Write the demo time.
	fl = LittleFloat(DEMORECORDTIME);
	CL_Demo_Write(&fl, sizeof(fl));

	// Write the message type.
	c = dem_set;
	CL_Demo_Write(&c, sizeof(c));

	// Write the outgoing / incoming sequence.
	len = LittleLong(cls.netchan.outgoing_sequence);
	CL_Demo_Write(&len, 4);
	len = LittleLong(cls.netchan.incoming_sequence);
	CL_Demo_Write(&len, 4);

	// Flush the demo file to disk.
	CL_Demo_Flush();
}

//
// Write startup data to demo (called when demo started and cls.state == ca_active)
//
static void CL_WriteStartupData (void) 
{
	sizebuf_t buf;
	char buf_data[MAX_MSGLEN * 2], *s;
	int n, i, j, seq = 1;
	entity_t *ent;
	entity_state_t *es, blankes;
	player_info_t *player;

	//
	// Serverdata
	// Send the info about the new client to all connected clients.
	//

	// Init a buffer that we write to before writing to the file.
	SZ_Init (&buf, (byte *) buf_data, sizeof(buf_data));

	//
	// Send the serverdata.
	//
	MSG_WriteByte (&buf, svc_serverdata);
#ifdef PROTOCOL_VERSION_FTE
	if (cls.fteprotocolextensions)	//maintain demo compatability
	{
		MSG_WriteLong (&buf, PROTOCOL_VERSION_FTE);
		MSG_WriteLong (&buf, cls.fteprotocolextensions);
	}
#endif
	MSG_WriteLong (&buf, PROTOCOL_VERSION);
	MSG_WriteLong (&buf, cl.servercount);
	MSG_WriteString (&buf, cls.gamedirfile);

	// Write if we're a spectator or not.
	if (cl.spectator)
		MSG_WriteByte (&buf, cl.playernum | 128);
	else
		MSG_WriteByte (&buf, cl.playernum);

	// Send full levelname.
	MSG_WriteString (&buf, cl.levelname);

	// Send the movevars.
	MSG_WriteFloat(&buf, movevars.gravity);
	MSG_WriteFloat(&buf, movevars.stopspeed);
	MSG_WriteFloat(&buf, cl.maxspeed);
	MSG_WriteFloat(&buf, movevars.spectatormaxspeed);
	MSG_WriteFloat(&buf, movevars.accelerate);
	MSG_WriteFloat(&buf, movevars.airaccelerate);
	MSG_WriteFloat(&buf, movevars.wateraccelerate);
	MSG_WriteFloat(&buf, movevars.friction);
	MSG_WriteFloat(&buf, movevars.waterfriction);
	MSG_WriteFloat(&buf, cl.entgravity);

	// Send music.
	MSG_WriteByte (&buf, svc_cdtrack);
	MSG_WriteByte (&buf, 0); // None in demos

	// Send server info string.
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va("fullserverinfo \"%s\"\n", cl.serverinfo));

	// Flush the buffer to the demo file and then clear it.
	CL_WriteStartupDemoMessage (&buf, seq++);
	SZ_Clear (&buf); 

	//
	// Write the soundlist.
	//
	MSG_WriteByte (&buf, svc_soundlist);
	MSG_WriteByte (&buf, 0);

	// Loop through all sounds and write them to the demo.
	n = 0;
	s = cl.sound_name[n + 1];
	while (*s) 
	{
		// Write the sound name to the buffer.
		MSG_WriteString (&buf, s);

		// If the buffer is half full, flush it.
		if (buf.cursize > MAX_MSGLEN / 2) 
		{
			// End of the partial sound list.
			MSG_WriteByte (&buf, 0);

			// Write how many sounds we've listed so far.
			MSG_WriteByte (&buf, n);

			// Flush the buffer to the demo file and clear the buffer.
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 

			// Start on a new sound list and continue writing the
			// remaining sounds.
			MSG_WriteByte (&buf, svc_soundlist);
			MSG_WriteByte (&buf, n + 1);
		}

		n++;
		s = cl.sound_name[n+1];
	}

	// If the buffer isn't empty flush and clear it.
	if (buf.cursize)
	{
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		CL_WriteStartupDemoMessage (&buf, seq++);
		SZ_Clear (&buf); 
	}

#ifdef VWEP_TEST
// vwep modellist
	if ((cl.z_ext & Z_EXT_VWEP) && cl.vw_model_name[0][0]) {
		// send VWep precaches
		// pray we don't overflow
		for (i = 0; i < MAX_VWEP_MODELS; i++) {
			s = cl.vw_model_name[i];
			if (!*s)
				continue;
			MSG_WriteByte (&buf, svc_serverinfo);
			MSG_WriteString (&buf, "#vw");
			MSG_WriteString (&buf, va("%i %s", i, TrimModelName(s)));
		}
		// send end-of-list messsage
		MSG_WriteByte (&buf, svc_serverinfo);
		MSG_WriteString (&buf, "#vw");
		MSG_WriteString (&buf, "");
	}
	// don't bother flushing, the vwep list is not that large (I hope)
#endif

	//
	// Modellist.
	//
	MSG_WriteByte (&buf, svc_modellist);
	MSG_WriteByte (&buf, 0);

	// Loop through the models
	n = 0;
	s = cl.model_name[n + 1];
	while (*s) 
	{
		// Write the model name to the buffer.
		MSG_WriteString (&buf, s);

		// If the buffer is half full, flush it.
		if (buf.cursize	> MAX_MSGLEN / 2) 
		{
			// End of partial model list.
			MSG_WriteByte (&buf, 0);

			// Write how many models we've written so far.
			MSG_WriteByte (&buf, n);

			// Flush the model list to the demo file and clear the buffer.
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 

			// Start on a new partial model list and continue.
			MSG_WriteByte (&buf, svc_modellist);
			MSG_WriteByte (&buf, n + 1);
		}
		n++;
		s = cl.model_name[n + 1];
	}

	// Flush the buffer if it's not empty.
	if (buf.cursize) 
	{
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, 0);
		CL_WriteStartupDemoMessage (&buf, seq++);
		SZ_Clear (&buf); 
	}

	//
	// Write static entities.
	//
	for (i = 0; i < cl.num_statics; i++) 
	{
		// Get the next static entity.
		ent = cl_static_entities + i;

		// Write ID for static entities.
		MSG_WriteByte (&buf, svc_spawnstatic);

		// Find if the model is precached or not.
		for (j = 1; j < MAX_MODELS; j++)
		{
			if (ent->model == cl.model_precache[j])
				break;
		}

		// Write if the model is precached.
		if (j == MAX_MODELS)
			MSG_WriteByte (&buf, 0);
		else
			MSG_WriteByte (&buf, j);

		// Write the entities frame and skin number.
		MSG_WriteByte (&buf, ent->frame);
		MSG_WriteByte (&buf, 0);
		MSG_WriteByte (&buf, ent->skinnum);

		// Write the coordinate and angles.
		for (j = 0; j < 3; j++) 
		{
			MSG_WriteCoord (&buf, ent->origin[j]);
			MSG_WriteAngle (&buf, ent->angles[j]);
		}

		// Flush the buffer if it's half full.
		if (buf.cursize > MAX_MSGLEN / 2) 
		{
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 
		}
	}

	// spawnstaticsound
	for (i = 0; i < cl.num_static_sounds; i++) {
		static_sound_t *ss = &cl.static_sounds[i];
		MSG_WriteByte (&buf, svc_spawnstaticsound);
		for (j = 0; j < 3; j++)
			MSG_WriteCoord (&buf, ss->org[j]);
		MSG_WriteByte (&buf, ss->sound_num);
		MSG_WriteByte (&buf, ss->vol);
		MSG_WriteByte (&buf, ss->atten);

		if (buf.cursize > MAX_MSGLEN/2) {
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 
		}
	}

	//
	// Write entity baselines.
	//
	memset(&blankes, 0, sizeof(blankes));
	for (i = 0; i < CL_MAX_EDICTS; i++) 
	{
		es = &cl_entities[i].baseline;

		//
		// If the entity state isn't blank write it to the buffer.
		//
		if (memcmp(es, &blankes, sizeof(blankes))) 
		{
			// Write ID.
			MSG_WriteByte (&buf, svc_spawnbaseline);		
			MSG_WriteShort (&buf, i);

			// Write model info.
			MSG_WriteByte (&buf, es->modelindex);
			MSG_WriteByte (&buf, es->frame);
			MSG_WriteByte (&buf, es->colormap);
			MSG_WriteByte (&buf, es->skinnum);

			// Write coordinates and angles.
			for (j = 0; j < 3; j++) 
			{
				MSG_WriteCoord(&buf, es->origin[j]);
				MSG_WriteAngle(&buf, es->angles[j]);
			}

			// Flush to demo file if buffer is half full.
			if (buf.cursize > MAX_MSGLEN / 2) 
			{
				CL_WriteStartupDemoMessage (&buf, seq++);
				SZ_Clear (&buf); 
			}
		}
	}

	// Write spawn information into the clients console buffer.
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va("cmd spawn %i 0\n", cl.servercount));

	// Flush buffer to demo file.
	if (buf.cursize) 
	{
		CL_WriteStartupDemoMessage (&buf, seq++);
		SZ_Clear (&buf); 
	}

	//
	// Send current status of all other players.
	//
	for (i = 0; i < MAX_CLIENTS; i++) 
	{
		player = cl.players + i;

		// Frags.
		MSG_WriteByte (&buf, svc_updatefrags);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, player->frags);

		// Ping.
		MSG_WriteByte (&buf, svc_updateping);
		MSG_WriteByte (&buf, i);
		MSG_WriteShort (&buf, player->ping);

		// Packet loss.
		MSG_WriteByte (&buf, svc_updatepl);
		MSG_WriteByte (&buf, i);
		MSG_WriteByte (&buf, player->pl);

		// Entertime.
		MSG_WriteByte (&buf, svc_updateentertime);
		MSG_WriteByte (&buf, i);
		MSG_WriteFloat (&buf, cls.realtime - player->entertime);

		// User ID and user info.
		MSG_WriteByte (&buf, svc_updateuserinfo);
		MSG_WriteByte (&buf, i);
		MSG_WriteLong (&buf, player->userid);
		MSG_WriteString (&buf, player->userinfo);

		// Flush buffer to demo file.
		if (buf.cursize > MAX_MSGLEN / 2) 
		{
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 
		}
	}

	// Send all current light styles.
	for (i = 0; i < MAX_LIGHTSTYLES; i++) 
	{
		// Don't send empty lightstyle strings.
		if (!cl_lightstyle[i].length)
			continue;		

		MSG_WriteByte (&buf, svc_lightstyle);
		MSG_WriteByte (&buf, (char)i);
		MSG_WriteString (&buf, cl_lightstyle[i].map);
	}

	for (i = 0; i < MAX_CL_STATS; i++) 
	{
		// No need to send zero values.
		if (!cl.stats[i])
			continue;		

		// Write the current players user stats.
		if (cl.stats[i] >= 0 && cl.stats[i] <= 255) 
		{
			MSG_WriteByte (&buf, svc_updatestat);
			MSG_WriteByte (&buf, i);
			MSG_WriteByte (&buf, cl.stats[i]);
		} 
		else 
		{
			MSG_WriteByte (&buf, svc_updatestatlong);
			MSG_WriteByte (&buf, i);
			MSG_WriteLong (&buf, cl.stats[i]);
		}

		// Flush buffer to demo file.
		if (buf.cursize > MAX_MSGLEN / 2) 
		{
			CL_WriteStartupDemoMessage (&buf, seq++);
			SZ_Clear (&buf); 
		}
	}

	// Get the client to check and download skins
	// when that is completed, a begin command will be issued.
	MSG_WriteByte (&buf, svc_stufftext);
	MSG_WriteString (&buf, va("skins\n"));

	CL_WriteStartupDemoMessage (&buf, seq++);

	CL_WriteSetDemoMessage();
}

//=============================================================================
//								DEMO READING
//=============================================================================

int CL_Demo_Peek(void *buf, int size);

vfsfile_t *playbackfile = NULL;		// The demo file used for playback.

char pb_buf[1024*10];				// Playback buffer.
char pb_tmp_buf[sizeof(pb_buf)];	// Temp playback buffer used for validating MVD data.
int	pb_s = 0;						// 
int pb_cnt = 0;						// How many bytes we've read from the playback buffer.
qbool pb_eof = false;				// Have we reached the end of the playback buffer?

//
// Inits the demo playback buffer.
//
void CL_Demo_PB_Init(void *buf, int buflen) 
{
	// The length of the buffer is out of bounds.
	if (buflen < 0 || buflen > (int)sizeof(pb_buf))
		Sys_Error("CL_Demo_PB_Init: buflen out of bounds.");

	// Copy the specified init data into the playback buffer.
	memcpy(pb_buf, buf, buflen);

	// Reset any associated playback buffers.
	pb_s = 0;
	pb_cnt = buflen;
	pb_eof = false;
}

//
// Reads a chunk of data from the playback file and returns the number of bytes read.
//
int pb_raw_read(void *buf, int size) 
{
	vfserrno_t err;
	int r = VFS_READ(playbackfile, buf, size, &err);

	// Size > 0 mean detect EOF only if we actually trying read some data.
	if (size > 0 && !r && err == VFSERR_EOF) 
		pb_eof = true;

	return r;
}

//
// Checks if the 
//
qbool pb_ensure(void) 
{
	int need, got = 0;

	// Show how much we've read.
	if (cl_shownet.value == 3)
		Com_Printf(" %d", pb_cnt);

	// Try to decrease the playback buffer.
	if (cls.mvdplayback && pb_cnt > 0 ) 
	{
		// At the same time increase internal TCP buffer by faking a read to it.
		pb_raw_read(NULL, 0); 
		
		// Seems theoretical size of one MVD packet is 1400, so 2000 must be safe to parse something.
		if (pb_cnt > 2000)
			return true;

		// Peek into the playback buffer.
		CL_Demo_Peek(pb_tmp_buf, pb_cnt);

		// Make sure the MVD data is valid.
		if (ConsistantMVDData((unsigned char*)pb_tmp_buf, pb_cnt))
			return true;
	}

	need = sizeof(pb_buf);	// We want to read the entire buffer.
	need -= pb_cnt;			// But only read stuff we haven't read yet.
	need = min(need, (int)sizeof(pb_buf) - pb_s - pb_cnt);
	need = max(0, need);
	
	if (need)
	{
		got = pb_raw_read(pb_buf + pb_s + pb_cnt, need);
		pb_cnt += got;
	}

	if (got == need) 
	{
		int tmp = (pb_s + pb_cnt) % (int)sizeof(pb_buf);
		need = sizeof(pb_buf);
		need -= pb_cnt;
		pb_cnt += pb_raw_read(pb_buf + tmp, need);
	}

	if (pb_cnt == (int)sizeof(pb_buf) || pb_eof)
		return true; // return true if we have full buffer or get EOF

	// Not enough data in buffer, check do we have at least one message in buffer.
	if (cls.mvdplayback && pb_cnt) 
	{ 
		CL_Demo_Peek(pb_tmp_buf, pb_cnt);

		if(ConsistantMVDData((unsigned char*)pb_tmp_buf, pb_cnt))
			return true;
	}

	if (cls.mvdplayback == QTV_PLAYBACK)
	{
		double prebufferseconds = 2; // hard coded pre buffering time is 2 seconds

		bufferingtime = Sys_DoubleTime() + prebufferseconds;

		if (developer.integer == 2)
			Com_Printf("qtv: no enough buffered, buffering for %.1f\n", prebufferseconds); // print some annoying message
	}

	return false;
}

int pb_read(void *buf, int size, qbool peek) 
{
	int need, have = 0;

	// Size can't be negative.
	if (size < 0)
		Sys_Error("pb_read: size < 0");

	need = min(pb_cnt, size);
	need = min(need, (int)sizeof(pb_buf) - pb_s);
	memcpy((byte*)buf + have, (byte*)pb_buf + pb_s, need);
	have += need;

	need = min(pb_cnt, size);
	need -= have;
	need = min(need, pb_s);
	memcpy((byte*)buf + have, pb_buf, need);
	have += need;

	if (!peek) 
	{
		pb_s += have;
		pb_s = pb_s % (int)sizeof(pb_buf);
		pb_cnt -= have;
	}

	return have;
}

int CL_Demo_Peek(void *buf, int size) 
{
	if (pb_read(buf, size, true) != size)
		Host_Error("Unexpected end of demo");
	return 1;
}

int CL_Demo_Read(void *buf, int size) 
{
	if (pb_read(buf, size, false) != size)
		Host_Error("Unexpected end of demo");
	return 1;
}

//
// When a demo is playing back, all NET_SendMessages are skipped, and NET_GetMessages are read from the demo file.
// Whenever cl.time gets past the last received message, another message is read from the demo file.
//
// Summary:
// 
// 1. MVD - Make sure we're not more than 1 second behind the next demo time.
// 2. Get the time of the next demo message by peeking at it.
// 3. Is it time to get the next demo message yet? (Always for timedemo) Return false if not.
// 4. Read the time of the next message from the demo (consume it).
// 5. MVD - Save the current time so we have it for the next frame 
//    (we need it to calculate the demo time. Time in MVDs is saved 
//     as the number of miliseconds since last frame).
// 6. Read the message type from the demo, only the first 3 bits are significant.
//    There are 3 basic message types used by all demos:
//    dem_set = Only appears once at start of demo. Contains sequence numbers for the netchan.
//    dem_cmd = A user movement cmd.
//    dem_Read = An arbitrary network message.
//
//    MVDs also use:
//    dem_multiple, dem_single, dem_stats and dem_all to direct certain messages to
//    specific clients only, and to update stats.
//
// 7. Parse the specific demo messages.
//
qbool CL_GetDemoMessage (void) 
{
	int i, j, tracknum;
	float demotime;
	byte c;
	byte message_type;
	usercmd_t *pcmd;
	static float prevtime = 0;

	// Don't try to play while QWZ is being unpacked.
	#ifdef _WIN32	
	if (qwz_unpacking)
		return false;
	#endif

	// Demo paused, don't read anything.
	if (cl.paused & PAUSED_DEMO)
	{
		pb_ensure(); // perform our operations on socket, in case of QTV
		return false;
	}

	// mean we are buffering for QTV, so not ready parse
	if (bufferingtime && bufferingtime > Sys_DoubleTime())
	{
		extern qbool	host_skipframe;

		pb_ensure(); // perform our operations on socket, in case of QTV
		host_skipframe = true; // this will force not update cls.demotime

		return false;
	}

	bufferingtime = 0;

	//
	// Adjust the time for MVD playback.
	//
	if (cls.mvdplayback) 
	{
		// Reset the previous time.
		if (prevtime < nextdemotime)
			prevtime = nextdemotime;

		// Always be within one second from the next demo time.
		if (cls.demotime + 1.0 < nextdemotime)
			cls.demotime = nextdemotime - 1.0;
	}

	//
	// Check if we need to get more messages for now and if so read it
	// from the demo file and pass it on to the net channel.
	//
	while (true)
	{
		if (!pb_ensure())
			return false;

		//
		// Read the time of the next message in the demo.
		//
		if (cls.mvdplayback) 
		{
			// Number of miliseconds since last frame can be between 0-255.
			byte mvd_time;

			// Peek inside, but don't read.
			// (Since it might not be time to continue reading in the demo
			// we want to be able to check this again later if that's the case).
			CL_Demo_Peek(&mvd_time, sizeof(mvd_time)); 
			
			// Calculate the demo time.
			// (The time in an MVD is saved as a byte with number of miliseconds since the last cmd
			// so we need to multiply it by 0.001 to get it in seconds like normal quake time).
			demotime = prevtime + (mvd_time * 0.001);
			
			if (cls.demotime - nextdemotime > 0.0001 && nextdemotime != demotime) 
			{
				olddemotime = nextdemotime;
				cls.netchan.incoming_sequence++;
				cls.netchan.incoming_acknowledged++;
				cls.netchan.frame_latency = 0;			 
				cls.netchan.last_received = cls.demotime; // Make timeout check happy.
				nextdemotime = demotime;
			}
		} 
		else 
		{
			// Peek inside, but don't read.
			// (Since it might not be time to continue reading in the demo
			// we want to be able to check this again later if that's the case).
			CL_Demo_Peek(&demotime, sizeof(demotime));
			demotime = LittleFloat(demotime);
		}

		playback_recordtime = demotime;

		//
		// Decide if it is time to grab the next message from the demo yet.
		//
		if (cls.timedemo)
		{
			//
			// Timedemo playback, grab the next message as quickly as possible.
			//

			if (cls.td_lastframe < 0) 
			{
				// This is the first frame of the timedemo.
				cls.td_lastframe = demotime;
			} 
			else if (demotime > cls.td_lastframe) 
			{
				// We've already read this frame's message so skip it.
				cls.td_lastframe = demotime;
				return false;
			}

			// Did we just start the time demo?
			if (!cls.td_starttime && cls.state == ca_active) 
			{
				// Save the start time (real world time) and current frame number
				// so that we will know how long it took to go through it all
				// and calculate the framerate when it's done.
				cls.td_starttime = Sys_DoubleTime();
				cls.td_startframe = cls.framecount;
			}

			cls.demotime = demotime; // warp
		} 
		else if (!(cl.paused & PAUSED_SERVER) && cls.state == ca_active) // always grab until fully connected
		{
			//
			// Not paused and active.
			//

			if (cls.mvdplayback) 
			{
				if (nextdemotime < demotime) 
				{
					// Don't need another message yet.
					return false;		
				}
			} 
			else 
			{
				if (cls.demotime < demotime) 
				{
					// Don't need another message yet.

					// Adjust the demotime to match what's read from file.
					if (cls.demotime + 1.0 < demotime)
						cls.demotime = demotime - 1.0;	

					return false;
				}
			}
		} 
		else 
		{
			cls.demotime = demotime; // we're warping
		}

		// Read the time from the packet (we peaked at it earlier),
		// we're ready to get the next message.
		if (cls.mvdplayback) 
		{
			byte dummy_newtime;
			CL_Demo_Read(&dummy_newtime, sizeof(dummy_newtime));
		} 
		else 
		{
			float dummy_demotime;
			CL_Demo_Read(&dummy_demotime, sizeof(dummy_demotime));
		}

		// Save the previous time for MVD playback (for the next message), 
		// it is needed to calculate the demotime since in mvd's the time is
		// saved as the number of miliseconds since last frame message.
		if (cls.mvdplayback)
			prevtime = demotime;

		// Get the msg type.
		CL_Demo_Read(&c, sizeof(c));
		message_type = (c & 7);

		if (message_type == dem_cmd)
		{
			//
			// User cmd read.
			//			

			// Get which frame we should read the cmd into from the demo. 
			i = cls.netchan.outgoing_sequence & UPDATE_MASK;
			
			// Read the user cmd from the demo.
			pcmd = &cl.frames[i].cmd;
			CL_Demo_Read(pcmd, sizeof(*pcmd));

			//
			// Convert the angles/movement vectors into the correct byte order.
			//
			for (j = 0; j < 3; j++)
				pcmd->angles[j] = LittleFloat(pcmd->angles[j]);
			pcmd->forwardmove = LittleShort(pcmd->forwardmove);
			pcmd->sidemove = LittleShort(pcmd->sidemove);
			pcmd->upmove = LittleShort(pcmd->upmove);
			
			// Set the time time this cmd was sent and increase
			// how many net messages have been sent.
			cl.frames[i].senttime = cls.realtime;
			cl.frames[i].receivedtime = -1;		// We haven't gotten a reply yet.
			cls.netchan.outgoing_sequence++;

			// Read the viewangles from the demo and convert them to correct byte order.
			CL_Demo_Read(cl.viewangles, 12);
			for (j = 0; j < 3; j++)
				cl.viewangles[j] = LittleFloat (cl.viewangles[j]);

			// Calculate the player fps based on the cmd.
			CL_CalcPlayerFPS(&cl.players[cl.playernum], pcmd->msec);

			// Try locking on to a player.
			if (cl.spectator)
				Cam_TryLock();

			// Write the demo to the record file if we're recording.
			if (cls.demorecording)
				CL_WriteDemoCmd(pcmd);

			// Get next message.
			continue;			
		}
		
		//
		// MVD Only. These message types tells to which players the message is directed to.
		//
		if (message_type >= dem_multiple && message_type <= dem_all)
		{
			switch (message_type)
			{
				case dem_multiple:
				//
				// This message should be sent to more than one player, get which players.
				//
				{
					// Read the player bit mask from the demo and convert to the correct byte order.
					// Each bit in this number represents a player, 32-bits, 32 players.
					CL_Demo_Read(&i, sizeof(i));
					cls.lastto = LittleLong(i);
					cls.lasttype = dem_multiple;
					break;
				}
				case dem_single:
				//
				// Only a single player should receive this message. Get the player number of that player.
				//
				{
					// The first 3 bits contain the message type (so remove that part), the rest contains
					// a 5-bit number which contains the player number of the affected player.
					cls.lastto = c >> 3;
					cls.lasttype = dem_single;
					break;
				}
				case dem_stats:
				//
				// The stats for a player has changed. Get the player number of that player.
				//
				{
					cls.lastto = c >> 3;
					cls.lasttype = dem_stats;
					break;
				}
				case dem_all:
				//
				// This message is directed to all players.
				//
				{
					cls.lastto = 0;
					cls.lasttype = dem_all;
					break;
				}
				default:
				{
					Host_Error("This can't happen\n");
					return false;
				}
			}

			//
			// Fall through to dem_read after we've gotten the affected players.
			//
			message_type = dem_read;
		}

		//
		// Get the next net message from the demo file.
		//
		if (message_type == dem_read)
		{
			// Read the size of next net message in the demo file
			// and convert it into the correct byte order.
			CL_Demo_Read(&net_message.cursize, 4);
			net_message.cursize = LittleLong (net_message.cursize);

			// The message was too big, stop playback.
			if (net_message.cursize > net_message.maxsize) 
			{
				Com_DPrintf("CL_GetDemoMessage: net_message.cursize > net_message.maxsize");
				Host_EndGame();
				Host_Abort();
			}

			// Read the net message from the demo.
			CL_Demo_Read(net_message.data, net_message.cursize);

			//
			// Check what the last message type was for MVDs.
			//
			if (cls.mvdplayback) 
			{
				switch(cls.lasttype) 
				{
					case dem_multiple:
					{
						// Get the number of the player being tracked.
						tracknum = Cam_TrackNum();

						// If no player is tracked (free flying), or the demo message 
						// didn't contain information regarding the player being tracked
						// we read the next message.
						if (tracknum == -1 || !(cls.lastto & (1 << tracknum)))
						{
							continue;
						}
						break;
					}
					case dem_single:
					{
						// If we're not tracking the player referred to in the demo
						// message it's time to read the next message.
						tracknum = Cam_TrackNum();
						if (tracknum == -1 || cls.lastto != spec_track)
						{
							continue;
						}
						break;
					}
				}
			}

			return true;
		}

		//
		// Gets the sequence numbers for the netchan at the start of the demo.
		//
		if (message_type == dem_set)
		{
			CL_Demo_Read(&i, sizeof(i));
			cls.netchan.outgoing_sequence = LittleLong(i);
			
			CL_Demo_Read(&i, sizeof(i));
			cls.netchan.incoming_sequence = LittleLong(i);
			
			if (cls.mvdplayback)
				cls.netchan.incoming_acknowledged = cls.netchan.incoming_sequence;

			continue;
		}

		// We should never get this far if the demo is ok.
		Host_Error("Corrupted demo");
		return false;
	}
}

//=============================================================================
//								DEMO RECORDING
//=============================================================================

static char demoname[2 * MAX_OSPATH];
static char fulldemoname[MAX_PATH];
static qbool autorecording = false;
static qbool easyrecording = false;

void CL_AutoRecord_StopMatch(void);
void CL_AutoRecord_CancelMatch(void);

static qbool OnChange_demo_dir(cvar_t *var, char *string) 
{
	if (!string[0])
		return false;

	// Replace \ with /.
	Util_Process_FilenameEx(string, cl_mediaroot.integer == 2);

	// Make sure the filename doesn't have any invalid chars in it.
	if (!Util_Is_Valid_FilenameEx(string, cl_mediaroot.integer == 2)) 
	{
		Com_Printf(Util_Invalid_Filename_Msg(var->name));
		return true;
	}

	// Change to the new folder in the demo browser.
	// FIXME: this did't work
	Menu_Demo_NewHome(string);
	return false;
}

#ifdef _WIN32
static qbool OnChange_demo_format(cvar_t *var, char *string) 
{
	char* allowed_formats[5] = { "qwd", "qwz", "mvd", "mvd.gz", "qwd.gz" };
	int i;

	for (i = 0; i < 5; i++)
		if (!strcmp(allowed_formats[i], string))
			return false;

	Com_Printf("Not valid demo format. Allowed values are: ");
	for (i = 0; i < 5; i++) 
	{
		if (i) 
			Com_Printf(", ");
		Com_Printf(allowed_formats[i]);
	}
	Com_Printf(".\n");

	return true;
}
#endif

//
// Writes a "pimp message" for ezQuake at the end of a demo.
//
static void CL_WriteDemoPimpMessage(void) 
{
	int i;
	char pimpmessage[256] = {0}, border[64] = {0};

	if (cls.demoplayback)
		return;

	strcat(border, "\x1d");
	for (i = 0; i < 34; i++)
		strcat(border, "\x1e");
	strcat(border, "\x1f");

	snprintf(pimpmessage, sizeof(pimpmessage), "\n%s\n%s\n%s\n",
		border,
		"\x1d\x1e\x1e\x1e\x1e\x1e\x1e Recorded by ezQuake \x1e\x1e\x1e\x1e\x1e\x1e\x1e\x1f",
		border		
	);

	SZ_Clear (&net_message);
	MSG_WriteLong (&net_message, cls.netchan.incoming_sequence + 1);
	MSG_WriteLong (&net_message, cls.netchan.incoming_acknowledged | (cls.netchan.incoming_reliable_acknowledged << 31));
	MSG_WriteByte (&net_message, svc_print);
	MSG_WriteByte (&net_message, PRINT_HIGH);
	MSG_WriteString (&net_message, pimpmessage);
	CL_WriteDemoMessage (&net_message);
}

//
// Stop recording a demo.
//
static void CL_StopRecording (void) 
{
	// Nothing to stop.
	if (!cls.demorecording)
		return;

	// Write a pimp message to the demo.
	CL_WriteDemoPimpMessage();

	// Write a disconnect message to the demo file.
	SZ_Clear (&net_message);
	MSG_WriteLong (&net_message, -1);	// -1 sequence means out of band
	MSG_WriteByte (&net_message, svc_disconnect);
	MSG_WriteString (&net_message, "EndOfDemo");
	CL_WriteDemoMessage (&net_message);

	// Finish up by closing the demo file.
	CL_Demo_Close();
	cls.demorecording = false;
}

//
// Stop recording a demo.
//
void CL_Stop_f (void) 
{
	if (!cls.demorecording) 
	{
		Com_Printf ("Not recording a demo\n");
		return;
	}
	if (autorecording) 
	{
		CL_AutoRecord_StopMatch();
#ifdef _WIN32
	} 
	else if (easyrecording) 
	{
		CL_StopRecording();
		CL_Demo_Compress(fulldemoname);
		easyrecording = false;
#endif
	} 
	else 
	{
		CL_StopRecording();
		Com_Printf ("Completed demo\n");
	}
}

//
// Returns the Demo directory. If the user hasn't set the demo_dir var, the gamedir is returned.
//
char *CL_DemoDirectory(void) 
{
	static char dir[MAX_PATH];

	strlcpy(dir, COM_LegacyDir(demo_dir.string), sizeof(dir));
	return dir;
}

#ifdef VWEP_TEST
// FIXME: same as in sv_user.c. Move to common.c?
static char *TrimModelName (char *full)
{
	static char shortn[MAX_QPATH];
	int len;

	if (!strncmp(full, "progs/", 6) && !strchr(full + 6, '/'))
		strlcpy (shortn, full + 6, sizeof(shortn));		// strip progs/
	else
		strlcpy (shortn, full, sizeof(shortn));

	len = strlen(shortn);
	if (len > 4 && !strcmp(shortn + len - 4, ".mdl")
		&& strchr(shortn, '.') == shortn + len - 4)
	{	// strip .mdl
		shortn[len - 4] = '\0';
	}

	return shortn;
}
#endif // VWEP_TEST

//
// Start recording a demo.
//
void CL_Record_f (void) 
{
	char nameext[MAX_OSPATH * 2], name[MAX_OSPATH * 2];

	if (cls.state != ca_active) 
	{
		Com_Printf ("You must be connected before using record\n");
		return;
	}

	if (cls.fteprotocolextensions) {
		Com_Printf ("recording with protocol extensions is not yet implemented\n");
		return;
	}

	switch(Cmd_Argc()) 
	{
		case 1:
		//
		// Just show if anything is being recorded.
		//
		{			
			if (autorecording)
				Com_Printf("Auto demo recording is in progress\n");
			else if (cls.demorecording)
				Com_Printf("Recording to %s\n", demoname);
			else
				Com_Printf("Not recording\n");
			break;
		}
		case 2:
		//
		// Start recording to the specified demo name.
		//
		{
			if (cls.mvdplayback) 
			{
				Com_Printf ("Cannot record during mvd playback\n");
				return;
			}

			if (cls.state != ca_active && cls.state != ca_disconnected) 
			{
				Com_Printf ("Cannot record whilst connecting\n");
				return;
			}

			if (autorecording) 
			{
				Com_Printf("Auto demo recording must be stopped first!\n");
				return;
			}

			// Stop any recording in progress.
			if (cls.demorecording)
				CL_Stop_f();

			// Make sure the filename doesn't contain any invalid characters.
			if (!Util_Is_Valid_Filename(Cmd_Argv(1))) 
			{
				Com_Printf(Util_Invalid_Filename_Msg(Cmd_Argv(1)));
				return;
			}

			// Open the demo file for writing.
			strlcpy(nameext, Cmd_Argv(1), sizeof(nameext));
			COM_ForceExtension (nameext, ".qwd");	

			// Get the path for the demo and try opening the file for writing.
			snprintf (name, sizeof(name), "%s/%s", CL_DemoDirectory(), nameext);
			if (!CL_Demo_Open(name)) 
			{
				Com_Printf ("Error: Couldn't record to %s. Make sure path exists.\n", name);
				return;
			}

			// Demo starting has begun.
			cls.demorecording = true;

			// If we're active, write startup data right away.
			if (cls.state == ca_active)
				CL_WriteStartupData();	

			// Save the demoname for later use.
			strlcpy(demoname, nameext, sizeof(demoname));	

			Com_Printf ("Recording to %s\n", nameext);

			break;
		}
		default:
		{
			Com_Printf("Usage: %s [demoname]\n", Cmd_Argv(0));
			break;
		}
	}
}

//
// Starts recording a demo using autorecord or easyrecord.
//
static qbool CL_MatchRecordDemo(char *dir, char *name, qbool autorecord) 
{
	char extendedname[MAX_PATH];
	char strippedname[MAX_PATH];
	char *fullname;
	char *exts[] = {"qwd", "qwz", "mvd", NULL};
	int num;

	if (cls.state != ca_active) 
	{
		Com_Printf ("You must be connected before using easyrecord\n");
		return false;
	}

	if (cls.mvdplayback) 
	{
		Com_Printf ("Cannot record during mvd playback\n");
		return false;
	}

	if (autorecording) 
	{	
		Com_Printf("Auto demo recording must be stopped first!\n");
		return false;
	}

	// Stop any old recordings.
	if (cls.demorecording)
		CL_Stop_f();

	// Make sure we don't have any invalid chars in the demo name.
	if (!Util_Is_Valid_Filename(name)) 
	{
		Com_Printf(Util_Invalid_Filename_Msg(name));
		return false;
	}

	// We always record to qwd. If the user has set some other demo format
	// we convert to that later on.
	COM_ForceExtension(name, ".qwd");

	if (autorecord) 
	{
		// Save the final demo name.
		strlcpy (extendedname, name, sizeof(extendedname));
	} 
	else 
	{
		//
		// Easy recording, file is saved using match_* settings.
		//

		// Get rid of the extension again.
		COM_StripExtension(name, strippedname);
		fullname = va("%s/%s", dir, strippedname);

		// Find a unique filename in the specified dir.
		if ((num = Util_Extend_Filename(fullname, exts)) == -1) 
		{
			Com_Printf("Error: no available filenames\n");
			return false;
		}

		// Save the demo name..
		snprintf (extendedname, sizeof(extendedname), "%s_%03i.qwd", strippedname, num);
	}

	// Get dir + final demo name.
	fullname = va("%s/%s", dir, extendedname);

	// Open the demo file for writing.
	if (!CL_Demo_Open(fullname)) 
	{
		// Failed to open the file, make sure it exists and try again.
		COM_CreatePath(fullname);
		if (!CL_Demo_Open(fullname)) 
		{
			Com_Printf("Error: Couldn't open %s\n", fullname);
			return false;
		}
	}

	// Write the demo startup stuff.
	cls.demorecording = true;
	CL_WriteStartupData ();

	// Echo the name of the demo if we're easy recording
	// and save the demo name for later use.
	if (!autorecord) 
	{
		Com_Printf ("Recording to %s\n", extendedname);
		strlcpy(demoname, extendedname, sizeof(demoname));		// Just demo name.
		strlcpy(fulldemoname, fullname, sizeof(fulldemoname));  // Demo name including path.
	}

	return true;
}

//
// Starts recording a demo and names it according to your match_ settings.
//
void CL_EasyRecord_f (void) 
{
	char *name;

	if (cls.state != ca_active) 
	{
		Com_Printf("You must be connected to easyrecord\n");
		return;
	}

	switch(Cmd_Argc()) 
	{
		case 1:
		{
			// No name specified by the user, get it from match tools instead.
			name = MT_MatchName(); 
			break;
		}
		case 2:
		{
			// User specified a demo name, use it.
			name = Cmd_Argv(1);	
			break;
		}
		default:
		{
			Com_Printf("Usage: %s [demoname]\n", Cmd_Argv(0));
			return;
		}
	}

	easyrecording = CL_MatchRecordDemo(CL_DemoDirectory(), name, false);
}

//=============================================================================
//							DEMO AUTO RECORDING
//=============================================================================

static char	auto_matchname[MAX_PATH];	// Demoname when recording auto match demo.
static qbool temp_demo_ready = false;	// Indicates if the autorecorded match demo is done recording.
static float auto_starttime;

char *MT_TempDirectory(void);

extern cvar_t match_auto_record, match_auto_minlength;

#define TEMP_DEMO_NAME "_!_temp_!_.qwd"

#define DEMO_MATCH_NORECORD		0 // No autorecord.
#define DEMO_MATCH_MANUALSAVE	1 // Demo will be recorded but requires manuall saving.
#define DEMO_MATCH_AUTOSAVE		2 // Automatically saves the demo after the match is completed.

//
// Stops auto recording of a match.
//
void CL_AutoRecord_StopMatch(void) 
{
	// Not doing anything.
	if (!autorecording)
		return;

	// Stop the recording and write end of demo stuff.
	autorecording = false;
	CL_StopRecording();
	temp_demo_ready = true;

	// Automatically save the demo after the match is completed.
	if (match_auto_record.value == DEMO_MATCH_AUTOSAVE)
	{
		CL_AutoRecord_SaveMatch();
		Com_Printf ("Auto record ok\n");
	}
	else
	{
		Com_Printf ("Auto demo recording completed\n");
	}
}

//
// Cancels the match.
//
void CL_AutoRecord_CancelMatch(void) 
{
	// Not recording.
	if (!autorecording)
		return;

	// Stop the recording and write end of demo stuff.
	autorecording = false;
	CL_StopRecording();
	temp_demo_ready = true;

	if (match_auto_record.value == DEMO_MATCH_AUTOSAVE) 
	{
		// Only save the demo if it's longer than the specified minimum length
		if (cls.realtime - auto_starttime > match_auto_minlength.value)
			CL_AutoRecord_SaveMatch();
		else
			Com_Printf("Auto demo recording cancelled\n");
	} 
	else 
	{
		Com_Printf ("Auto demo recording completed\n");
	}
}

//
// Starts autorecording a match.
//
void CL_AutoRecord_StartMatch(char *demoname) 
{
	temp_demo_ready = false;

	// No autorecording is set.
	if (!match_auto_record.value)
		return;

	// Don't start autorecording if the 
	// user already is recording a demo.
	if (cls.demorecording) 
	{
		// We're autorecording since before, it's
		// ok to restart the recording then.
		if (autorecording)
		{
			autorecording = false;
			CL_StopRecording();
		} 
		else 
		{
			Com_Printf("Auto demo recording skipped (already recording)\n");
			return;
		}
	}

	// Save the name of the auto recorded demo for later.
	strlcpy(auto_matchname, demoname, sizeof(auto_matchname));
	
	// Try starting to record the demo.
	if (!CL_MatchRecordDemo(MT_TempDirectory(), TEMP_DEMO_NAME, true)) 
	{
		Com_Printf ("Auto demo recording failed to start!\n");
		return;
	}

	// We're now in business.
	autorecording = true;
	auto_starttime = cls.realtime;
	Com_Printf ("Auto demo recording commenced\n");
}

//
//
//
qbool CL_AutoRecord_Status(void) 
{
	return temp_demo_ready ? 2 : autorecording ? 1 : 0;
}

//
// Saves an autorecorded demo.
//
void CL_AutoRecord_SaveMatch(void) 
{
	//
	// All demos are first recorded in .qwd, and will then be converted to .mvd/.qwz afterwards
	// if those formats are chosen.
	//
	int error, num;
	FILE *f;
	char *dir, *tempname, savedname[MAX_PATH], *fullsavedname, *exts[] = {"qwd", "qwz", "mvd", NULL};
	
	// The auto recorded demo hasn't finished recording, can't do this yet.
	if (!temp_demo_ready)
		return;

	// Don't try to save it again.
	temp_demo_ready = false;

	// Get the demo dir.
	dir = CL_DemoDirectory();

	// Get the temp name of the file we've recorded.
	tempname = va("%s/%s", MT_TempDirectory(), TEMP_DEMO_NAME);

	// Get the final name where we'll save the final product.
	fullsavedname = va("%s/%s", dir, auto_matchname);
	
	// Find a unique filename in the final location.
	if ((num = Util_Extend_Filename(fullsavedname, exts)) == -1) 
	{
		Com_Printf("Error: no available filenames\n");
		return;
	}

	// Get the final full path where we'll save the demo. (This is the final name for real now)
	snprintf (savedname, sizeof(savedname), "%s_%03i.qwd", auto_matchname, num);
	fullsavedname = va("%s/%s", dir, savedname);

	// Try opening the temp file to make sure we can read it.
	if (!(f = fopen(tempname, "rb")))
		return;
	fclose(f);

	// Move the temp file to the final location.
	if ((error = rename(tempname, fullsavedname))) 
	{
		// Failed to move, make sure the path exists and try again.
		COM_CreatePath(fullsavedname);
		error = rename(tempname, fullsavedname);
	}

#ifdef _WIN32

	// If the file type is not QWD we need to conver it using external apps.
	if (!strcmp(demo_format.string, "qwz") || !strcmp(demo_format.string, "mvd")) 
	{
		Com_Printf("Converting QWD to %s format.\n", demo_format.string);
		
		// Convert the file to either MVD or QWZ.
		if (CL_Demo_Compress(fullsavedname)) 
		{
			return;
		}
		else
		{
			qwz_packing = false;
		}
	}

#endif

	if (!error)
		Com_Printf("Match demo saved to %s\n", savedname);
}

//=============================================================================
//							QIZMO COMPRESSION
//=============================================================================

#ifdef _WIN32

//
//
//
void CL_Demo_RemoveQWD(void)
{
	unlink(tempqwd_name);
}

//
//
//
void CL_Demo_GetCompressedName(char* cdemo_name)
{
	int namelen;
	
	namelen = strlen(tempqwd_name);
	if (strlen(demo_format.string) && strlen(tempqwd_name)) 
	{
		strncpy(cdemo_name, tempqwd_name, namelen - 3);
		strlcpy(cdemo_name + namelen - 3, demo_format.string, 255 - (namelen - 3) - strlen(demo_format.string));
	}
}

//
//
//
void CL_Demo_RemoveCompressed(void)
{
	char cdemo_name[255];
	CL_Demo_GetCompressedName(cdemo_name);
	unlink(cdemo_name);
}

//
//
//
static void StopQWZPlayback (void) 
{
	if (!hQizmoProcess && tempqwd_name[0]) 
	{
		if (remove (tempqwd_name) != 0)
			Com_Printf ("Error: Couldn't delete %s\n", tempqwd_name);
		tempqwd_name[0] = 0;
	}
	qwz_playback = false;
	qwz_unpacking = false;	
}

//
//
//
void CL_CheckQizmoCompletion (void) 
{
	DWORD ExitCode;

	if (!hQizmoProcess)
		return;

	if (!GetExitCodeProcess (hQizmoProcess, &ExitCode)) 
	{
		Com_Printf ("WARINING: CL_CheckQizmoCompletion: GetExitCodeProcess failed\n");
		hQizmoProcess = NULL;
		if (qwz_unpacking) 
		{
			qwz_unpacking = false;
			qwz_playback = false;
			cls.demoplayback = cls.timedemo = false;
			StopQWZPlayback();
		}
		else if (qwz_packing) 
		{
			qwz_packing = false;
			CL_Demo_RemoveCompressed();
		}
		return;
	}

	if (ExitCode == STILL_ACTIVE)
		return;

	hQizmoProcess = NULL;

	if (!qwz_packing && (!qwz_unpacking || !cls.demoplayback)) 
	{
		StopQWZPlayback ();
		return;
	}

	if (qwz_unpacking) 
	{
		qwz_unpacking = false;

		if (!(playbackfile = FS_OpenVFS (tempqwd_name, "rb", FS_NONE_OS))) 
		{
			Com_Printf ("Error: Couldn't open %s\n", tempqwd_name);
			qwz_playback = false;
			cls.demoplayback = cls.timedemo = false;
			tempqwd_name[0] = 0;
			return;
		}

		Com_Printf("Decompression completed...playback starting\n");
	} 
	else if (qwz_packing) 
	{
		FILE* tempfile;
		char newname[255];
		CL_Demo_GetCompressedName(newname);
		qwz_packing = false;

		if ((tempfile = fopen(newname, "rb")) && (COM_FileLength(tempfile) > 0) && fclose(tempfile) != EOF)
		{
			Com_Printf("Demo saved to %s\n", newname + strlen(com_basedir));
			CL_Demo_RemoveQWD();
		} 
		else 
		{
			Com_Printf("Compression failed, demo saved as QWD.\n");
		}
	}
}

//
//
//
static void PlayQWZDemo (void) 
{
	extern cvar_t qizmo_dir;
	STARTUPINFO si;
	PROCESS_INFORMATION	pi;
	char *name, qwz_name[MAX_PATH], cmdline[512], *p;

	if (hQizmoProcess) 
	{
		Com_Printf ("Cannot unpack -- Qizmo still running!\n");
		return;
	}

	name = Cmd_Argv(1);

	if (!strncmp(name, "../", 3) || !strncmp(name, "..\\", 3)) 
	{
		strlcpy (qwz_name, va("%s/%s", com_basedir, name + 3), sizeof(qwz_name));
	} 
	else 
	{
		if (name[0] == '/' || name[0] == '\\')
			strlcpy (qwz_name, va("%s/%s", cls.gamedir, name + 1), sizeof(qwz_name));
		else
			strlcpy (qwz_name, va("%s/%s", cls.gamedir, name), sizeof(qwz_name));
	}

	if (playbackfile = FS_OpenVFS(name, "rb", FS_NONE_OS)) 
	{
		// either we got full system path
		strlcpy(qwz_name, name, sizeof(qwz_name));
	} 
	else 
	{
		// or we have to build it because Qizmo needs an absolute file name
		_fullpath (qwz_name, qwz_name, sizeof(qwz_name) - 1);
		qwz_name[sizeof(qwz_name) - 1] = 0;

		// check if the file exists
		if (!(playbackfile = FS_OpenVFS (qwz_name, "rb", FS_NONE_OS))) 
		{
			Com_Printf ("Error: Couldn't open %s\n", name);
			return;
		}
	}

	VFS_CLOSE (playbackfile);
	playbackfile = NULL;

	strlcpy (tempqwd_name, qwz_name, sizeof(tempqwd_name) - 4);

	// the way Qizmo does it, sigh
	if (!(p = strstr (tempqwd_name, ".qwz")))
		p = strstr (tempqwd_name, ".QWZ");
	if (!p)
		p = tempqwd_name + strlen(tempqwd_name);
	strcpy (p, ".qwd");

	// If .qwd already exists, just play it.
	if ((playbackfile = FS_OpenVFS(tempqwd_name, "rb", FS_NONE_OS)))	
		return;

	Com_Printf ("Unpacking %s...\n", COM_SkipPath(name));

	// Start Qizmo to unpack the demo.
	memset (&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.wShowWindow = SW_SHOWMINNOACTIVE;
	si.dwFlags = STARTF_USESHOWWINDOW;

	strlcpy (cmdline, va("%s/%s/qizmo.exe -q -u -3 -D \"%s\"", com_basedir, qizmo_dir.string, qwz_name), sizeof(cmdline));

	if (!CreateProcess (NULL, cmdline, NULL, NULL,
		FALSE, GetPriorityClass(GetCurrentProcess()),
		NULL, va("%s/%s", com_basedir, qizmo_dir.string), &si, &pi))
	{
		Com_Printf ("Couldn't execute %s/%s/qizmo.exe\n", com_basedir, qizmo_dir.string);
		return;
	}

	hQizmoProcess = pi.hProcess;
	qwz_unpacking = true;
	qwz_playback = true;
}

//
//
//
int CL_Demo_Compress(char* qwdname)
{
	extern cvar_t qizmo_dir;
	extern cvar_t qwdtools_dir;
	STARTUPINFO si;
	PROCESS_INFORMATION	pi;
	char cmdline[1024];
	char *path, outputpath[255];
	char *appname;
	char *parameters;

	if (hQizmoProcess) 
	{
		Com_Printf ("Cannot compress -- Qizmo still running!\n");
		return 0;
	}

	memset (&si, 0, sizeof(si));
	si.cb = sizeof(si);
	si.wShowWindow = SW_SHOWMINNOACTIVE;
	si.dwFlags = STARTF_USESHOWWINDOW;

	if (!strcmp(demo_format.string, "qwz")) 
	{
		appname = "qizmo.exe";
		parameters = "-q -C";
		path = qizmo_dir.string;
		outputpath[0] = 0;
	} 
	else if (!strcmp(demo_format.string, "mvd")) 
	{
		appname = "qwdtools.exe";
		parameters = "-c -o * -od";
		path = qwdtools_dir.string;
		strlcpy(outputpath, qwdname, COM_SkipPath(qwdname) - qwdname);
	} 
	else 
	{
		Com_Printf("%s demo format not yet supported.\n", demo_format.string);
		return 0;
	}

	strlcpy (cmdline, va("\"%s/%s/%s\" %s \"%s\" \"%s\"", com_basedir, path, appname, parameters, outputpath, qwdname), sizeof(cmdline));
	Com_DPrintf("Executing ---\n%s\n---\n", cmdline);

	if (!CreateProcess (NULL, cmdline, NULL, NULL,
		FALSE, GetPriorityClass(GetCurrentProcess()),
		NULL, va("%s/%s", com_basedir, path), &si, &pi))
	{
		Com_Printf ("Couldn't execute %s/%s/%s\n", com_basedir, path, appname);
		return 0;
	}

	strlcpy(tempqwd_name, qwdname, sizeof(tempqwd_name));

	hQizmoProcess = pi.hProcess;
	qwz_packing = true;
	return 1;
}

#endif

//=============================================================================
//							DEMO PLAYBACK
//=============================================================================

double		demostarttime;

#ifdef WITH_ZIP
//
// [IN]		play_path = The compressed demo file that needs to be extracted to play it.
// [OUT]	unpacked_path = The path to the decompressed file.
//
static int CL_GetUnpackedDemoPath (char *play_path, char *unpacked_path, int unpacked_path_size)
{
	//
	// Check if the demo is in a zip file and if so, try to extract it before playing.
	//
	int retval = 0;
	char archive_path[MAX_PATH];
	char inzip_path[MAX_PATH];

	//
	// Is it a GZip file?
	//
	if (!strcmp (COM_FileExtension (play_path), "gz"))
	{
		int i = 0;
		int ext_len = 0;
		char ext[5];

		//
		// Find the extension with .gz removed.
		//
		{
			for (i = strlen(play_path) - 4; i > 0; i--)
			{
				if (play_path[i] == '.' || ext_len >= 4)
				{
					break;
				}

				ext_len++;
			}

			if (ext_len <= 4)
			{
				strlcpy (ext, play_path + strlen(play_path) - 4 - ext_len, sizeof(ext));
			}
			else
			{
				// Default to MVD.
				strlcpy (ext, ".mvd", sizeof(ext));
			}
		}

		// Unpack the file.
		if (!COM_GZipUnpackToTemp (play_path, unpacked_path, unpacked_path_size, ext))
		{
			return 0;
		}

		return 1;
	}
	
	//
	// Check if the path is in the format "c:\quake\bla\demo.zip\some_demo.mvd" and split it up.
	//
	if (COM_ZipBreakupArchivePath ("zip", play_path, archive_path, MAX_PATH, inzip_path, MAX_PATH) < 0)
	{
		return retval;
	}

	//
	// Extract the ZIP file.
	//
	{
		char temp_path[MAX_PATH];

		// Open the zip file.
		unzFile zip_file = COM_ZipUnpackOpenFile (archive_path);
		
		// Try extracting the zip file.
		if(COM_ZipUnpackOneFileToTemp (zip_file, inzip_path, false, false, NULL, temp_path, MAX_PATH) != UNZ_OK)
		{
			Com_Printf ("Failed to unpack the demo file \"%s\" to the temp path \"%s\"\n", inzip_path, temp_path);
			unpacked_path[0] = 0;
		}
		else
		{
			// Successfully unpacked the demo.
			retval = 1;

			// Copy the path of the unpacked file to the return string.
			strlcpy (unpacked_path, temp_path, unpacked_path_size);
		}

		// Close the zip file.
		COM_ZipUnpackCloseFile (zip_file);
	}

	return retval;
}
#endif // WITH_ZIP

//
// Stops demo playback.
//
void CL_StopPlayback (void) 
{
	extern int demo_playlist_started;
	extern int mvd_demo_track_run;
	
	// Nothing to stop.
	if (!cls.demoplayback)
		return;

	// Capturing to avi/images, stop that.
	if (Movie_IsCapturing())	
		Movie_Stop();

	// Close the playback file.
	if (playbackfile)
		VFS_CLOSE (playbackfile);

	// Reset demo playback vars.
	playbackfile = NULL;
	cls.mvdplayback = cls.demoplayback = cls.nqdemoplayback = false;
	cl.paused &= ~PAUSED_DEMO;

	// Stop Qizmo demo playback.
	#ifdef WIN32
	if (qwz_playback)
		StopQWZPlayback ();
	#endif

	//
	// Stop timedemo and show the result.
	//
	if (cls.timedemo) 
	{	
		int frames;
		float time;

		cls.timedemo = false;

		//
		// Calculate the time it took to render the frames.
		//
		frames = cls.framecount - cls.td_startframe - 1;
		time = Sys_DoubleTime() - cls.td_starttime;
		if (time <= 0)
			time = 1;
		Com_Printf ("%i frames %5.1f seconds %5.1f fps\n", frames, time, frames / time);
	}

	// Go to the next demo in the demo playlist.
	if (demo_playlist_started)
	{
		CL_Demo_Playlist_f();
		mvd_demo_track_run = 0;
	}

	TP_ExecTrigger("f_demoend");
}

//
// Starts playback of a demo.
//
void CL_Play_f (void) 
{
	#ifdef WITH_ZIP
	char unpacked_path[MAX_OSPATH];
	#endif // WITH_ZIP
	
	int i;
	char *real_name;
	char name[2 * MAX_OSPATH], **s;
	static char *ext[] = {".qwd", ".mvd", ".dem", NULL};

	// Show usage.
	if (Cmd_Argc() != 2) 
	{
		Com_Printf ("Usage: %s <demoname>\n", Cmd_Argv(0));
		return;
	}

	// Save the name the user specified.
	real_name = Cmd_Argv(1);

	// Disconnect any current game.
	Host_EndGame();

	TP_ExecTrigger("f_demostart");

	#ifdef WITH_ZIP
	//
	// Unpack the demo if it's zipped or gzipped. And get the path to the unpacked demo file.
	//
	if (CL_GetUnpackedDemoPath (Cmd_Argv(1), unpacked_path, MAX_OSPATH))
	{
		real_name = unpacked_path;
	}
	#endif // WITH_ZIP

	#ifdef WIN32
	//
	// Decompress QWZ demos to QWD before playing it (using an external app).
	//
	strlcpy (name, real_name, sizeof(name) - 4);

	if (strlen(name) > 4 && !strcasecmp(COM_FileExtension(name), "qwz")) 
	{
		PlayQWZDemo();

		// We failed to extract the QWZ demo.
		if (!playbackfile && !qwz_playback)
		{
			return;	
		}
	} 
	else 
	#endif // WIN32
	{
		//
		// Find the demo path, trying different extensions if needed.
		//
		for (s = ext; *s && !playbackfile; s++) 
		{
			// Strip the extension from the specified filename and append
			// the one we're currently checking for.
			COM_StripExtension (real_name, name);
			strlcpy (name, va("%s%s", name, *s), sizeof(name));

			// Look for the file in the above directory if it has ../ prepended to the filename.
			if (!strncmp(name, "../", 3) || !strncmp(name, "..\\", 3))
			{
				playbackfile = FS_OpenVFS (va("%s/%s", com_basedir, name + 3), "rb", FS_NONE_OS);
			}
			else
			{
				// Search demo on quake file system, even in paks.
				playbackfile = FS_OpenVFS (name, "rb", FS_ANY); 
			}

			// Look in the demo dir (user specified).
			if (!playbackfile)
			{
				playbackfile = FS_OpenVFS (va("%s/%s", CL_DemoDirectory(), name), "rb", FS_NONE_OS);
			}

			// Check the full system path (Run a demo anywhere on the file system).
			if (!playbackfile)
			{
				playbackfile = FS_OpenVFS (name, "rb", FS_NONE_OS);
			}
		}

		// Failed to open the demo from any path :(
		if (!playbackfile) 
		{
			Com_Printf ("Error: Couldn't open %s\n", Cmd_Argv(1));
			return;
		}

		// Reset multiview track slots.
		for(i = 0; i < 4; i++)
		{
			mv_trackslots[i] = -1;
		}
		nTrack1duel = nTrack2duel = 0;
		mv_skinsforced = false;

		Com_Printf ("Playing demo from %s\n", COM_SkipPath(name));
	}

	// Set demoplayback vars depending on the demo type.
	cls.demoplayback	= true;
	cls.mvdplayback		= !strcasecmp(COM_FileExtension(name), "mvd");	
	cls.nqdemoplayback	= !strcasecmp(COM_FileExtension(name), "dem");

	 // Init some buffers for reading.
	CL_Demo_PB_Init(NULL, 0);

	// NetQuake demo support.
	if (cls.nqdemoplayback)
	{
		NQD_StartPlayback ();
	}

	// Setup the netchan and state.
	cls.state = ca_demostart;
	Netchan_Setup (NS_CLIENT, &cls.netchan, net_from, 0);
	cls.demotime = 0;
	demostarttime = -1.0;
	olddemotime = nextdemotime = 0;	
	cls.findtrack = true;

	bufferingtime = 0;

	// Used for knowing who messages is directed to in MVD's.
	cls.lastto = cls.lasttype = 0;

	CL_ClearPredict();
	
	// Recording not allowed during mvdplayback.
	if (cls.mvdplayback && cls.demorecording)
	{
		CL_Stop_f();
	}
}

//
// Renders a demo as quickly as possible.
//
void CL_TimeDemo_f (void) 
{
	if (Cmd_Argc() != 2) 
	{
		Com_Printf ("timedemo <demoname> : gets demo speeds\n");
		return;
	}

	CL_Play_f ();

	// We failed to start demoplayback.
	if (cls.state != ca_demostart)
		return;

	// cls.td_starttime will be grabbed at the second frame of the demo, 
	// so all the loading time doesn't get counted.

	cls.timedemo = true;
	cls.td_starttime = 0;
	cls.td_startframe = cls.framecount;
	cls.td_lastframe = -1;		// Get a new message this frame.
}

void CL_QTVPlay (vfsfile_t *newf, void *buf, int buflen);

char qtvrequestbuffer[4096] = {0};
int qtvrequestsize = 0;
vfsfile_t *qtvrequest = NULL;

//
// Closes a QTV request.
//
void QTV_CloseRequest(qbool warn) 
{
	if (qtvrequest) 
	{
		if (warn)
			Com_Printf("Closing qtv request file\n");

		VFS_CLOSE(qtvrequest);
		qtvrequest = NULL;
	}

	qtvrequestsize = 0;
}

//
// Polls a QTV proxy. (Called on each frame to see if we have a new QTV request available)
//
void CL_QTVPoll (void)
{
	vfserrno_t err;
	char *start, *end, *colon;
	int len, need;
	qbool streamavailable = false;
	qbool saidheader = false;

	// We're not playing any QTV stream.
	if (!qtvrequest)
		return;

	//
	// Calculate how much we need to read from the buffer.
	//
	need = sizeof(qtvrequestbuffer); // Read the entire buffer
	need -= 1;						// For null terminator.
	need -= qtvrequestsize;			// We probably already have something, so don't re-read something we already read.
	need = max(0, need);			// Don't cause a crash by trying to read a negative value.

	len = VFS_READ(qtvrequest, qtvrequestbuffer + qtvrequestsize, need, &err);

	// EOF, end of polling.
	if (!len && err == VFSERR_EOF) 
	{
		QTV_CloseRequest(true); 
		return;
	}

	// Increase how much we've read.
	qtvrequestsize += len;
	qtvrequestbuffer[qtvrequestsize] = '\0';

	// We didn't read anything, abort.
	if (!qtvrequestsize)
		return;

	// Make sure it's a complete chunk. "\n\n" specifies the end of a message.
	for (start = qtvrequestbuffer; *start; start++)
	{
		if (start[0] == '\n' && start[1] == '\n')
			break;
	}

	// We've reached the end and didn't find "\n\n" so the chunk is incomplete.
	if (!*start)
		return;

	start = qtvrequestbuffer;

	//
	// Loop through the request buffer line by line.
	//
	for (end = start; *end; )
	{
		// Found a new line.
		if (*end == '\n')
		{
			// Don't parse it again.
			*end = '\0';

			// Find the first colon in the string.
			colon = strchr(start, ':');

			// We found a request.
			if (colon)
			{
				// Remove the colon.
				*colon++ = '\0';

				//
				// Check the request type. Might be an error.
				//
				if (!strcmp(start, "PERROR"))
				{
					Com_Printf("QTV Error:\n%s\n", colon);
				}
				else if (!strcmp(start, "PRINT"))
				{
					Com_Printf("QTV:\n%s\n", colon);
				}
				else if (!strcmp(start, "TERROR"))
				{
					Com_Printf("QTV Error:\n%s\n", colon);
				}
				else if (!strcmp(start, "ADEMO"))
				{
					Com_Printf("Demo%s is available\n", colon);
				}
				else if (!strcmp(start, "ASOURCE"))
				{
					//
					// Print sources.
					//

					if (!saidheader)
					{
						saidheader = true;
						Com_Printf("Available Sources:\n");
					}

					Com_Printf("%s\n", colon);
				}
				else if (!strcmp(start, "BEGIN"))
				{
					streamavailable = true;
				}
			}
			else
			{
				// What follows this is a stream.
				if (!strcmp(start, "BEGIN"))
				{
					streamavailable = true;
				}
			}

			// From start to end, we have a line.
			start = end + 1;
		}

		end++;
	}

	// We found a stream.
	if (streamavailable)
	{
		// Get the size of the stream chunk.
		int chunk_size = qtvrequestsize - (end - qtvrequestbuffer);

		// Start playing the QTV stream.
		if (chunk_size >= 0) 
		{
			CL_QTVPlay(qtvrequest, end, chunk_size);
			qtvrequest = NULL;
			return;
		}

		Com_Printf("Error while parsing qtv request\n");
	}

	QTV_CloseRequest(true);
}

//
// Returns a list of available stream sources from a QTV proxy.
//
void CL_QTVList_f (void)
{
	char *connrequest;
	vfsfile_t *newf;

	// Not enough arguments, show usage.
	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: qtvlist hostname[:port]\n");
		return;
	}

	// Open the TCP connection to the QTV proxy.
	if (!(newf = FS_OpenTCP(Cmd_Argv(1))))
	{
		Com_Printf("Couldn't connect to proxy\n");
		return;
	}

	// Send the version of QTV the client supports.
	connrequest =	"QTV\n"
					"VERSION: 1\n";
	VFS_WRITE(newf, connrequest, strlen(connrequest));

	// Get a source list from the server.
	connrequest =	"SOURCELIST\n";
	VFS_WRITE(newf, connrequest, strlen(connrequest));

	// "\n\n" will end the session.
	connrequest =	"\n";
	VFS_WRITE(newf, connrequest, strlen(connrequest));

	// Close any old request that might still be open.
	QTV_CloseRequest(true);

	// Set the current connection to be the QTVRequest to be used later.
	qtvrequest = newf;
}

//
// Plays a QTV stream.
//
void CL_QTVPlay (vfsfile_t *newf, void *buf, int buflen)
{
	int i;
	double prebufferseconds = 2; // hard coded pre buffering time is 2 seconds

	// End any current game.
	Host_EndGame();

	// Close the old playback file just in case, and
	// open the "network file" for QTV playback.
	if (playbackfile)
		VFS_CLOSE(playbackfile);
	playbackfile = newf;

	// Reset multiview track slots.
	for(i = 0; i < 4; i++)
	{
		mv_trackslots[i] = -1;
	}
	nTrack1duel = nTrack2duel = 0;
	mv_skinsforced = false;

	// We're now playing a demo.
	cls.demoplayback	= true;
	cls.mvdplayback		= QTV_PLAYBACK;	
	cls.nqdemoplayback	= false;

	// Init playback buffers.
	CL_Demo_PB_Init(buf, buflen); 

	// NetQuake demo support.
	if (cls.nqdemoplayback)
	{
		NQD_StartPlayback (); // Maybe some day QTV will stream NQ demos too...
	}

	// Setup demo playback for the netchan.
	cls.state = ca_demostart;
	Netchan_Setup (NS_CLIENT, &cls.netchan, net_from, 0);
	cls.demotime = 0;
	demostarttime = -1.0;		
	olddemotime = nextdemotime = 0;	
	cls.findtrack = true;

	bufferingtime = Sys_DoubleTime() + prebufferseconds;

	// Used for knowing who messages is directed to in MVD's.
	cls.lastto = cls.lasttype = 0;

	CL_ClearPredict();
	
	// Recording not allowed during mvdplayback.
	if (cls.mvdplayback && cls.demorecording)
	{
		CL_Stop_f();
	}

	TP_ExecTrigger ("f_demostart");

	Com_Printf("Attempting to stream QTV data, buffer is %.1fs\n", prebufferseconds);
}

//
// Start playback of a QTV stream.
//
void CL_QTVPlay_f (void)
{
	char *connrequest;
	vfsfile_t *newf;
	char stream_host[1024] = {0}, *stream, *host;

	// Show usage.
	if (Cmd_Argc() < 2)
	{
		Com_Printf("Usage: qtvplay [stream@]hostname[:port] [password]\n");
		return;
	}

	// The stream address.
	connrequest = Cmd_Argv(1);

	//
	// If a "#" is at the beginning of the given address it refers to a .qtv file.
	//
	if (*connrequest == '#')
	{
		#define QTV_FILE_STREAM		1
		#define QTV_FILE_JOIN		2
		#define QTV_FILE_OBSERVE	3
		int match = 0;

		char buffer[1024];
		char *s;
		FILE *f;

		// Try to open the .qtv file.
		f = fopen(connrequest + 1, "rt");
		if (!f) 
		{
			Com_Printf("qtvplay: can't open file %s\n", connrequest + 1);
			return;
		}

		//
		// Read the entire .qtv file and execute the approriate commands
		// Stream, join or observe.
		//
		while (!feof(f))
		{
			fgets(buffer, sizeof(buffer) - 1, f);

			if (!strncmp(buffer, "Stream=", 7) || !strncmp(buffer, "Stream:", 7))
			{
				match = QTV_FILE_STREAM;
			}

			if (!strncmp(buffer, "Join=", 5) || !strncmp(buffer, "Join:", 5))
			{
				match = QTV_FILE_JOIN;
			}

			if (!strncmp(buffer, "Observe=", 8) || !strncmp(buffer, "Observe:", 8))
			{
				match = QTV_FILE_OBSERVE;
			}

			// We found a match in the .qtv file.
			if (match)
			{
				// Strip new line chars.
				for (s = buffer + strlen(buffer)-1; s >= buffer; s--)
				{
					if (*s == '\r' || *s == '\n')
					{
						*s = 0;
					}
					else
					{
						break;
					}
				}

				// Skip the title part.
				s = buffer + 8;

				// Remove any leading white spaces.
				while(*s && *s <= ' ')
				{
					s++;
				}

				// Call the appropriate commands based on the match.
				switch (match)
				{
					case QTV_FILE_STREAM :
						Cbuf_AddText(va("qtvplay \"%s\"\n", s));
						break;
					case QTV_FILE_JOIN :
						Cbuf_AddText(va("join \"%s\"\n", s));
						break;
					case QTV_FILE_OBSERVE :
						Cbuf_AddText(va("observe \"%s\"\n", s));
						break;
				}

				// Break the while-loop we found what we want.
				break;
			}
		}

		// Close the file.
		fclose(f);
		return;
	}

	// The argument is the host address for the QTV server.

	// Find the position of the last @ char in the connection string,
	// this is when the user specifies a specific stream # on the QTV proxy
	// that he wants to stream. Default is to stream the first one.
	// [stream@]hostname[:port]
	// (QTV proxies can be chained, so we must get the last @)
	// In other words split stream and host part

	strlcpy(stream_host, connrequest, sizeof(stream_host));

	connrequest = strchrrev(stream_host, '@');
	if (connrequest)
	{
		stream = stream_host; // stream part
		connrequest[0] = 0; // truncate
		host   = connrequest + 1; // host part
	}
	else
	{
		stream = ""; // use default stream, user not specifie stream part
		host   = stream_host; // arg is just host
	}
	
	// Open a TCP socket to the specified host.
	newf = FS_OpenTCP(host);

	// Failed to open the connection. 
	if (!newf)
	{
		Com_Printf("Couldn't connect to proxy %s\n", host);
		return;
	}

	// Send a QTV request to the proxy.
	connrequest =	"QTV\n"
					"VERSION: 1\n";
	VFS_WRITE(newf, connrequest, strlen(connrequest));

	// If the user specified a specific stream such as "5@hostname:port"
	// we need to send a SOURCE request.
	if (stream[0])
	{
		connrequest =	"SOURCE: ";
		VFS_WRITE(newf, connrequest, strlen(connrequest));
		connrequest =	stream;
		VFS_WRITE(newf, connrequest, strlen(connrequest));
		connrequest =	"\n";
		VFS_WRITE(newf, connrequest, strlen(connrequest));
	}

	// Two \n\n tells the server we're done.
	connrequest =	"\n";
	VFS_WRITE(newf, connrequest, strlen(connrequest));

	// We're finished requesting, but not done yet so save the
	// socket for the actual streaming :)
	QTV_CloseRequest(false);
	qtvrequest = newf;	
}


//=============================================================================
//								DEMO TOOLS
//=============================================================================

//
// Sets the playback speed of a demo.
//
void CL_Demo_SetSpeed_f (void) 
{
	extern cvar_t cl_demospeed;

	if (Cmd_Argc() != 2) 
	{
		Com_Printf("Usage: %s [speed %%]\n", Cmd_Argv(0));
		return;
	}

	Cvar_SetValue(&cl_demospeed, atof(Cmd_Argv(1)) / 100.0);
}

//
// Jumps to a specified time in a demo (forwards only).
//
void CL_Demo_Jump_f (void) 
{
    int seconds = 0, seen_col, relative = 0;
	double newdemotime;
    char *text, *s;
	static char *usage_message = "Usage: %s [+|-][m:]<s> (seconds)\n";

	// Cannot jump without playing demo.
	if (!cls.demoplayback) 
	{
		Com_Printf("Error: not playing a demo\n");
        return;
	}

	// Must be active to jump.
	if (cls.state < ca_active) 
	{	
		Com_Printf("Error: demo must be active first\n");
		return;
	}

	// Show usage.
    if (Cmd_Argc() != 2) 
	{
        Com_Printf(usage_message, Cmd_Argv(0));
        return;
    }

	// Get the jump string.
    text = Cmd_Argv(1);

	//
	// Parse which direction we're jumping in if
	// we're jumping relativly based on the current time.
	//
	if (text[0] == '-') 
	{
		// Jumping backwards.
		text++;
		relative = -1;
	} 
	else if (text[0] == '+') 
	{
		// Jumping forward.
		text++;
		relative = 1;
	}
	else if (!isdigit(text[0]))
	{
		// Incorrect input, show usage message.
        Com_Printf(usage_message, Cmd_Argv(0));
        return;
	}

	// Find the number of colons (max 2 allowed) and make sure
	// we only have digits in the string.
	for (seen_col = 0, s = text; *s; s++) 
	{
		if (*s == ':') 
		{
			seen_col++;
		} 
		else if (!isdigit(*s)) 
		{
			// Not a digit, show usage message.
			Com_Printf(usage_message, Cmd_Argv(0));
			return;			
		}

		if (seen_col >= 2) 
		{
			// More than two colons found, show usage message.
			Com_Printf(usage_message, Cmd_Argv(0));
			return;			
		}
	}

	// If there's at least 1 colon we know everything
	// before it is minutes, so add it them to our jump time.
    if (strchr(text, ':')) 
	{
        seconds += 60 * atoi(text);
        text = strchr(text, ':') + 1;
    }

	// The numbers after the first colon will be seconds.
    seconds += atoi(text);
	cl.gametime += seconds;

	// Calculate the new demo time we want to jump to.
	newdemotime = relative ? (cls.demotime + relative * seconds) : (demostarttime + seconds);

	// Can't travel back in time :(
	if (newdemotime < cls.demotime) 
	{
		Com_Printf ("Error: cannot demo_jump backwards\n");
		return;
	}

	// Set the new demotime.
	cls.demotime = newdemotime;
}

//
// Inits the demo cache and adds demo commands.
//
void CL_Demo_Init (void) 
{
	int parm, democache_size;
	byte *democache_buffer;

	//
	// Init the demo cache if the user specified to use one.
	//
	democache_available = false;
	if ((parm = COM_CheckParm("-democache")) && parm + 1 < com_argc) 
	{
		democache_size = Q_atoi(com_argv[parm + 1]) * 1024;
		democache_size = max(democache_size, DEMOCACHE_MINSIZE);
		if ((democache_buffer = malloc(democache_size))) 
		{
			Com_Printf_State (PRINT_OK, "Democache initialized (%.1f MB)\n", (float) (democache_size) / (1024 * 1024));
			SZ_Init(&democache, democache_buffer, democache_size);
			democache_available = true;
		} 
		else 
		{
			Com_Printf_State (PRINT_FAIL, "Democache allocation failed\n");
		}
	}

	//
	// Add demo commands.
	//
	Cmd_AddCommand ("record", CL_Record_f);
	Cmd_AddCommand ("stop", CL_Stop_f);
	Cmd_AddCommand ("playdemo", CL_Play_f);
	Cmd_AddCommand ("timedemo", CL_TimeDemo_f);
	Cmd_AddCommand ("easyrecord", CL_EasyRecord_f);

	Cmd_AddCommand("demo_setspeed", CL_Demo_SetSpeed_f);
	Cmd_AddCommand("demo_jump", CL_Demo_Jump_f);

	//
	// QTV commands.
	//
	Cmd_AddCommand ("qtvplay", CL_QTVPlay_f);
	Cmd_AddCommand ("qtvlist", CL_QTVList_f);

	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
#ifdef _WIN32
	Cvar_Register(&demo_format);
#endif
	Cvar_Register(&demo_dir);

	Cvar_ResetCurrentGroup();
}
