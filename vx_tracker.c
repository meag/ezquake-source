/*
Copyright (C) 2011 VULTUREIIC

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.

See the GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
/**
	\file

	\brief
	Frags Tracker Screen Element

	\author
	VULTUREIIC
**/

#include "quakedef.h"
#include "utils.h"
#include "vx_tracker.h"
#include "server.h"
#include "gl_model.h"
#include "gl_local.h"

static void VX_TrackerAddText(char *msg, tracktype_t tt);

extern cvar_t		cl_useimagesinfraglog;

static int active_track = 0;
static int max_active_tracks = 0;

static void VXSCR_DrawTrackerString(void);

cvar_t		amf_tracker_flags			= {"r_tracker_flags", "0"};
cvar_t		amf_tracker_frags			= {"r_tracker_frags", "1"};
cvar_t		amf_tracker_streaks			= {"r_tracker_streaks", "0"};
cvar_t		amf_tracker_time			= {"r_tracker_time", "4"};
cvar_t		amf_tracker_messages		= {"r_tracker_messages", "10"};
cvar_t		amf_tracker_inconsole       = {"r_tracker_inconsole", "0"};
cvar_t		amf_tracker_align_right		= {"r_tracker_align_right", "1"};
cvar_t		amf_tracker_x				= {"r_tracker_x", "0"};
cvar_t		amf_tracker_y				= {"r_tracker_y", "0"};
cvar_t		amf_tracker_frame_color		= {"r_tracker_frame_color", "0 0 0 0", CVAR_COLOR};
cvar_t		amf_tracker_scale			= {"r_tracker_scale", "1"};
cvar_t		amf_tracker_images_scale	= {"r_tracker_images_scale", "1"};
cvar_t		amf_tracker_color_good      = {"r_tracker_color_good",     "090"}; // good news
cvar_t		amf_tracker_color_bad       = {"r_tracker_color_bad",      "900"}; // bad news
cvar_t		amf_tracker_color_tkgood    = {"r_tracker_color_tkgood",   "990"}; // team kill, not on ur team
cvar_t		amf_tracker_color_tkbad     = {"r_tracker_color_tkbad",    "009"}; // team kill, on ur team
cvar_t		amf_tracker_color_myfrag    = {"r_tracker_color_myfrag",   "090"}; // use this color for frag which u done
cvar_t		amf_tracker_color_fragonme  = {"r_tracker_color_fragonme", "900"}; // use this color when u frag someone
cvar_t		amf_tracker_color_suicide   = {"r_tracker_color_suicide",  "900"}; // use this color when u suicides
cvar_t		amf_tracker_string_suicides = {"r_tracker_string_suicides", " (suicides)"};
cvar_t		amf_tracker_string_died     = {"r_tracker_string_died",     " (died)"};
cvar_t		amf_tracker_string_teammate = {"r_tracker_string_teammate", "teammate"};
cvar_t		amf_tracker_string_enemy    = {"r_tracker_string_enemy",    "enemy"};
cvar_t		amf_tracker_name_width      = {"r_tracker_name_width",      "0"};
cvar_t		amf_tracker_name_remove_prefixes = {"r_tracker_name_remove_prefixes", ""};
cvar_t		amf_tracker_own_frag_prefix = {"r_tracker_own_frag_prefix", "You fragged "};
cvar_t		amf_tracker_positive_enemy_suicide = {"r_tracker_positive_enemy_suicide", "0"};	// Medar wanted it to be customizable

#define MAX_TRACKERMESSAGES 30
#define MAX_TRACKER_MSG_LEN 500
#define MAX_IMAGES_PER_LINE 2
#define MAX_LINES_PER_MESSAGE 2

typedef struct 
{
	char msg[MAX_TRACKER_MSG_LEN];
	float die;
	tracktype_t tt;

	// Pre-parse now, don't do this every frame
	char imagename[MAX_LINES_PER_MESSAGE][MAX_IMAGES_PER_LINE][64];
	int imagepos[MAX_LINES_PER_MESSAGE][MAX_IMAGES_PER_LINE];

	wchar content[MAX_LINES_PER_MESSAGE][MAX_TRACKER_MSG_LEN];
	int printable_length[MAX_LINES_PER_MESSAGE];
} trackmsg_t;

