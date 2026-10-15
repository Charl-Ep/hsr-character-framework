#pragma once
#include "../application/ApplicationService.hpp"
#include <filesystem>
#include <memory>

namespace hksr::framework {
struct HttpSettings {
    bool enabled=false;
    std::string address="127.0.0.1";
    unsigned port=0;
    std::string token;
    std::vector<std::string> origins;
};
// Strict INI syntax in a .cfg file (not consumed by the mod INI loader).
// Missing file means disabled. No DNS lookup, listener or game calls here.
HttpSettings ReadHttpSettings(const std::filesystem::path& path);

struct HttpStatus {bool listening=false;unsigned port=0;std::string error;};
// Owner-thread Start/Stop/Status. Network callbacks only use ApplicationService
// values and queues. Stop joins all network workers before the service dies;
// it neither stops the application nor cancels already posted commands.
class HttpControl {
public:
    explicit HttpControl(ApplicationService&);
    ~HttpControl();
    HttpControl(const HttpControl&)=delete;
    HttpControl& operator=(const HttpControl&)=delete;
    void Start(const HttpSettings&); // throws on configuration/bind failure
    void Stop();
    HttpStatus Status() const;
private:
    struct State;
    ApplicationService& application_;
    std::unique_ptr<State> state_;
};
}
