#include "plugin.h"
#include "usf.h"

#include "audio_hle.h"
#include "memory.h"

#include <glib.h>

#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>

#include <libaudcore/drct.h>
#include <libaudcore/mainloop.h>

bool usf_playing;
int32_t SampleRate = 0;
int16_t samplebuf[16384];
USFPlugin *context;

//EXPORT USFPlugin aud_plugin_instance;
__attribute__((visibility("default"))) USFPlugin aud_plugin_instance;

const char * const USFPlugin::exts[] = {
    "usf", "miniusf", nullptr
};


bool USFPlugin::init(){
    usf_init();
    return true;
}


void USFPlugin::cleanup (){
}

bool USFPlugin::is_our_file (const char * filename, VFSFile & file){
    // LoadUSF() writes into the global savestatespace buffer whenever the
    // file has an embedded savestate section (true of every real USF/miniusf
    // file), so it must not run without PreAllocate_Memory() first having
    // allocated that buffer - otherwise it writes through a null pointer.
    if (!PreAllocate_Memory()) {
	Release_Memory();
	return false;
    }
    if (!LoadUSF(filename, &file)) {
	Release_Memory();
	return false;
    }
    else{
	Release_Memory();
        return true;
    }
    return false;
}

Tuple USFPlugin::read_tuple (const char * filename, VFSFile & file){
    return usf_get_song_tuple(filename, &file);
}

bool USFPlugin::read_tag (const char * filename, VFSFile & file, Tuple & tuple, Index<char> * image){
    tuple = usf_get_song_tuple(filename, &file);
    return true;
}

// Audacious's own "track ended, advance to the next one" handoff (queuing
// end_cb() from the playback thread, which then calls playlist.next_song())
// is gated behind an in_sync() check tying it to a serial number shared with
// the main thread. We've observed that gate silently never open - play()
// returns cleanly, but nothing further ever happens automatically: no
// probing of the next track, no further play() call, nothing - even though
// the playlist position/title can still update via some other path. This
// timer is a safety net, not a replacement: ~750ms after any play() call
// returns, check whether playback actually is happening: if so (the normal
// case), do nothing; if the playlist has legitimately run out (position
// -1, mirroring how Audacious itself signals "reached end of playlist") do
// nothing; otherwise explicitly (re)start playback at the current position,
// the same call a user manually pressing play would trigger - a path we've
// confirmed works reliably where the automatic one does not.
//
// drct.h functions are documented as not thread-safe, so this must not run
// directly on our decode thread; QueuedFunc is the documented way to safely
// call back into the main thread from a worker thread.
static QueuedFunc stall_recovery;

static void check_for_stalled_advance()
{
    if (aud_drct_get_playing())
	return;
    if (aud_drct_get_position() < 0)
	return;
    aud_drct_play();
}

bool USFPlugin::play (const char * filename, VFSFile & file){
    bool result = usf_play(this, filename, &file);
    stall_recovery.queue(750, check_for_stalled_advance);
    return result;
}


void USFPlugin::open_sound(){
    open_audio(FMT_S16_NE, SampleRate, 2);
}

void USFPlugin::add_buffer(unsigned char *buf, unsigned int length){
	if(check_stop()){
		usf_stop(this);
		return;
	}

    int seek_ms = check_seek();
    if (seek_ms != -1) {
	usf_mseek(this, seek_ms);
    }

    int32_t i = 0, out = 0;
    double vol = 1.0;

    if (!cpu_running)
	return;

    if (is_seeking) {
	play_time +=
	    (((double) (length >> 2) / (double) SampleRate) * 1000.0);
	if (play_time > (double) seek_time) {
	    is_seeking = 0;
	}
	return;
    }

    if (play_time > track_time) {
	vol =
	    1.0f -
	    (((double) play_time -
	      (double) track_time) / (double) fade_time);
    }

    for (out = i = 0; i < (length >> 1); i += 2) {
	samplebuf[out++] =
	    (int16_t) (vol * (double) ((int16_t *) buf)[i + 1]);
	samplebuf[out++] = (int16_t) (vol * (double) ((int16_t *) buf)[i]);
    }

    play_time += (((double) (length >> 2) / (double) SampleRate) * 1000.0);

    usf_playing = play_time < (track_time + fade_time);

    // Match the documented check_stop() contract (return as soon as you
    // decide to stop, without writing further audio) instead of writing one
    // more buffer after already having decided the track is over.
    if (play_time > (track_time + fade_time)) {
	cpu_running = 0;
	return;
    }

    write_audio(samplebuf, length);
}

void USFPlugin::ai_len_changed(){
    int32_t length = 0;
    uint32_t address = (AI_DRAM_ADDR_REG & 0x00FFFFF8);

    length = AI_LEN_REG & 0x3FFF8;

    while (is_paused && !is_seeking && cpu_running)
	g_usleep(10000);


    add_buffer(RDRAM + address, length);

    if (enableFIFOfull) {
	AI_STATUS_REG |= 0x40000000;
    }

    StartAiInterrupt();

}

unsigned USFPlugin::ai_read_length(){
    AI_LEN_REG = 0;
    return AI_LEN_REG;
}

void USFPlugin::ai_dacrate_changed(unsigned int value){
    AI_DACRATE_REG = value;
    SampleRate = 48681812 / (AI_DACRATE_REG + 1);
}