static trackmsg_t trackermsg[MAX_TRACKERMESSAGES];

static void VX_PreProcessMessage(trackmsg_t* msg);

static struct {
	double time;
	char text[MAX_SCOREBOARDNAME+20];
} ownfragtext;

static char ToHex(int v)
{
	if (v >= 0 && v < 10) {
		return '0' + v;
	}
	else if (v >= 0 && v < 16) {
		return 'a' + v - 10;
	}
	else {
		return 'f';
	}
}

static int VX_TrackerColourToStandard(const char* input, wchar* output, int position)
{
	// The tracker's strings use 9 as maximum, standard drawing functions expect f
	// Can't just change elsewhere as everyone's configs will have these set...
	if (isdigit(input[0]) && isdigit(input[1]) && isdigit(input[2])) {
		output[position++] = '&';
		output[position++] = 'c';
		output[position++] = ToHex(((input[0] - '0') / 9.0f) * 15);
		output[position++] = ToHex(((input[1] - '0') / 9.0f) * 15);
		output[position++] = ToHex(((input[2] - '0') / 9.0f) * 15);
		return position;
	}

	return position;
}

void InitTracker(void)
{
	Cvar_SetCurrentGroup(CVAR_GROUP_SCREEN);

	Cvar_Register (&amf_tracker_frags);
	Cvar_Register (&amf_tracker_flags);
	Cvar_Register (&amf_tracker_streaks);
	Cvar_Register (&amf_tracker_messages);
	Cvar_Register (&amf_tracker_inconsole);
	Cvar_Register (&amf_tracker_time);
	Cvar_Register (&amf_tracker_align_right);
	Cvar_Register (&amf_tracker_x);
	Cvar_Register (&amf_tracker_y);
	Cvar_Register (&amf_tracker_frame_color);
	Cvar_Register (&amf_tracker_scale);
	Cvar_Register (&amf_tracker_images_scale);

	Cvar_Register (&amf_tracker_color_good);	
	Cvar_Register (&amf_tracker_color_bad);
	Cvar_Register (&amf_tracker_color_tkgood);
	Cvar_Register (&amf_tracker_color_tkbad);
	Cvar_Register (&amf_tracker_color_myfrag);
	Cvar_Register (&amf_tracker_color_fragonme);
	Cvar_Register (&amf_tracker_color_suicide);

	Cvar_Register (&amf_tracker_string_suicides);
	Cvar_Register (&amf_tracker_string_died);
	Cvar_Register (&amf_tracker_string_teammate);
	Cvar_Register (&amf_tracker_string_enemy);

	Cvar_Register (&amf_tracker_name_width);
	Cvar_Register (&amf_tracker_name_remove_prefixes);
	Cvar_Register (&amf_tracker_own_frag_prefix);
	Cvar_Register (&amf_tracker_positive_enemy_suicide);
}

void VX_TrackerClear(void)
{
	int i;

	// this block VX_TrackerAddText() untill first VX_TrackerThink()
	//   which mean we connected and may start process it right
	active_track = max_active_tracks = 0;

	for (i = 0; i < MAX_TRACKERMESSAGES; i++) {
		trackermsg[i].die = -1;
		trackermsg[i].msg[0] = 0;
	}
	ownfragtext.text[0] = '\0';
}

