#pragma once

// Windows cue playback remains in the platform host. Its miniaudio declarations
// and implementation now come from the engine's VoiceCapture dependency.

#include <string>

struct ma_engine;
struct ma_sound;

class CuePlayer
{
  public:
    CuePlayer();
    ~CuePlayer();

    // open_now=false 只记下路径，音频设备推迟到第一次播放时才打开：miniaudio 引擎一打开就占着
    // WASAPI 播放设备和一条常驻音频线程，没开语音输入的用户不该为它付内存和唤醒。
    bool init(const std::wstring &start_path, const std::wstring &end_path, bool open_now = true);
    void shutdown();

    void play_start();
    void play_end();

  private:
    bool open();
    bool load_sound(const std::wstring &path, ma_sound *sound, bool *loaded_flag, const char *label);
    bool play_sound(ma_sound *sound, bool loaded_flag, const char *label);
    std::string wstring_to_utf8(const std::wstring &wstr) const;

    ma_engine *engine_ = nullptr;
    ma_sound *start_sound_ = nullptr;
    ma_sound *end_sound_ = nullptr;
    std::wstring start_path_;
    std::wstring end_path_;
    // init 之后、shutdown 之前为 true；打开失败也会清掉，和原先一样之后的播放都静默跳过，不反复重试。
    bool configured_ = false;
    bool engine_initialized_ = false;
    bool start_loaded_ = false;
    bool end_loaded_ = false;
};
