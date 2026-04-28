// Tests for the llama.cpp factory contract.
// We don't ship a model in the repo, so these are factory-level tests:
// the factory must return nullptr on bad input regardless of whether
// llama.cpp is built in. Real-model integration is exercised via the
// `dafeng-cli rerank` smoke path in docs/phase-3.2.md.

#include <gtest/gtest.h>

#include "../../src/daemon/services.h"

namespace dafeng {
namespace {

TEST(LlamaCppRerankerTest, FactoryReturnsNullWithEmptyPath) {
  MLXRerankConfig cfg;  // model_path == ""
  auto svc = MakeLlamaCppRerankService(cfg);
  EXPECT_EQ(svc, nullptr);
}

TEST(LlamaCppRerankerTest, FactoryReturnsNullWithMissingModel) {
  MLXRerankConfig cfg;
  cfg.model_path = "/tmp/dafeng-no-such-gguf-file.gguf";
  auto svc = MakeLlamaCppRerankService(cfg);
  EXPECT_EQ(svc, nullptr);
}

}  // namespace
}  // namespace dafeng