//When a message fades away, this moves all the other messages up a slot
void VX_TrackerThink(void)
{
	int i;

	if (!(amf_tracker_frags.value || amf_tracker_flags.value || amf_tracker_streaks.value)) {
		return;
	}

	VXSCR_DrawTrackerString();

	if (ISPAUSED) {
		return;
	}

	active_track = 0; // 0 slots active
	max_active_tracks = bound(0, amf_tracker_messages.value, MAX_TRACKERMESSAGES);

	for (i = 0; i < max_active_tracks; i++) {
		if (trackermsg[i].die < r_refdef2.time) {
			// inactive
			continue;
		}

		active_track = i+1; // i+1 slots active

		if (!i) {
			continue;
		}

		if (trackermsg[i-1].die < r_refdef2.time && trackermsg[i].die >= r_refdef2.time) {
			// free slot above
			memcpy(&trackermsg[i - 1], &trackermsg[i], sizeof(trackermsg[0]));
			memset(&trackermsg[i], 0, sizeof(trackermsg[0]));
			trackermsg[i].die = -1;

			active_track = i; // i slots active
			continue;
		}
	}
}

static void VX_TrackerAddText(char *msg, tracktype_t tt)
{
	int ic;

	if (!msg || !msg[0])
		return;

	if (CL_Demo_SkipMessage(true)) {
		return;
	}

	switch (tt) 
	{
		case tt_death:  if (!amf_tracker_frags.value)   return; break;
		case tt_streak: if (!amf_tracker_streaks.value) return; break;
		case tt_flag:   if (!amf_tracker_flags.value)   return; break;
		default: return;
	}

	ic = amf_tracker_inconsole.integer;
	if (ic == 1) {
		Com_Printf("%s\n", msg);
		return;
	} else if (ic == 2) {
		Com_Printf("%s\n", msg);
	} else if (ic == 3) {
		int flags = Print_flags[Print_current];
		Print_flags[Print_current] |= PR_NONOTIFY;
		Com_Printf("%s\n", msg);
		Print_flags[Print_current] = flags;
	}

	if (active_track >= max_active_tracks) { // free space by removing the oldest one
		int i;

		for (i = 1; i < max_active_tracks; i++) {
			memcpy(&trackermsg[i - 1], &trackermsg[i], sizeof(trackermsg[0]));
		}
		active_track = max_active_tracks - 1;
	}

	strlcpy(trackermsg[active_track].msg, msg, sizeof(trackermsg[0].msg));
	trackermsg[active_track].die = r_refdef2.time + max(0, amf_tracker_time.value);
	trackermsg[active_track].tt = tt;
	VX_PreProcessMessage(&trackermsg[active_track]);
	++active_track;
}

static char *VX_RemovePrefix(int player)
{
	size_t skip;
	char *prefixes, *prefix, *name;

	if (amf_tracker_name_remove_prefixes.string[0] == 0)
		return cl.players[player].name;

	skip = 0;
	prefixes = Q_normalizetext(Q_strdup(amf_tracker_name_remove_prefixes.string));
	prefix = strtok(prefixes, " ");
	name = Q_normalizetext(Q_strdup(cl.players[player].name));

	while (prefix != NULL) {
		if (strlen(prefix) > skip && strlen(name) > strlen(prefix) && strncasecmp(prefix, name, strlen(prefix)) == 0) {
			skip = strlen(prefix);
			// remove spaces from the new start of the name
			while (name[skip] == ' ')
				skip++;
			// if it would skip the whole name, just use the whole name
			if (name[skip] == 0) {
				skip = 0;
				break;
			}
		}
		prefix = strtok(NULL, " ");
	}

	Q_free(prefixes);
	Q_free(name);

	return cl.players[player].name + skip;
}

static char *VX_Name(int player)
{
	static char string[2][MAX_SCOREBOARDNAME];
	static int idx = 0;
	int length;
	int i;

	length = bound(amf_tracker_name_width.integer, 0, MAX_SCOREBOARDNAME - 1);

	strlcpy (string[++idx % 2], VX_RemovePrefix(player), MAX_SCOREBOARDNAME);

	if (length > 0) {
		// align by adding spaces
		for (i = min(strlen(string[idx % 2]), length); i < length; i++) {
			string[idx % 2][i] = ' ';
		}
		string[idx % 2][i] = 0;
	}

	return string[idx % 2];
}

