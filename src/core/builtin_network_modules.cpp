#include "core/builtin_module_factories.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace ispcok {
namespace {

double ClampScore(double value)
{
    if (value < 0.0)
        return 0.0;
    if (value > 100.0)
        return 100.0;
    return value;
}

class SocketRuntime
{
public:
    SocketRuntime()
    {
#if defined(_WIN32)
        WSADATA wsa_data{};
        ok_ = (WSAStartup(MAKEWORD(2, 2), &wsa_data) == 0);
#else
        ok_ = false;
#endif
    }

    ~SocketRuntime()
    {
#if defined(_WIN32)
        if (ok_)
            WSACleanup();
#endif
    }

    bool ok() const { return ok_; }

private:
    bool ok_ = false;
};

class SocketHandle
{
public:
    using Raw = SOCKET;
    static constexpr Raw Invalid = INVALID_SOCKET;

    SocketHandle() = default;
    explicit SocketHandle(Raw value) : value_(value) {}
    ~SocketHandle()
    {
        if (value_ != Invalid)
            closesocket(value_);
    }

    SocketHandle(const SocketHandle&) = delete;
    SocketHandle& operator=(const SocketHandle&) = delete;

    SocketHandle(SocketHandle&& other) noexcept : value_(other.value_)
    {
        other.value_ = Invalid;
    }

    SocketHandle& operator=(SocketHandle&& other) noexcept
    {
        if (this != &other)
        {
            if (value_ != Invalid)
                closesocket(value_);
            value_ = other.value_;
            other.value_ = Invalid;
        }
        return *this;
    }

    Raw get() const { return value_; }
    bool valid() const { return value_ != Invalid; }

private:
    Raw value_ = Invalid;
};

bool SendAll(SOCKET socket, const char* data, int size)
{
    int sent = 0;
    while (sent < size)
    {
        const int rc = send(socket, data + sent, size - sent, 0);
        if (rc <= 0)
            return false;
        sent += rc;
    }
    return true;
}

bool RecvAll(SOCKET socket, char* data, int size)
{
    int received = 0;
    while (received < size)
    {
        const int rc = recv(socket, data + received, size - received, 0);
        if (rc <= 0)
            return false;
        received += rc;
    }
    return true;
}

class NetRttModule final : public IModule
{
public:
    std::string id() const override { return "net_rtt"; }
    std::string category() const override { return "network"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();

#if !defined(_WIN32)
        result.status = "not_supported";
        result.message = "Implemented for Windows in current version";
        return result;
#else
        SocketRuntime runtime;
        if (!runtime.ok())
        {
            result.status = "error";
            result.message = "WSAStartup failed";
            return result;
        }

        SocketHandle listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!listener.valid())
        {
            result.status = "error";
            result.message = "listener socket failed";
            return result;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            result.status = "error";
            result.message = "bind failed";
            return result;
        }
        if (listen(listener.get(), 1) != 0)
        {
            result.status = "error";
            result.message = "listen failed";
            return result;
        }

        int addr_len = sizeof(addr);
        if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0)
        {
            result.status = "error";
            result.message = "getsockname failed";
            return result;
        }
        const u_short port = ntohs(addr.sin_port);

        std::atomic<bool> server_ok{true};
        std::thread server([&]()
        {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);
            SocketHandle client(accept(listener.get(), reinterpret_cast<sockaddr*>(&client_addr), &client_len));
            if (!client.valid())
            {
                server_ok = false;
                return;
            }

            char byte = 0;
            for (int i = 0; i < 2000; ++i)
            {
                if (!RecvAll(client.get(), &byte, 1))
                {
                    server_ok = false;
                    return;
                }
                if (!SendAll(client.get(), &byte, 1))
                {
                    server_ok = false;
                    return;
                }
            }
        });

        SocketHandle client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!client.valid())
        {
            server_ok = false;
            server.join();
            result.status = "error";
            result.message = "client socket failed";
            return result;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(client.get(), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0)
        {
            server_ok = false;
            server.join();
            result.status = "error";
            result.message = "connect failed";
            return result;
        }

        char byte = 0x7f;
        const auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < 2000; ++i)
        {
            if (!SendAll(client.get(), &byte, 1) || !RecvAll(client.get(), &byte, 1))
            {
                server_ok = false;
                break;
            }
        }
        const auto end = std::chrono::high_resolution_clock::now();
        server.join();

        if (!server_ok)
        {
            result.status = "error";
            result.message = "loopback RTT exchange failed";
            return result;
        }

        const double total_ms = std::chrono::duration<double, std::milli>(end - start).count();
        const double avg_rtt_ms = total_ms / 2000.0;
        result.status = "ok";
        result.metrics["avg_rtt_ms"] = avg_rtt_ms;
        result.metrics["samples"] = 2000.0;
        result.score = ClampScore(100.0 - avg_rtt_ms * 150.0);
        result.message = "TCP loopback RTT";
        return result;
