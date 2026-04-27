#ifndef DAFENG_CLIENT_C_H
#define DAFENG_CLIENT_C_H

// C ABI for embedding DafengClient into runtimes that can't link C++ —
// most importantly the librime Lua bridge. Strings are NUL-terminated UTF-8.
//
// Memory rules:
//   - All `const char*` parameters are borrowed; the caller owns them.
//   - dafeng_rerank_result holds an int32 array allocated by this library
//     and must be released via dafeng_rerank_result_free.
//   - dafeng_client_t is opaque and must be released via dafeng_client_destroy.

#include <stddef.h>
#include <stdint.h>

#include "dafeng/api.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct dafeng_client dafeng_client_t;

typedef struct {
  int32_t* indices;  // owned; freed via dafeng_rerank_result_free
  size_t count;
  uint32_t latency_us;
  uint8_t model_version;
} dafeng_rerank_result;

// Construct a client bound to `address`. Pass NULL for the platform default
// (GetDaemonAddress). Returns NULL on allocation failure.
DAFENG_API dafeng_client_t* dafeng_client_create(const char* address);

DAFENG_API void dafeng_client_destroy(dafeng_client_t* client);

// Returns 1 on success (out_result populated), 0 on timeout / failure.
// On success, caller must free out_result->indices via dafeng_rerank_result_free.
DAFENG_API int dafeng_client_rerank(dafeng_client_t* client,
                                    const char* code,
                                    const char* context_before,
                                    const char* const* candidates,
                                    size_t candidates_count,
                                    const char* app_id,
                                    int timeout_ms,
                                    dafeng_rerank_result* out_result);

DAFENG_API void dafeng_rerank_result_free(dafeng_rerank_result* result);

DAFENG_API void dafeng_client_record_commit(dafeng_client_t* client,
                                            const char* code,
                                            const char* committed_text,
                                            const char* context_before);

DAFENG_API int dafeng_client_is_connected(const dafeng_client_t* client);

DAFENG_API void dafeng_client_reset_connection(dafeng_client_t* client);

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // DAFENG_CLIENT_C_H