// Own Frags Text
static void VX_OwnFragNew(const char *victim)
{
	ownfragtext.time = r_refdef2.time;
	snprintf(ownfragtext.text, sizeof(ownfragtext.text), "%s%s", amf_tracker_own_frag_prefix.string, victim);
}

int VX_OwnFragTextLen(void)
{
	return (int) strlen_color(ownfragtext.text);
}

double VX_OwnFragTime(void)
{
	return r_refdef2.time - ownfragtext.time;
}

const char * VX_OwnFragText(void)
{
	return ownfragtext.text;
}

// return true if player enemy comparing to u, handle spectator mode
qbool VX_TrackerIsEnemy(int player)
{
	int selfnum;

	if (!cl.teamplay) // non teamplay mode, so player is enemy if not u 
		return !(cl.playernum == player || (player == Cam_TrackNum() && cl.spectator));

	// ok, below is teamplay

	if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
		return false;

	selfnum = (cl.spectator ? Cam_TrackNum() : cl.playernum);

	if (selfnum == -1)
		return true; // well, seems u r spec, but tracking noone

	return strncmp(cl.players[player].team, cl.players[selfnum].team, sizeof(cl.players[0].team)-1);
}

// is this player you, or you track him in case of spec
static qbool its_you(int player)
{
	return (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator));
}

// some bright guy may set variable to "", thats bad, work around...
static char *empty_is_000(char *str)
{
	return (*str ? str : "000");
}

static char *SuiColor(int player)
{
	if (its_you(player))
		return empty_is_000(amf_tracker_color_suicide.string);

	// with images color_bad == enemy color
	// without images color bad == bad frag for us
	if (cl_useimagesinfraglog.integer) {
		if (amf_tracker_positive_enemy_suicide.integer)
			return (VX_TrackerIsEnemy(player) ? empty_is_000(amf_tracker_color_good.string) : empty_is_000(amf_tracker_color_bad.string));
		else
			return (VX_TrackerIsEnemy(player) ? empty_is_000(amf_tracker_color_bad.string) : empty_is_000(amf_tracker_color_good.string));
	} else {
		return (VX_TrackerIsEnemy(player) ? empty_is_000(amf_tracker_color_good.string) : empty_is_000(amf_tracker_color_bad.string));
	}
}

static char *XvsYColor(int player, int killer)
{
	if (its_you(player))
		return empty_is_000(amf_tracker_color_fragonme.string);

	if (its_you(killer))
		return empty_is_000(amf_tracker_color_myfrag.string);

	return (VX_TrackerIsEnemy(player) ? empty_is_000(amf_tracker_color_good.string) : empty_is_000(amf_tracker_color_bad.string));
}

static char *OddFragColor(int killer)
{
	if (its_you(killer))
		return empty_is_000(amf_tracker_color_myfrag.string);

	return (!VX_TrackerIsEnemy(killer) ? empty_is_000(amf_tracker_color_good.string) : empty_is_000(amf_tracker_color_bad.string));
}

static char *EnemyColor(void)
{
	return empty_is_000(amf_tracker_color_bad.string);
}

// well, I'm too lazy making different functions for each type of TK
static char *TKColor(int player)
{
	return (VX_TrackerIsEnemy(player) ? empty_is_000(amf_tracker_color_tkgood.string) : empty_is_000(amf_tracker_color_tkbad.string));
}

