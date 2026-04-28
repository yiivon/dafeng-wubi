// Build-time stub for the llama.cpp reranker. Active when
// DAFENG_ENABLE_LLAMA_CPP is OFF. Returns nullptr unconditionally so
// the daemon's BuildReranker falls back to a different backend.

#include "services.h"

namespace dafeng {

std::unique_ptr<IRerankService> MakeLlamaCppRerankService(
    const MLXRerankConfig& /*config*/) {
  return nullptr;
}

}  // namespace dafeng
