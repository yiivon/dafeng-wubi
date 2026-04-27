#include "dafeng/client_c.h"

#include <chrono>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "dafeng/client.h"
#include "dafeng/paths.h"

extern "C" {

struct dafeng_client {
  dafeng::DafengClient cpp;
  explicit dafeng_client(std::string address) : cpp(std::move(address)) {}
};

dafeng_client_t* dafeng_client_create(const char* address) {
  try {
    std::string addr = (address != nullptr && address[0] != '\0')
                           ? std::string(address)
                           : dafeng::GetDaemonAddress();
    return new dafeng_client(std::move(addr));
  } catch (...) {
    return nullptr;
  }
}

void dafeng_client_destroy(dafeng_client_t* client) {
  delete client;
}

int dafeng_client_rerank(dafeng_client_t* client, const char* code,
                          const char* context_before,
                          const char* const* candidates,
                          size_t candidates_count, const char* app_id,
                          int timeout_ms, dafeng_rerank_result* out_result) {
  if (client == nullptr || out_result == nullptr) return 0;
  out_result->indices = nullptr;
  out_result->count = 0;
  out_result->latency_us = 0;
  out_result->model_version = 0;

  try {
    dafeng::RerankRequest req;
    if (code) req.code = code;
    if (context_before) req.context_before = context_before;
    if (app_id) req.app_id = app_id;
    req.candidates.reserve(candidates_count);
    for (size_t i = 0; i < candidates_count; ++i) {
      req.candidates.emplace_back(candidates[i] ? candidates[i] : "");
    }

    auto resp = client->cpp.Rerank(
        req, std::chrono::milliseconds(timeout_ms < 0 ? 0 : timeout_ms));
    if (!resp) return 0;

    const size_t n = resp->reordered_indices.size();
    if (n > 0) {
      auto* buf = static_cast<int32_t*>(std::malloc(sizeof(int32_t) * n));
      if (buf == nullptr) return 0;
      std::memcpy(buf, resp->reordered_indices.data(), sizeof(int32_t) * n);
      out_result->indices = buf;
    }
    out_result->count = n;
    out_result->latency_us = resp->latency_us;
    out_result->model_version = resp->model_version;
    return 1;
  } catch (...) {
    return 0;
  }
}

void dafeng_rerank_result_free(dafeng_rerank_result* result) {
  if (result == nullptr) return;
  std::free(result->indices);
  result->indices = nullptr;
  result->count = 0;
}

void dafeng_client_record_commit(dafeng_client_t* client, const char* code,
                                  const char* committed_text,
                                  const char* context_before) {
  if (client == nullptr) return;
  try {
    client->cpp.RecordCommit(code ? code : "",
                              committed_text ? committed_text : "",
                              context_before ? context_before : "");
  } catch (...) {
    // Fire-and-forget: swallow.
  }
}

int dafeng_client_is_connected(const dafeng_client_t* client) {
  if (client == nullptr) return 0;
  return client->cpp.IsConnected() ? 1 : 0;
}

void dafeng_client_reset_connection(dafeng_client_t* client) {
  if (client == nullptr) return;
  client->cpp.ResetConnection();
}

}  // extern "C"