void VX_TrackerDeath(int player, int weapon, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s&c%s%s&r", SuiColor(player), VX_Name(player), GetWeaponName(weapon), SuiColor(player), amf_tracker_string_died.string);
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r%s", VX_Name(player), SuiColor(player), GetWeaponName(weapon), amf_tracker_string_died.string);
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c960You died&r\n%s deaths: %i", GetWeaponName(weapon), count);
	}

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerSuicide(int player, int weapon, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s&c%s%s&r", SuiColor(player), VX_Name(player), GetWeaponName(weapon), SuiColor(player), amf_tracker_string_suicides.string);
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r%s", VX_Name(player), SuiColor(player), GetWeaponName(weapon), amf_tracker_string_suicides.string);
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c960You killed yourself&r\n%s suicides: %i", GetWeaponName(weapon), count);
	}

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerFragXvsY(int player, int killer, int weapon, int player_wcount, int killer_wcount)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s &c%s%s&r", XvsYColor(player, killer), VX_Name(killer), GetWeaponName(weapon), XvsYColor(killer, player), VX_Name(player));
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r %s", VX_Name(killer), XvsYColor(player, killer), GetWeaponName(weapon), VX_Name(player));
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
		snprintf(outstring, sizeof(outstring), "&r%s &c900killed you&r\n%s deaths: %i", cl.players[killer].name, GetWeaponName(weapon), player_wcount);
	else if (cl.playernum == killer || (killer == Cam_TrackNum() && cl.spectator))
		snprintf(outstring, sizeof(outstring), "&c900You killed &r%s\n%s kills: %i", cl.players[player].name, GetWeaponName(weapon), killer_wcount);

	if (cl.playernum == killer || (killer == Cam_TrackNum() && cl.spectator))
		VX_OwnFragNew(cl.players[player].name);

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerOddFrag(int player, int weapon, int wcount)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s &c%s%s&r", OddFragColor(player), VX_Name(player), GetWeaponName(weapon), EnemyColor(), amf_tracker_string_enemy.string);
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r %s", VX_Name(player), OddFragColor(player), GetWeaponName(weapon), amf_tracker_string_enemy.string);
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
		snprintf(outstring, sizeof(outstring), "&c900You killed&r an enemy\n%s kills: %i", GetWeaponName(weapon), wcount);

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerTK_XvsY(int player, int killer, int weapon, int p_count, int p_icount, int k_count, int k_icount)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s &c%s%s&r", TKColor(player), VX_Name(killer), GetWeaponName(weapon), TKColor(player), VX_Name(player));
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r %s", VX_Name(killer), TKColor(player), GetWeaponName(weapon), VX_Name(player));
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c380Teammate&r %s &c900killed you\nTimes: %i\nTotal Teamkills: %i", cl.players[killer].name, p_icount, p_count);
	}
	else if (cl.playernum == killer || (killer == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c900You killed &c380teammate&r %s\nTimes: %i\nTotal Teamkills: %i", cl.players[player].name, k_icount, k_count);
	}

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerOddTeamkill(int player, int weapon, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s &c%s%s&r", TKColor(player), VX_Name(player), GetWeaponName(weapon), TKColor(player), amf_tracker_string_teammate.string);
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r %s", VX_Name(player), TKColor(player), GetWeaponName(weapon), amf_tracker_string_teammate.string);
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c900You killed &c380a teammate&r\nTotal Teamkills: %i", count);
	}

	VX_TrackerAddText(outstring, tt_death);
}

void VX_TrackerOddTeamkilled(int player, int weapon)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";

	if (amf_tracker_frags.value == 2)
	{
		if (cl_useimagesinfraglog.integer)
		{
			snprintf(outstring, sizeof(outstring), "&c%s%s&r %s &c%s%s&r", TKColor(player), amf_tracker_string_teammate.string, GetWeaponName(weapon), TKColor(player), VX_Name(player));
			Q_normalizetext(outstring);
		}
		else
		{
			snprintf(outstring, sizeof(outstring), "&r%s &c%s%s&r %s", amf_tracker_string_teammate.string, TKColor(player), GetWeaponName(weapon), VX_Name(player));
		}
	}
	else if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
	{
		snprintf(outstring, sizeof(outstring), "&c380Teammate &c900killed you&r");
	}

	VX_TrackerAddText(outstring, tt_death);
}


void VX_TrackerFlagTouch(int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	snprintf(outstring, sizeof(outstring), "&c960You've taken the flag&r\nFlags taken: %i", count);
	VX_TrackerAddText(outstring, tt_flag);
}

