// Build-time stub that returns nullptr from MakeMLXRerankService — used
// when DAFENG_ENABLE_MLX is OFF or we're not on Apple Silicon. The real
// implementation lives in reranker_mlx.cc and is conditionally compiled.

#include "services.h"

namespace dafeng {

std::unique_ptr<IRerankService> MakeMLXRerankService(
    const MLXRerankConfig& /*config*/) {
  return nullptr;
}

}  // namespace dafeng