#endif
    }
};

class NetBandwidthModule final : public IModule
{
public:
    std::string id() const override { return "net_bw"; }
    std::string category() const override { return "network"; }

    ModuleResult run() override
    {
        ModuleResult result;
        result.id = id();
        result.category = category();

#if !defined(_WIN32)
        result.status = "not_supported";
        result.message = "Implemented for Windows in current version";
        return result;
#else
        SocketRuntime runtime;
        if (!runtime.ok())
        {
            result.status = "error";
            result.message = "WSAStartup failed";
            return result;
        }

        SocketHandle listener(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!listener.valid())
        {
            result.status = "error";
            result.message = "listener socket failed";
            return result;
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = 0;
        if (bind(listener.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            result.status = "error";
            result.message = "bind failed";
            return result;
        }
        if (listen(listener.get(), 1) != 0)
        {
            result.status = "error";
            result.message = "listen failed";
            return result;
        }

        int addr_len = sizeof(addr);
        if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr), &addr_len) != 0)
        {
            result.status = "error";
            result.message = "getsockname failed";
            return result;
        }
        const u_short port = ntohs(addr.sin_port);

        constexpr std::size_t total_bytes = 64ULL * 1024ULL * 1024ULL;
        constexpr int chunk = 64 * 1024;
        std::vector<char> buffer(chunk, 'b');

        std::atomic<bool> server_ok{true};
        std::thread server([&]()
        {
            sockaddr_in client_addr{};
            int client_len = sizeof(client_addr);
            SocketHandle client(accept(listener.get(), reinterpret_cast<sockaddr*>(&client_addr), &client_len));
            if (!client.valid())
            {
                server_ok = false;
                return;
            }

            std::size_t received_total = 0;
            while (received_total < total_bytes)
            {
                const int target = static_cast<int>(std::min<std::size_t>(chunk, total_bytes - received_total));
                if (!RecvAll(client.get(), buffer.data(), target))
                {
                    server_ok = false;
                    return;
                }
                received_total += static_cast<std::size_t>(target);
            }
        });

        SocketHandle client(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (!client.valid())
        {
            server_ok = false;
            server.join();
            result.status = "error";
            result.message = "client socket failed";
            return result;
        }

        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);
        server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(client.get(), reinterpret_cast<sockaddr*>(&server_addr), sizeof(server_addr)) != 0)
        {
            server_ok = false;
            server.join();
            result.status = "error";
            result.message = "connect failed";
            return result;
        }

        const auto start = std::chrono::high_resolution_clock::now();
        std::size_t sent_total = 0;
        while (sent_total < total_bytes)
        {
            const int target = static_cast<int>(std::min<std::size_t>(chunk, total_bytes - sent_total));
            if (!SendAll(client.get(), buffer.data(), target))
            {
                server_ok = false;
                break;
            }
            sent_total += static_cast<std::size_t>(target);
        }
        const auto end = std::chrono::high_resolution_clock::now();
        server.join();

        if (!server_ok)
        {
            result.status = "error";
            result.message = "loopback bandwidth exchange failed";
            return result;
        }

        const double elapsed_s = std::chrono::duration<double>(end - start).count();
        const double mib = static_cast<double>(total_bytes) / (1024.0 * 1024.0);
        const double mibps = mib / std::max(elapsed_s, 0.000001);
        result.status = "ok";
        result.metrics["mibps"] = mibps;
        result.metrics["elapsed_s"] = elapsed_s;
        result.score = ClampScore(mibps / 25.0);
        result.message = "TCP loopback bandwidth";
        return result;
#endif
    }
};

} // namespace

std::vector<ModulePtr> CreateBuiltinNetworkModules()
{
    std::vector<ModulePtr> modules;
    modules.emplace_back(std::make_shared<NetRttModule>());
    modules.emplace_back(std::make_shared<NetBandwidthModule>());
    return modules;
}

} // namespace ispcok
