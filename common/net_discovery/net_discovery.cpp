#include "net_discovery.hpp"

#include <spdlog/spdlog.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <mdns.h>
#include <mutex>
#include <stdexcept>
#include <string_view>
#include <thread>

#ifdef _WIN32
#include <WinSock2.h>
#include <Ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace net_discovery
{
namespace
{

constexpr std::size_t K_BUFFER_SIZE = 2048;
constexpr std::string_view K_SERVICE_TYPE = "_hello-server._tcp.local.";
constexpr std::string_view K_INSTANCE_PREFIX = "hello_server-";
constexpr std::string_view K_MACHINE_ID_TXT = "machine_id";
constexpr auto K_MACHINE_ID_FILE = "/etc/machine-id";

struct socket_state {
    int fd = -1;
    sockaddr_in address{};
};

struct hello_server_records {
    std::string service_type;
    std::string instance_name;
    std::string host_name;
    std::string machine_id;
    mdns_record_t ptr{};
    mdns_record_t srv{};
    mdns_record_t txt{};
    mdns_record_t a{};
    std::array<mdns_record_t, 3> additional{};
};

struct discovery_state {
    std::vector<endpoint> endpoints;
};

auto ensure_dot(std::string value) -> std::string
{
    if (!value.empty() && value.back() != '.') {
        value.push_back('.');
    }
    return value;
}

auto trim(std::string value) -> std::string
{
    while (!value.empty() && (value.back() == '\n' || value.back() == '\r' || value.back() == ' ')
    ) {
        value.pop_back();
    }
    return value;
}

auto host_name() -> std::string
{
    std::array<char, 256> buffer{};
#ifdef _WIN32
    if (gethostname(buffer.data(), static_cast<int>(buffer.size())) != 0) {
#else
    if (gethostname(buffer.data(), buffer.size()) != 0) {
#endif
        return "hello-server";
    }
    return buffer.data();
}

auto instance_name(const std::string &machine_id) -> std::string
{
    return std::string(K_INSTANCE_PREFIX) + machine_id + "." + std::string(K_SERVICE_TYPE);
}

auto sockaddr_to_ip(const sockaddr_in &addr) -> std::string
{
    std::array<char, INET_ADDRSTRLEN> text{};
    if (inet_ntop(AF_INET, &addr.sin_addr, text.data(), text.size()) == nullptr) {
        return {};
    }
    return text.data();
}

auto same_dns_name(std::string lhs, std::string rhs) -> bool
{
    while (!lhs.empty() && lhs.back() == '.') {
        lhs.pop_back();
    }
    while (!rhs.empty() && rhs.back() == '.') {
        rhs.pop_back();
    }
    return lhs == rhs;
}

auto interface_allowed(const std::string &name, const std::vector<std::string> &interfaces) -> bool
{
    return interfaces.empty() ||
           std::find(interfaces.begin(), interfaces.end(), name) != interfaces.end();
}

auto format_interfaces(const std::vector<std::string> &interfaces) -> std::string
{
    if (interfaces.empty()) {
        return "<auto>";
    }
    std::string value;
    for (const auto &name : interfaces) {
        if (!value.empty()) {
            value += ",";
        }
        value += name;
    }
    return value;
}

auto ensure_socket_runtime() -> bool
{
#ifdef _WIN32
    static const bool initialized = [] {
        WSADATA data{};
        const int result = WSAStartup(MAKEWORD(2, 2), &data);
        if (result != 0) {
            spdlog::error("mDNS failed to initialize Winsock result={}", result);
            return false;
        }
        static const bool cleanup_registered = std::atexit([] {
                                                 WSACleanup();
                                             }) == 0;
        (void)cleanup_registered;
        spdlog::info("mDNS initialized Winsock version={}.{}", LOBYTE(data.wVersion), HIBYTE(data.wVersion));
        return true;
    }();
    return initialized;
#else
    return true;
#endif
}

auto socket_error_code() -> int
{
#ifdef _WIN32
    return WSAGetLastError();
#else
    return errno;
#endif
}

auto interface_sockets(const std::vector<std::string> &interfaces, std::uint16_t port)
        -> std::vector<socket_state>
{
    std::vector<socket_state> sockets;
#ifdef _WIN32
    (void)interfaces;
    spdlog::info("mDNS using Windows default interface selection, port={}", port);
    socket_state socket;
    socket.address.sin_family = AF_INET;
    socket.address.sin_addr.s_addr = INADDR_ANY;
    socket.address.sin_port = htons(port);
    sockets.push_back(socket);
#else
    ifaddrs *ifaddr = nullptr;
    if (getifaddrs(&ifaddr) != 0) {
        spdlog::warn("mDNS failed to enumerate network interfaces");
        return sockets;
    }

    for (auto *ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == nullptr || ifa->ifa_addr->sa_family != AF_INET) {
            continue;
        }
        const auto flags = ifa->ifa_flags;
        if ((flags & IFF_UP) == 0 || (flags & IFF_MULTICAST) == 0 || (flags & IFF_LOOPBACK) != 0) {
            continue;
        }
        if (!interface_allowed(ifa->ifa_name, interfaces)) {
            continue;
        }

        socket_state socket;
        std::memcpy(&socket.address, ifa->ifa_addr, sizeof(socket.address));
        socket.address.sin_port = htons(port);
        spdlog::info(
                "mDNS selected interface name={} address={} port={}",
                ifa->ifa_name,
                sockaddr_to_ip(socket.address),
                port
        );
        sockets.push_back(socket);
    }

    freeifaddrs(ifaddr);
#endif
    return sockets;
}

void close_socket(int fd)
{
    if (fd >= 0) {
        mdns_socket_close(fd);
    }
}

auto open_discovery_socket() -> int
{
    if (!ensure_socket_runtime()) {
        return -1;
    }
    const int sock = mdns_socket_open_ipv4(nullptr);
    if (sock < 0) {
        spdlog::warn("mDNS discovery failed to open query socket error={}", socket_error_code());
    }
    return sock;
}

void send_discovery_query(int sock, std::array<std::byte, K_BUFFER_SIZE> &buffer)
{
    (void)mdns_query_send(
            sock,
            MDNS_RECORDTYPE_PTR,
            K_SERVICE_TYPE.data(),
            K_SERVICE_TYPE.size(),
            buffer.data(),
            buffer.size(),
            0
    );
    spdlog::info("mDNS discovery query sent service={} socket={}", K_SERVICE_TYPE, sock);
}

auto make_records(const hello_server_config &config, const sockaddr_in &address)
        -> hello_server_records
{
    hello_server_records records;
    records.service_type = std::string(K_SERVICE_TYPE);
    records.machine_id = config.machine_id.empty() ? local_machine_id() : config.machine_id;
    records.instance_name = instance_name(records.machine_id);
    records.host_name = ensure_dot(host_name() + ".local");

    // DNS-SD 的最小描述：PTR(service -> instance)，SRV(instance -> host:port)，
    // TXT(instance -> machine_id)，A(host -> IPv4)。
    records.ptr.name = { records.service_type.c_str(), records.service_type.size() };
    records.ptr.type = MDNS_RECORDTYPE_PTR;

    records.srv.name = { records.instance_name.c_str(), records.instance_name.size() };
    records.srv.type = MDNS_RECORDTYPE_SRV;

    records.txt.name = { records.instance_name.c_str(), records.instance_name.size() };
    records.txt.type = MDNS_RECORDTYPE_TXT;

    records.a.name = { records.host_name.c_str(), records.host_name.size() };
    records.a.type = MDNS_RECORDTYPE_A;

    // NOLINTBEGIN(cppcoreguidelines-pro-type-union-access)
    records.ptr.data.ptr.name = { records.instance_name.c_str(), records.instance_name.size() };
    records.srv.data.srv.port = config.port;
    records.srv.data.srv.name = { records.host_name.c_str(), records.host_name.size() };
    records.txt.data.txt.key = { K_MACHINE_ID_TXT.data(), K_MACHINE_ID_TXT.size() };
    records.txt.data.txt.value = { records.machine_id.c_str(), records.machine_id.size() };
    records.a.data.a.addr = address;
    // NOLINTEND(cppcoreguidelines-pro-type-union-access)

    records.additional = { records.srv, records.txt, records.a };
    return records;
}

auto extract_name(
        const void *data,
        std::size_t size,
        std::size_t name_offset,
        std::size_t name_length
) -> std::string
{
    std::array<char, 1024> buffer{};
    std::size_t offset = name_offset;
    const auto name = mdns_string_extract(data, size, &offset, buffer.data(), buffer.size());
    if (name.length == 0 && name_length != 0) {
        return {};
    }
    return { name.str, name.length };
}

auto machine_id_from_txt(const void *data, std::size_t size, std::size_t offset, std::size_t length)
        -> std::string
{
    std::array<mdns_record_txt_t, 8> txt{};
    const auto count = mdns_record_parse_txt(data, size, offset, length, txt.data(), txt.size());
    for (std::size_t i = 0; i < count; ++i) {
        const auto &item = txt.at(i);
        const std::string key(item.key.str, item.key.length);
        if (key == K_MACHINE_ID_TXT) {
            return { item.value.str, item.value.length };
        }
    }
    return {};
}

auto endpoint_for(discovery_state &state, const std::string &instance) -> endpoint &
{
    const auto found =
            std::find_if(state.endpoints.begin(), state.endpoints.end(), [&](const endpoint &item) {
                return same_dns_name(item.instance_name, instance);
            });
    if (found != state.endpoints.end()) {
        return *found;
    }
    auto &created = state.endpoints.emplace_back();
    created.instance_name = instance;
    return created;
}

auto discovery_socket_ready(int sock) -> bool
{
    fd_set set;
    FD_ZERO(&set);
    FD_SET(sock, &set);

    timeval wait{ .tv_sec = 0, .tv_usec = 100000 };
    return select(sock + 1, &set, nullptr, nullptr, &wait) > 0 && FD_ISSET(sock, &set);
}

struct responder_context {
    hello_server_config config;
    socket_state *socket = nullptr;
};

auto query_answer_for(
        const std::string &question,
        std::uint16_t rtype,
        const hello_server_records &records
) -> std::optional<mdns_record_t>
{
    if (same_dns_name(question, records.service_type)) {
        return records.ptr;
    }
    if (same_dns_name(question, records.instance_name)) {
        return rtype == MDNS_RECORDTYPE_SRV ? records.srv : records.txt;
    }
    if (same_dns_name(question, records.host_name)) {
        return records.a;
    }
    return std::nullopt;
}

auto responder_callback(
        int sock,
        const sockaddr *from,
        std::size_t addrlen,
        mdns_entry_type_t entry,
        std::uint16_t query_id,
        std::uint16_t rtype,
        std::uint16_t rclass,
        std::uint32_t /*ttl*/,
        const void *data,
        std::size_t size,
        std::size_t name_offset,
        std::size_t name_length,
        std::size_t /*record_offset*/,
        std::size_t /*record_length*/,
        void *user_data
) -> int
{
    if (entry != MDNS_ENTRYTYPE_QUESTION) {
        return 0;
    }

    auto *context = static_cast<responder_context *>(user_data);
    const auto question = extract_name(data, size, name_offset, name_length);
    const auto records = make_records(context->config, context->socket->address);
    const auto answer = query_answer_for(question, rtype, records);
    if (!answer) {
        spdlog::debug("mDNS ignored query name={} type={}", question, rtype);
        return 0;
    }

    spdlog::info(
            "mDNS answering query name={} type={} machine_id={} endpoint={}:{}",
            question,
            rtype,
            records.machine_id,
            sockaddr_to_ip(context->socket->address),
            context->config.port
    );

    std::array<std::byte, K_BUFFER_SIZE> buffer{};
    if ((rclass & MDNS_UNICAST_RESPONSE) != 0) {
        (void)mdns_query_answer_unicast(
                sock,
                from,
                addrlen,
                buffer.data(),
                buffer.size(),
                query_id,
                static_cast<mdns_record_type_t>(rtype),
                question.c_str(),
                question.size(),
                *answer,
                nullptr,
                0,
                records.additional.data(),
                records.additional.size()
        );
    } else {
        (void)mdns_query_answer_multicast(
                sock,
                buffer.data(),
                buffer.size(),
                *answer,
                nullptr,
                0,
                records.additional.data(),
                records.additional.size()
        );
    }
    return 0;
}

auto discovery_callback(
        int /*sock*/,
        const sockaddr *from,
        std::size_t /*addrlen*/,
        mdns_entry_type_t entry,
        std::uint16_t /*query_id*/,
        std::uint16_t rtype,
        std::uint16_t /*rclass*/,
        std::uint32_t /*ttl*/,
        const void *data,
        std::size_t size,
        std::size_t name_offset,
        std::size_t name_length,
        std::size_t record_offset,
        std::size_t record_length,
        void *user_data
) -> int
{
    if (entry == MDNS_ENTRYTYPE_QUESTION) {
        return 0;
    }

    auto *state = static_cast<discovery_state *>(user_data);
    const auto record_name = extract_name(data, size, name_offset, name_length);
    std::array<char, 1024> name_buffer{};

    if (rtype == MDNS_RECORDTYPE_PTR) {
        const auto ptr = mdns_record_parse_ptr(
                data, size, record_offset, record_length, name_buffer.data(), name_buffer.size()
        );
        const std::string instance(ptr.str, ptr.length);
        if (instance.starts_with(K_INSTANCE_PREFIX)) {
            spdlog::info("mDNS discovered PTR instance={}", instance);
            endpoint_for(*state, instance);
        }
    } else if (rtype == MDNS_RECORDTYPE_SRV) {
        auto &item = endpoint_for(*state, record_name);
        const auto srv = mdns_record_parse_srv(
                data, size, record_offset, record_length, name_buffer.data(), name_buffer.size()
        );
        item.port = srv.port;
        spdlog::info("mDNS discovered SRV instance={} port={}", record_name, item.port);
    } else if (rtype == MDNS_RECORDTYPE_TXT) {
        auto &item = endpoint_for(*state, record_name);
        item.machine_id = machine_id_from_txt(data, size, record_offset, record_length);
        spdlog::info("mDNS discovered TXT instance={} machine_id={}", record_name, item.machine_id);
    } else if (rtype == MDNS_RECORDTYPE_A) {
        sockaddr_in addr{};
        (void)mdns_record_parse_a(data, size, record_offset, record_length, &addr);
        const auto address = sockaddr_to_ip(addr);
        spdlog::info("mDNS discovered A name={} address={}", record_name, address);
        for (auto &item : state->endpoints) {
            if (item.address.empty()) {
                item.address = address;
            }
        }
        if (state->endpoints.empty() && from != nullptr && from->sa_family == AF_INET) {
            sockaddr_in from_addr{};
            std::memcpy(&from_addr, from, sizeof(from_addr));
            state->endpoints.push_back({ .address = sockaddr_to_ip(from_addr) });
        }
    }

    return 0;
}

void receive_discovery_answers(
        int sock,
        std::array<std::byte, K_BUFFER_SIZE> &buffer,
        discovery_state &state
)
{
    if (!discovery_socket_ready(sock)) {
        return;
    }
    (void)mdns_query_recv(sock, buffer.data(), buffer.size(), &discovery_callback, &state, 0);
}

void remove_incomplete_endpoints(discovery_state &state)
{
    std::erase_if(state.endpoints, [](const endpoint &item) {
        return item.machine_id.empty() || item.ip_port().empty();
    });
}

void log_discovered_endpoints(const std::vector<endpoint> &endpoints)
{
    if (endpoints.empty()) {
        spdlog::warn("mDNS discovery finished with no hello_server endpoints");
        return;
    }

    spdlog::info("mDNS discovery finished endpoint_count={}", endpoints.size());
    for (const auto &item : endpoints) {
        spdlog::info(
                "mDNS endpoint machine_id={} ip_port={} instance={}",
                item.machine_id,
                item.ip_port(),
                item.instance_name
        );
    }
}

} // namespace

auto endpoint::ip_port() const -> std::string
{
    if (address.empty() || port == 0) {
        return {};
    }
    return address + ":" + std::to_string(port);
}

auto local_machine_id() -> std::string
{
#ifdef _WIN32
    return host_name();
#else
    std::ifstream file{ K_MACHINE_ID_FILE };
    std::string id;
    std::getline(file, id);
    id = trim(std::move(id));
    if (!id.empty()) {
        return id;
    }
    return host_name();
#endif
}

class hello_server_announcer::impl
{
public:
    explicit impl(hello_server_config config) : config_(std::move(config))
    {
        if (config_.machine_id.empty()) {
            config_.machine_id = local_machine_id();
        }
        spdlog::info(
                "mDNS announcer configured service={} machine_id={} port={} interfaces={}",
                K_SERVICE_TYPE,
                config_.machine_id,
                config_.port,
                format_interfaces(config_.interfaces)
        );
    }

    ~impl()
    {
        stop();
    }

    impl(const impl &) = delete;
    auto operator=(const impl &) -> impl & = delete;
    impl(impl &&) = delete;
    auto operator=(impl &&) -> impl & = delete;

    void start()
    {
        if (running_.exchange(true)) {
            return;
        }

        if (!ensure_socket_runtime()) {
            running_ = false;
            throw std::runtime_error("failed to initialize socket runtime for mDNS");
        }

        sockets_ = interface_sockets(config_.interfaces, MDNS_PORT);
        if (sockets_.empty()) {
            running_ = false;
            throw std::runtime_error("no multicast-capable IPv4 interface available for mDNS");
        }

        for (auto &socket : sockets_) {
            socket.fd = mdns_socket_open_ipv4(&socket.address);
            if (socket.fd < 0) {
                running_ = false;
                throw std::runtime_error(
                        "failed to open mDNS socket, error=" +
                        std::to_string(socket_error_code())
                );
            }
            spdlog::info(
                    "mDNS socket opened fd={} bind_address={} mdns_port={}",
                    socket.fd,
                    sockaddr_to_ip(socket.address),
                    MDNS_PORT
            );
        }

        announce(&mdns_announce_multicast);
        thread_ = std::thread([this]() {
            run();
        });
    }

    void stop()
    {
        if (!running_.exchange(false)) {
            return;
        }

        announce(&mdns_goodbye_multicast);
        if (thread_.joinable()) {
            thread_.join();
        }
        for (auto &socket : sockets_) {
            close_socket(socket.fd);
            socket.fd = -1;
        }
    }

private:
    using announce_fn =
            int (*)(int,
                    void *,
                    std::size_t,
                    mdns_record_t,
                    const mdns_record_t *,
                    std::size_t,
                    const mdns_record_t *,
                    std::size_t);

    void announce(announce_fn send)
    {
        std::array<std::byte, K_BUFFER_SIZE> buffer{};
        std::lock_guard lock(mutex_);
        for (auto &socket : sockets_) {
            const auto records = make_records(config_, socket.address);
            spdlog::info(
                    "mDNS announce service={} instance={} machine_id={} host={} endpoint={}:{}",
                    records.service_type,
                    records.instance_name,
                    records.machine_id,
                    records.host_name,
                    sockaddr_to_ip(socket.address),
                    config_.port
            );
            (void
            )send(socket.fd,
                  buffer.data(),
                  buffer.size(),
                  records.ptr,
                  nullptr,
                  0,
                  records.additional.data(),
                  records.additional.size());
        }
    }

    auto read_set(fd_set &set) -> int
    {
        FD_ZERO(&set);
        int max_fd = -1;
        std::lock_guard lock(mutex_);
        for (const auto &socket : sockets_) {
            if (socket.fd >= 0) {
                FD_SET(socket.fd, &set);
                max_fd = std::max(max_fd, socket.fd);
            }
        }
        return max_fd;
    }

    void answer_queries(fd_set &set)
    {
        std::array<std::byte, K_BUFFER_SIZE> buffer{};
        std::lock_guard lock(mutex_);
        for (auto &socket : sockets_) {
            if (socket.fd >= 0 && FD_ISSET(socket.fd, &set)) {
                responder_context context{ .config = config_, .socket = &socket };
                (void)mdns_socket_listen(
                        socket.fd, buffer.data(), buffer.size(), &responder_callback, &context
                );
            }
        }
    }

    void run()
    {
        auto next_announce = std::chrono::steady_clock::now() + config_.announce_interval;
        while (running_) {
            fd_set set;
            const auto max_fd = read_set(set);
            timeval timeout{ .tv_sec = 0, .tv_usec = 200000 };
            const auto ready = max_fd >= 0 ? select(max_fd + 1, &set, nullptr, nullptr, &timeout)
                                           : select(0, nullptr, nullptr, nullptr, &timeout);
            if (ready > 0) {
                answer_queries(set);
            }
            if (std::chrono::steady_clock::now() >= next_announce) {
                announce(&mdns_announce_multicast);
                next_announce = std::chrono::steady_clock::now() + config_.announce_interval;
            }
        }
    }

    hello_server_config config_;
    std::atomic_bool running_{ false };
    std::mutex mutex_;
    std::vector<socket_state> sockets_;
    std::thread thread_;
};

hello_server_announcer::hello_server_announcer(hello_server_config config)
    : impl_(std::make_unique<impl>(std::move(config)))
{
}

hello_server_announcer::~hello_server_announcer() = default;
hello_server_announcer::hello_server_announcer(hello_server_announcer &&) noexcept = default;
auto hello_server_announcer::operator=(hello_server_announcer &&) noexcept
        -> hello_server_announcer & = default;

void hello_server_announcer::start()
{
    impl_->start();
}

void hello_server_announcer::stop()
{
    impl_->stop();
}

auto discover_hello_servers(std::chrono::milliseconds timeout) -> std::vector<endpoint>
{
    spdlog::info(
            "mDNS discovery started service={} timeout_ms={}", K_SERVICE_TYPE, timeout.count()
    );
    const int sock = open_discovery_socket();
    if (sock < 0) {
        return {};
    }

    std::array<std::byte, K_BUFFER_SIZE> buffer{};
    send_discovery_query(sock, buffer);

    discovery_state state;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        receive_discovery_answers(sock, buffer, state);
    }
    close_socket(sock);

    remove_incomplete_endpoints(state);
    log_discovered_endpoints(state.endpoints);
    return state.endpoints;
}

auto discover_hello_server(const std::string &machine_id, std::chrono::milliseconds timeout)
        -> std::optional<endpoint>
{
    auto endpoints = discover_hello_servers(timeout);
    const auto found = std::find_if(endpoints.begin(), endpoints.end(), [&](const endpoint &item) {
        return item.machine_id == machine_id;
    });
    if (found == endpoints.end()) {
        return std::nullopt;
    }
    return *found;
}

} // namespace net_discovery