void VX_TrackerFlagDrop(int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	snprintf(outstring, sizeof(outstring), "&c960You've dropped the flag&r\nFlags dropped: %i", count);
	VX_TrackerAddText(outstring, tt_flag);
}

void VX_TrackerFlagCapture(int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	snprintf(outstring, sizeof(outstring), "&c960You've captured the flag&r\nFlags captured: %i", count);
	VX_TrackerAddText(outstring, tt_flag);
}

//STREAKS
typedef struct
{
	int frags;
	char *spreestring;
	char *name; //internal name
	char *wavfilename;
} killing_streak_t;

killing_streak_t	tp_streak[] = 
{
	{ 100, "teh chet",           "0wnhack",     "client/streakx6.wav"},
	{ 50,  "the master now",     "master",      "client/streakx5.wav"},
	{ 20,  "godlike",            "godlike",     "client/streakx4.wav"},
	{ 15,  "unstoppable",        "unstoppable", "client/streakx3.wav"},
	{ 10,  "on a rampage",       "rampage",     "client/streakx2.wav"},
	{ 5,   "on a killing spree", "spree",       "client/streakx1.wav"},
};						

#define NUMSTREAK (sizeof(tp_streak) / sizeof(tp_streak[0]))

killing_streak_t *VX_GetStreak (int frags)	
{
	unsigned int i;
	killing_streak_t *streak = tp_streak;

	for (i = 0, streak = tp_streak; i < NUMSTREAK; i++, streak++)
		if (frags >= streak->frags)
			return streak;

	return NULL;
}

