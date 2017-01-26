/*

Copyright (C) 2001-2002       A Nourai

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

	$Id: movie.c,v 1.26 2007-10-11 06:46:12 borisu Exp $
*/

#include "quakedef.h"
#include "utils.h"
#include "qsound.h"
#ifdef _WIN32
#include "movie_avi.h"	//joe: capturing to avi
#include <windows.h>

static qbool movie_is_avi = false;
static SYSTEMTIME movie_start_date;
#else
#include <time.h>
struct tm movie_start_date;
#endif // _WIN32

static void OnChange_movie_dir(cvar_t *var, char *string, qbool *cancel);
static void WAVCaptureStop (void);
static void WAVCaptureStart (void);
int SCR_Screenshot(char *);

//joe: capturing audio
// Variables for buffering audio
static unsigned char aviSoundBuffer[4096] = { 0 };  // temporary buffer for the current frame
static short capture_audio_samples[44100];          // big enough buffer for 1fps at 44100Hz (keeps overlapping sound from previous frame)
static int captured_audio_samples;
static qbool frame_has_sound = false;

cvar_t   movie_fps        =  {"demo_capture_fps", "30.0"};
cvar_t   movie_dir        =  {"demo_capture_dir",  "capture", 0, OnChange_movie_dir};
cvar_t   movie_steadycam  =  {"demo_capture_steadycam", "0"};

extern cvar_t scr_sshot_type;

static char screenshotFolderPath[MAX_PATH];
static volatile qbool movie_is_capturing = false;  // set while capture is active
static double movie_start_time, movie_len;         // when movie started & number of seconds it should capture (in gametime)
static int movie_frame_count;                      // number of frames processed so far
static char image_ext[4];                          // set by /sshot_type as capture starts
static double movie_real_start_time;               // system time when capture began

qbool Movie_IsCapturing(void) {
	return cls.demoplayback && !cls.timedemo && movie_is_capturing;
}

float Movie_Frametime(void) {
	if (movie_steadycam.value)
		return movie_fps.value > 0 ? 1.0 / movie_fps.value : 1 / 30.0;
	return cls.trueframetime;
}

static void Movie_Start(double _time) 
{
	extern cvar_t scr_sshot_format;

#ifndef _WIN32
	time_t t;
	t = time(NULL);
	localtime_r(&t, &movie_start_date);
	snprintf(
		screenshotFolderPath, sizeof(screenshotFolderPath),
		"%s/capture_%02d-%02d-%04d_%02d-%02d-%02d/",
		movie_dir.string, movie_start_date.tm_mday, movie_start_date.tm_mon, movie_start_date.tm_year,
		movie_start_date.tm_hour, movie_start_date.tm_min, movie_start_date.tm_sec);
#else
	GetLocalTime(&movie_start_date);
	snprintf(
		screenshotFolderPath, sizeof(screenshotFolderPath),
		"%s/capture_%02d-%02d-%04d_%02d-%02d-%02d/",
		movie_dir.string, movie_start_date.wDay, movie_start_date.wMonth, movie_start_date.wYear,
		movie_start_date.wHour,	movie_start_date.wMinute, movie_start_date.wSecond);
#endif
	movie_len = _time;
	movie_start_time = cls.realtime;

	movie_frame_count = 0;

	#ifdef _WIN32
	if (movie_is_avi)	//joe: capturing to avi
	{
		movie_is_capturing = Capture_Open ();
	}
	else
	#endif
	{
		// DEFAULT_SSHOT_FORMAT
		if (!strcmp(scr_sshot_format.string, "tga")
		 || !strcmp(scr_sshot_format.string, "jpeg")
		 || !strcmp(scr_sshot_format.string, "jpg")
		 || !strcmp(scr_sshot_format.string, "png"))
		{
			strlcpy(image_ext, scr_sshot_format.string, sizeof(image_ext));		
		}
		else
		{
			strlcpy (image_ext, "tga", sizeof (image_ext));
		}
		movie_is_capturing = true;
		WAVCaptureStart ();
	}
	movie_real_start_time = Sys_DoubleTime();
}

void Movie_Stop (void) {
#ifdef _WIN32
	Capture_Stop();
#endif
	WAVCaptureStop ();

	Com_Printf("Captured %d frames in %.1fs.\n", movie_frame_count, Sys_DoubleTime() - movie_real_start_time);
	movie_is_capturing = false;
	movie_frame_count = 0;
}

