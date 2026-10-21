#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Jev
{
inline constexpr const char *kEndpoint = "https://api.typesafe.ai/v1/systemone";
inline constexpr const char *kModel = "jev-1.13.0";
inline constexpr size_t kMaxCandidates = 32;

nlohmann::json BuildRequest(const std::string &model, const std::string &context,
                            const std::vector<std::string> &pinyin, const std::vector<std::string> &candidates);
// Unknown choices, abstentions and malformed responses never become input text.
std::string ParseChoice(const std::string &response, const std::vector<std::string> &candidates);
// Stack-owned by the worker so connection cleanup runs before Windows TLS teardown.
class Client
{
  public:
    Client();
    ~Client();
    Client(const Client &) = delete;
    Client &operator=(const Client &) = delete;
    std::optional<std::string> Fetch(const std::string &endpoint, const std::string &token, const std::string &payload,
                                     const std::function<bool()> &cancelled = {});

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
// Official HTTPS endpoint only. The callback is polled while the request is in flight.
std::optional<std::string> Fetch(const std::string &endpoint, const std::string &token, const std::string &payload,
                                 const std::function<bool()> &cancelled = {});
} // namespace Jev