void VX_TrackerStreak (int player, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	killing_streak_t *streak = VX_GetStreak(count);
	if (!streak)
		return;
	if (count != streak->frags)
		return;

	if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
		snprintf(outstring, sizeof(outstring), "&c940You are %s (%i kills)", streak->spreestring, count);
	else
		snprintf(outstring, sizeof(outstring), "&r%s &c940is %s (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), streak->spreestring, count);
	VX_TrackerAddText(outstring, tt_streak);
}

void VX_TrackerStreakEnd(int player, int killer, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	killing_streak_t *streak = VX_GetStreak(count);
	if (!streak)
		return;

	if (player == killer) // streak ends due to suicide
	{
		char* userinfo_gender = Info_ValueForKey(cl.players[player].userinfo, "gender");
		char gender;
		if (! *userinfo_gender)
			userinfo_gender = Info_ValueForKey(cl.players[player].userinfo, "g");

		gender = userinfo_gender[0];
		if (gender == '0' || gender == 'M')
			gender = 'm';
		else if (gender == '1' || gender == 'F')
			gender = 'f';
		else if (gender == '2' || gender == 'N')
			gender = 'n';

		if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
			snprintf(outstring, sizeof(outstring), "&c940You were looking good until you killed yourself (%i kills)", count);
		else if (gender == 'm')
			snprintf(outstring, sizeof(outstring), "&r%s&c940 was looking good until he killed himself (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
		else if (gender == 'f')
			snprintf(outstring, sizeof(outstring), "&r%s&c940 was looking good until she killed herself (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
		else if (gender == 'n')
			snprintf(outstring, sizeof(outstring), "&r%s&c940 was looking good until it killed itself (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
		else
			snprintf(outstring, sizeof(outstring), "&r%s&c940 was looking good, then committed suicide (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
	}
	else // non suicide
	{
		if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator))
			snprintf(outstring, sizeof(outstring), "&c940Your streak was ended by &r%s&c940 (%i kills)", Info_ValueForKey(cl.players[killer].userinfo, "name"), count);
		else if (cl.playernum == killer || (killer == Cam_TrackNum() && cl.spectator))
			snprintf(outstring, sizeof(outstring), "&r%s&c940's streak was ended by you (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
		else
			snprintf(outstring, sizeof(outstring), "&r%s&c940's streak was ended by &r%s&c940 (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), Info_ValueForKey(cl.players[killer].userinfo, "name"), count);
	}
	VX_TrackerAddText(outstring, tt_streak);
}

void VX_TrackerStreakEndOddTeamkilled(int player, int count)
{
	char outstring[MAX_TRACKER_MSG_LEN]="";
	killing_streak_t *streak = VX_GetStreak(count);
	if (!streak) {
		return;
	}

	if (cl.playernum == player || (player == Cam_TrackNum() && cl.spectator)) {
		snprintf(outstring, sizeof(outstring), "&c940Your streak was ended by teammate (%i kills)", count);
	}
	else {
		snprintf(outstring, sizeof(outstring), "&r%s&c940's streak was ended by teammate (%i kills)", Info_ValueForKey(cl.players[player].userinfo, "name"), count);
	}

	VX_TrackerAddText(outstring, tt_streak);
}

// We need a seperate function, since our messages are in colour... and transparent
void VXSCR_DrawTrackerString (void)
{
	char	fullpath[MAX_PATH];
	int		x, y;
	int		i, printable_chars;
	int     line;
	float	alpha = 1;
	float	scale = bound(0.1, amf_tracker_scale.value, 10);
	float	im_scale = bound(0.1, amf_tracker_images_scale.value, 10);

	if (!active_track) {
		return;
	}

	StringToRGB(amf_tracker_frame_color.string);

	// Draw the max allowed trackers allowed at the same time
	// the latest ones are always shown.
	y = vid.height * 0.2 / scale + amf_tracker_y.value;
	for (i = 0; i < max_active_tracks; i++)
	{
		// Time expired for this tracker, don't draw it.
		if (trackermsg[i].die < r_refdef2.time) {
			continue;
		}

		// Fade the text as it gets older.
		alpha = min(1, (trackermsg[i].die - r_refdef2.time) / 2);

		for (line = 0; line < MAX_LINES_PER_MESSAGE; ++line) {
			printable_chars = trackermsg[i].printable_length[line];

			if (printable_chars <= 0) {
				break;
			}

			// Place the tracker.
			x = scale * (amf_tracker_align_right.value ? (vid.width / scale - printable_chars * 8) - 8 : 8);
			x += amf_tracker_x.value;

			// Draw the string.
			Draw_SColoredAlphaString(x, y, trackermsg[i].content[line], NULL, 0, 0, scale, alpha);

			y += 8 * scale;	// Next line.
		}
	}

	// Draw images
	y = vid.height * 0.2 / scale + amf_tracker_y.value;
	for (i = 0; i < max_active_tracks; i++) {
		// Time expired for this tracker, don't draw it.
		if (trackermsg[i].die < r_refdef2.time) {
			continue;
		}

		// Fade the text as it gets older.
		alpha = min(1, (trackermsg[i].die - r_refdef2.time) / 2);

		for (line = 0; line < MAX_LINES_PER_MESSAGE; ++line) {
			int img;

			printable_chars = trackermsg[i].printable_length[line];
			if (printable_chars <= 0) {
				break;
			}

			// Place the tracker.
			x = scale * (amf_tracker_align_right.value ? (vid.width / scale - printable_chars * 8) - 8 : 8);
			x += amf_tracker_x.value;

			for (img = 0; img < MAX_IMAGES_PER_LINE; ++img) {
				mpic_t *pic;

				if (!trackermsg[i].imagename[line][img][0]) {
					break;
				}

				snprintf(fullpath, sizeof(fullpath), "textures/tracker/%s", trackermsg[i].imagename[line][img]);

				if ((pic = Draw_CachePicSafe(fullpath, false, true))) {
					Draw_FitPicAlpha(
						(float)x + (trackermsg[i].imagepos[line][img] * 8 * scale) - 0.5 * 8 * 2 * (im_scale - 1) * scale,
						(float)y - 0.5 * 8 * (im_scale - 1) * scale,
						im_scale * 8 * 2 * scale,
						im_scale * 8 * scale, pic, alpha
					);
				}
			}
		}
		y += 8 * scale;	// Next line.
	}
}

// Tracker used to step through every character twice every draw frame
// Now pre-process and throw string at renderer instead
static void VX_PreProcessMessage(trackmsg_t* msg)
{
	const char* start = msg->msg;
	int l, printable_chars, content_pos;
	int line = 0;
	int img = 0;

	memset(msg->imagename, 0, sizeof(msg->imagename));
	memset(msg->imagepos, -1, sizeof(msg->imagepos));
	memset(msg->content, 0, sizeof(msg->content));
	memset(msg->printable_length, 0, sizeof(msg->printable_length));

	// Loop through the tracker message and parse it.
	while (start[0] && line < MAX_LINES_PER_MESSAGE)
	{
		img = content_pos = l = printable_chars = 0;

		// Find the number of printable characters for the next line.
		while (start[l] && start[l] != '\n')
		{
			// Look for any escape codes for images and color codes.

			// Image escape.
			if (start[l] == '\\') 
			{
				// We found opening slash, get image name now.
				int from, to;

				from = to = ++l;

				for( ; start[l]; l++) 
				{
					if (start[l] == '\n')
						break; // Something bad, we didn't find a closing slash.

					if (start[l] == '\\')
						break; // Found a closing slash.

					to = l + 1;
				}

				if (to > from) {
					// We got potential image name, treat image as two printable characters.
					if (to - from < sizeof(msg->imagename[0][0]) && img < MAX_IMAGES_PER_LINE) {
						msg->imagepos[line][img] = printable_chars;
						strncpy(msg->imagename[line][img], start + from, to - from);
						++img;
					}

					msg->content[line][content_pos++] = ' ';
					msg->content[line][content_pos++] = ' ';
					printable_chars += 2;
				}

				if (start[l] == '\\') {
					l++; // Advance.
				}

				continue;
			}

			// Get rid of color codes.
			if (start[l] == '&')
			{
				if (start[l + 1] == 'r') 
				{
					msg->content[line][content_pos++] = '&';
					msg->content[line][content_pos++] = 'r';
					l += 2;
					continue;
				}
				else if (start[l + 1] == 'c' && start[l + 2] && start[l + 3] && start[l + 4]) 
				{
					content_pos = VX_TrackerColourToStandard(start + l + 2, msg->content[line], content_pos);

					l += 5;
					continue;
				}
			}

			msg->content[line][content_pos++] = start[l];
			printable_chars++;	// Increment count of printable chars.
			l++;				// Increment count of any chars in string untill end or new line.
		}

		msg->printable_length[line] = printable_chars;

		start += l;
		line++;

		if (*start == '\n') {
			start++; // Skip the \n
		}
	}
}

void VX_TrackerInit(void)
{
	int i;
	char fullpath[MAX_PATH];

	// Pre-cache weapon-class images
	for (i = 0; i < MAX_WEAPON_CLASSES; ++i) {
		const char* image = GetWeaponImageName(i);

		// FIXME: This is largely duplicated from VX_PreProcessMessage
		if (image && image[0]) {
			const char* start = image;
			int l = 0;

			while (start[l] && start[l] != '\n') {
				// Image escape.
				if (start[l] == '\\') {
					// We found opening slash, get image name now.
					int from, to;

					from = to = ++l;

					for (; start[l]; l++) {
						if (start[l] == '\n')
							break; // Something bad, we didn't find a closing slash.

						if (start[l] == '\\')
							break; // Found a closing slash.

						to = l + 1;
					}

					if (to > from) {
						char imagename[128];

						// We got potential image name, treat image as two printable characters.
						if (to - from < sizeof(imagename)) {
							strncpy(imagename, start + from, to - from);
							imagename[to - from] = '\0';

							snprintf(fullpath, sizeof(fullpath), "textures/tracker/%s", imagename);
							Draw_CachePicSafe(fullpath, false, true);
						}
					}

					if (start[l] == '\\') {
						l++; // Advance.
					}

					continue;
				}

				l++; // Increment count of any chars in string untill end or new line.
			}
		}
	}

	CachePics_MarkAtlasDirty();
}
