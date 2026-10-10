#include <catch2/catch_test_macros.hpp>
#include "agent/model_client.hpp"
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <thread>
#include <stdexcept>

namespace {
// One-response loopback server. Every test owns its port and worker lifetime.
class LocalResponse {
  public:
    explicit LocalResponse(std::string payload) {
        listener_ = socket(AF_INET, SOCK_STREAM, 0);
        if (listener_ < 0)
            throw std::runtime_error("Cannot create local test socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener_, 1) != 0) {
            close(listener_);
            throw std::runtime_error("Cannot bind local test socket");
        }
        socklen_t length = sizeof(address);
        if (getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length) != 0) {
            close(listener_);
            throw std::runtime_error("Cannot resolve local test port");
        }
        endpoint = "http://127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        worker_ = std::jthread([this, payload = std::move(payload)] {
            const int client = accept(listener_, nullptr, nullptr);
            if (client < 0)
                return;
            timeval timeout{2, 0};
            setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
            std::string request;
            char buffer[4096];
            // Consume headers and body before responding to avoid resetting unread data.
            while (request.size() < 65536) {
                const auto count = recv(client, buffer, sizeof(buffer), 0);
                if (count <= 0)
                    break;
                request.append(buffer, static_cast<size_t>(count));
                const auto header_end = request.find("\r\n\r\n");
                const auto content_length = request.find("Content-Length:");
                if (header_end != std::string::npos && content_length != std::string::npos &&
                    request.size() >=
                        header_end + 4 + std::stoul(request.substr(content_length + 15)))
                    break;
            }
            std::string response =
                "HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                std::to_string(payload.size()) + "\r\nConnection: close\r\n\r\n" + payload;
            size_t sent = 0;
            while (sent < response.size()) {
                const auto count =
                    send(client, response.data() + sent, response.size() - sent, MSG_NOSIGNAL);
                if (count <= 0)
                    break;
                sent += static_cast<size_t>(count);
            }
            close(client);
        });
    }
    ~LocalResponse() {
        shutdown(listener_, SHUT_RDWR);
        worker_.join();
        close(listener_);
    }
    std::string endpoint;

  private:
    int listener_ = -1;
    std::jthread worker_;
};
} // namespace

TEST_CASE("Model HTTP: oversized bodies are bounded and deltas can cancel", "[model][http]") {
    for (auto factory : {agent::make_openai_client, agent::make_anthropic_client}) {
        auto client = factory();
        agent::ModelRequest request;
        request.model = "local";
        request.api_key = "fake-local-key";
        request.timeout_seconds = 3;
        {
            LocalResponse server(std::string(10000, 'x'));
            request.endpoint = server.endpoint;
            request.max_response_bytes = 32;
            const auto response = client->complete(request, {});
            CHECK(response.finish_reason == agent::FinishReason::Error);
            CHECK(response.error_message.find("curl error") != std::string::npos);
        }
        {
            const auto payload = factory == agent::make_openai_client
                                     ? R"({"choices":[{"message":{"content":"hello"}}]})"
                                     : R"({"content":[{"type":"text","text":"hello"}]})";
            LocalResponse server(payload);
            request.endpoint = server.endpoint;
            request.max_response_bytes = 1024;
            bool delivered = false;
            const auto response = client->complete(request, {}, [&](std::string_view text) {
                delivered = true;
                CHECK(text == "hello");
                return false;
            });
            CHECK(delivered);
            CHECK(response.finish_reason == agent::FinishReason::Cancelled);
        }
    }
}
