#ifndef _PLUGIN_H_
#define _PLUGIN_H_

#include <mutex>

#include <libaudcore/plugin.h>
#include <libaudcore/i18n.h>
#include <libaudcore/preferences.h>

class USFPlugin : public InputPlugin
{
    public:
    static const char * const exts[];
    static constexpr PluginInfo info = {
        N_("USF plugin"),
        "",
        N_(
			"N64 USF music decoder plugin\n"
			"\n"
			"https://github.com/saschaklick/audacious-usf-plugin"
        )
    };
    static constexpr auto iinfo = InputInfo ()
        .with_exts (exts);

    constexpr USFPlugin () : InputPlugin (info, iinfo) {}

    bool init ();
    void cleanup ();
    bool is_our_file (const char * filename, VFSFile & file);
    Tuple read_tuple (const char * filename, VFSFile & file);
    bool play (const char * filename, VFSFile & file);
    bool read_tag (const char * filename, VFSFile & file, Tuple & tuple, Index<char> * image);

    void open_sound();
    void add_buffer(unsigned char *buf, unsigned int length);
    void ai_len_changed();
    unsigned ai_read_length(void);
    void ai_dacrate_changed(unsigned int value);
};

extern bool usf_playing;
extern int32_t SampleRate;
extern int16_t samplebuf[16384];
extern USFPlugin* context;

// LoadUSF()/PreAllocate_Memory()/Allocate_Memory()/Release_Memory() operate on
// a single shared set of global emulator buffers (savestatespace, ROMPages,
// MemChunk, N64MEM, ...) ported straight from PJ64, which never had to support
// more than one file at a time. Audacious' play-list scanner runs is_our_file()
// for multiple files concurrently (SCAN_THREADS in libaudcore/scanner.cc), and
// concurrent probes race on those globals - one thread's PreAllocate_Memory()
// can overwrite the pointer another thread is about to free in Release_Memory(),
// corrupting the heap. Both is_our_file() and play() must hold this lock for
// their entire body so only one file at a time ever touches that shared state.
extern std::mutex g_usf_state_mutex;

#endif /* _PLUGIN_H_ */
