#pragma once
#include "session.h"
#include <atomic>
#include <span>
#include <memory>
#include <mutex>
#include "freertos/FreeRTOS.h"
#include "freertos/stream_buffer.h"
#include "ssh_connection.hpp"
namespace tab5_shell {
bool verify_host_key(const char* host, std::uint16_t port, const std::uint8_t* fingerprint);
class ShellNetwork {
public:
    bool start(SshRequest request, std::string password, std::string& error);
    void stop();
    void send(std::span<const std::uint8_t> bytes);
    std::size_t receive(std::uint8_t* bytes, std::size_t capacity);
    bool busy() const { return busy_.load(); }
    bool connected() const { return connected_.load(); }
    const char* result() const { return result_.load(); }
    static std::string wifi_status();
private:
    static void task(void* context);
    SshRequest request_;
    std::string password_;
    std::atomic<bool> busy_{false}, connected_{false}, stop_{false};
    std::atomic<const char*> result_{"LOCAL"};
    StreamBufferHandle_t rx_{nullptr};
    std::unique_ptr<tab5::SshConnection> connection_;
    std::mutex transport_mutex_;
};
} // namespace tab5_shell
