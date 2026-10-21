#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

// Client side of the settings bridge for the Direct2D window. It speaks the settings
// protocol of engine/contracts/webview: the host pushes
// `configSnapshot` and response messages, the window posts `configUpdate` and request
// messages. settings_app.cpp stays the only place that maps config paths to setters.
namespace native_settings
{
using Json = nlohmann::json;

class SettingsModel
{
  public:
    using Sender = std::function<void(const std::string &serializedMessage)>;
    using ConfigListener = std::function<void()>;
    using MessageListener = std::function<void(const Json &message)>;

    explicit SettingsModel(Sender sender);

    // `data` of the latest configSnapshot; an empty object before the first one.
    const Json &Config() const;
    bool HasConfig() const;
    // Dotted path into the snapshot ("appearance.font_size"); null when absent.
    const Json *Find(std::string_view path) const;
    bool GetBool(std::string_view path, bool fallback = false) const;
    std::string GetString(std::string_view path, const std::string &fallback = {}) const;
    int64_t GetInt(std::string_view path, int64_t fallback = 0) const;
    double GetNumber(std::string_view path, double fallback = 0.0) const;

    // Posts configUpdate. The value is also written into the local snapshot, so controls
    // that depend on it react at once, as the page does before the host answers; the
    // next snapshot from the host is authoritative either way.
    void Update(std::string_view path, Json value);
    // Posts any client message, e.g. Post("openSkinDirectory") or
    // Post("dictionaryRequest", {{"requestId", id}, ...}). `data` null omits the field.
    void Post(const std::string &type, Json data = nullptr);
    std::string NewRequestId(std::string_view prefix = "native");

    // Called after every snapshot (and after Update's local write).
    void AddConfigListener(ConfigListener listener);
    // Called for every host message of `type` other than configSnapshot.
    void AddMessageListener(const std::string &type, MessageListener listener);

    // Entry point for messages from the host (the JSON the page would receive).
    void Receive(const std::string &serializedMessage);

  private:
    void NotifyConfig();

    Sender sender_;
    Json config_ = Json::object();
    bool hasConfig_ = false;
    uint64_t nextRequestId_ = 1;
    std::vector<ConfigListener> configListeners_;
    std::map<std::string, std::vector<MessageListener>> messageListeners_;
    bool notifying_ = false;
    bool notifyAgain_ = false;
};
} // namespace native_settings