void Movie_Demo_Capture_f(void) {
	int argc;
	double time;
	char *error;
	
#ifdef _WIN32
	error = va("Usage: %s (\"start\" time [avifile]) | \"stop\"\n", Cmd_Argv(0));
	if ((argc = Cmd_Argc()) != 2 && argc != 3 && argc != 4) {
#else
	error = va("Usage: %s (\"start\" time) | \"stop\"\n", Cmd_Argv(0));
	if ((argc = Cmd_Argc()) != 2 && argc != 3) {
#endif
		Com_Printf(error);
		return;
	}
	if (argc == 2) {
		if (strncasecmp("stop", Cmd_Argv(1), 4))
			Com_Printf(error);
		else if (Movie_IsCapturing()) 
			Movie_Stop();
		else
			Com_Printf("%s : Not capturing\n", Cmd_Argv(0));
		return;
	}
	if (strncasecmp("start", Cmd_Argv(1), 5)) {
		Com_Printf(error);
		return;
	} else if (Movie_IsCapturing()) {
		Com_Printf("%s : Already capturing\n", Cmd_Argv(0));
		return;
	}
	if (!cls.demoplayback || cls.timedemo) {
		Com_Printf("%s : Must be playing a demo to capture\n", Cmd_Argv(0));
		return;
	}
	if ((time = Q_atof(Cmd_Argv(2))) <= 0) {
		Com_Printf("%s : Time argument must be positive\n", Cmd_Argv(0));
		return;
	}
#ifdef _WIN32
	//joe: capturing to avi
	if (argc == 4) {
		if (!Capture_StartCapture(Cmd_Argv(3))) {
			return;
		}
		movie_is_avi = true;
	}
#endif
	Movie_Start(time);
}

void Movie_Init(void) {
	captured_audio_samples = 0;

	Cvar_SetCurrentGroup(CVAR_GROUP_DEMO);
	Cvar_Register(&movie_fps);
	Cvar_Register(&movie_dir);
	Cvar_Register(&movie_steadycam);

	Cvar_ResetCurrentGroup();

	Cmd_AddCommand("demo_capture", Movie_Demo_Capture_f);

#ifdef _WIN32
	Capture_InitAVI ();		//joe: capturing to avi
#endif
}

double Movie_FrameTime (void)
{
	// Default to 30 fps.
	return (movie_fps.value > 0) ? (1.0 / movie_fps.value) : (1 / 30.0);
}

double Movie_StartFrame(void) 
{
	double time;

	if (Cmd_FindAlias("f_captureframe"))
	{
		Cbuf_AddTextEx (&cbuf_main, "f_captureframe\n");
	}

	time = Movie_FrameTime();
	return bound(1.0 / 1000, time, 1.0);
}

void Movie_FinishFrame(void) 
{
	char fname[128];

	if (!Movie_IsCapturing())
		return;

#ifdef _WIN32
	if (movie_is_avi) {
		Capture_FinishFrame();
	}
	else
#endif
	{
		snprintf(fname, sizeof(fname), "%sshot-%06d.%s", screenshotFolderPath, movie_frame_count, image_ext);

		con_suppress = true;

		SCR_Screenshot(fname);

		con_suppress = false;
	}

	++movie_frame_count;
	if (cls.realtime >= movie_start_time + movie_len) {
		Movie_Stop ();
	}
}

//joe: capturing audio
#ifdef _WIN32
qbool Movie_IsCapturingAVI(void) {
	return movie_is_avi && Movie_IsCapturing();
}
#endif

static vfsfile_t *wav_output = NULL;
static int wav_sample_count = 0;

static void WAVCaptureStart (void)
{
	// WAV file format details taken from http://soundfile.sapp.org/doc/WaveFormat/
	char fname[MAX_PATH];

	int chunkSize = 0;  // We write 0 to start with, need to replace once we know number of samples
	int fmtBlockSize = LittleLong (16);
	short fmtFormat = LittleShort (1); // PCM, uncompressed
	short fmtNumberChannels = LittleShort (shw->numchannels);
	int fmtSampleRate = LittleLong (shw->khz);
	int fmtBitsPerSample = 16;
	int fmtByteRate = shw->khz * fmtNumberChannels * (fmtBitsPerSample / 8);  // 16 = 16 bit sound
	int fmtBlockAlign = (fmtBitsPerSample / 8) * shw->numchannels; // 16 bit sound

#ifdef _WIN32
	snprintf (fname, sizeof (fname), "%s/capture_%02d-%02d-%04d_%02d-%02d-%02d/audio.wav",
		movie_dir.string, movie_start_date.wDay, movie_start_date.wMonth, movie_start_date.wYear,
		movie_start_date.wHour, movie_start_date.wMinute, movie_start_date.wSecond);
#else
	snprintf (fname, sizeof (fname), "%s/capture_%02d-%02d-%04d_%02d-%02d-%02d/audio.wav",
		movie_dir.string, movie_start_date.tm_mday, movie_start_date.tm_mon, movie_start_date.tm_year,
		movie_start_date.tm_hour, movie_start_date.tm_min, movie_start_date.tm_sec);
#endif
	if (!(wav_output = FS_OpenVFS (fname, "wb", FS_NONE_OS))) {
		FS_CreatePath (fname);
		if (!(wav_output = FS_OpenVFS (fname, "wb", FS_NONE_OS)))
			return;
	}

	// RIFF header
	VFS_WRITE (wav_output, "RIFF", 4);
	VFS_WRITE (wav_output, &chunkSize, 4);
	VFS_WRITE (wav_output, "WAVE", 4);

	// "fmt " chunk (24 bytes total)
	VFS_WRITE (wav_output, "fmt ", 4);
	VFS_WRITE (wav_output, &fmtBlockSize, 4);
	VFS_WRITE (wav_output, &fmtFormat, 2);
	VFS_WRITE (wav_output, &fmtNumberChannels, 2);
	VFS_WRITE (wav_output, &fmtSampleRate, 4);
	VFS_WRITE (wav_output, &fmtByteRate, 4);
	VFS_WRITE (wav_output, &fmtBlockAlign, 2);
	VFS_WRITE (wav_output, &fmtBitsPerSample, 2);

	// "data" chunk
	VFS_WRITE (wav_output, "data", 4);
	VFS_WRITE (wav_output, &chunkSize, 4);

	// actual data will be written as audio mixed each frame
	wav_sample_count = 0;
}

static void WAVCaptureFrame (int samples, byte *sample_buffer)
{
	int i;

	if (wav_output) {
		for (i = 0; i < samples; ++i) {
			unsigned long original = LittleLong (*(unsigned long*)sample_buffer);

			VFS_WRITE (wav_output, &original, 4);
			sample_buffer += 4;
		}

		wav_sample_count += samples;
	}
}

static void WAVCaptureStop (void)
{
	if (wav_output) {
		// Now we know how many samples, so can fill in the blocks in the file
		int dataSize = wav_sample_count * 2 * shw->numchannels; // 16 bit sound, stereo
		int riffChunkSize = 36 + dataSize; // 36 = 4[format field] + (8 + chunk1size)[fmt block] + 8 [subchunk2Id + size fields]

		VFS_SEEK (wav_output, 4, SEEK_SET);
		VFS_WRITE (wav_output, &riffChunkSize, 4);
		VFS_SEEK (wav_output, 40, SEEK_SET);   // RIFF header = 12, fmt chunk 24, "data" 4
		VFS_WRITE (wav_output, &dataSize, 4);
		VFS_CLOSE (wav_output);

		wav_output = 0;
		wav_sample_count = 0;
	}
}

void Movie_MixFrameSound (void (*mixFunction)(void))
{
	int samples_required = (int)(0.5 + Movie_FrameTime() * shw->khz) * shw->numchannels - (captured_audio_samples << 1);

	memset(aviSoundBuffer, 0, sizeof(aviSoundBuffer));
	shw->buffer = (unsigned char*)aviSoundBuffer;
	shw->samples = min(samples_required, sizeof(aviSoundBuffer) / 2);
	frame_has_sound = false;

	do {
		mixFunction();
	} while (! frame_has_sound);
}

void Movie_TransferSound(void* data, int snd_linear_count)
{
	int samples_per_frame = (int)(0.5 + Movie_FrameTime() * shw->khz);

	// Write some sound
	memcpy(capture_audio_samples + (captured_audio_samples << 1), data, snd_linear_count * shw->numchannels);
	captured_audio_samples += (snd_linear_count >> 1);
	shw->snd_sent += snd_linear_count * shw->numchannels;

	if (captured_audio_samples >= samples_per_frame) {
		// We have enough audio samples to match one frame of video
#ifdef _WIN32
		if (movie_is_avi) {
			Capture_WriteAudio (samples_per_frame, (byte *)capture_audio_samples);
		}
		else
#else
		{
			WAVCaptureFrame (samples_per_frame, (byte *)capture_audio_samples);
		}
#endif
		memcpy (capture_audio_samples, capture_audio_samples + (samples_per_frame << 1), (captured_audio_samples - samples_per_frame) * 2 * shw->numchannels);
		captured_audio_samples -= samples_per_frame;

		frame_has_sound = true;
	}
}

static void OnChange_movie_dir(cvar_t *var, char *string, qbool *cancel) {
	if (Movie_IsCapturing()) {
		Com_Printf("Cannot change demo_capture_dir whilst capturing.  Use 'demo_capture stop' to cease capturing first.\n");
		*cancel = true;
		return;
	} else if (strlen(string) > 31) {
		Com_Printf("demo_capture_dir can only contain a maximum of 31 characters\n");
		*cancel = true;
		return;
	}
	Util_Process_Filename(string);
	if (!(Util_Is_Valid_Filename(string))) {
		Com_Printf(Util_Invalid_Filename_Msg("demo_capture_dir"));
		*cancel = true;
		return;
	}
}

void Movie_Shutdown (void)
{
	Capture_Shutdown ();
}
