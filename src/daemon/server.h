#pragma once

#include <memory>

#include "dafeng/endpoint.h"
#include "services.h"

namespace dafeng {

class Server {
 public:
  Server(std::unique_ptr<EndpointServer> endpoint,
         std::unique_ptr<IRerankService> reranker,
         std::unique_ptr<ICommitLogger> logger);
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  // Block in the accept loop until Shutdown() is called or the endpoint
  // returns an error. Spawns a detached worker thread per accepted client.
  void Run();

  // Async-signal-safe — delegates to EndpointServer::Shutdown which only
  // writes one byte to a self-pipe.
  void Shutdown();

 private:
  void HandleConnection(std::unique_ptr<Connection> conn);

  std::unique_ptr<EndpointServer> endpoint_;
  std::unique_ptr<IRerankService> reranker_;
  std::unique_ptr<ICommitLogger> logger_;
};

}  // namespace dafeng
