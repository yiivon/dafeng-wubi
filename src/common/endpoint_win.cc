// Windows Named Pipe backend. Phase 2.1 ships a build-only stub so the source
// tree stays cross-platform-clean; full implementation lands with the Windows
// port. The functions here unconditionally fail — Mac builds never see this
// translation unit, and Windows builds will hit the LOG_ERROR before falling
// back to the IME-only path.

#include "dafeng/endpoint.h"

#include "dafeng/logging.h"

namespace dafeng {

std::unique_ptr<EndpointServer> ListenEndpoint(const std::string& /*address*/) {
  DAFENG_LOG_ERROR("Windows endpoint not implemented yet (Phase 2.1 stub)");
  return nullptr;
}

std::unique_ptr<Connection> ConnectEndpoint(const std::string& /*address*/) {
  DAFENG_LOG_ERROR("Windows endpoint not implemented yet (Phase 2.1 stub)");
  return nullptr;
}

}  // namespace dafeng
