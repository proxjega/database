#include <lithium_http_server.hh>
#include "include/routes.hpp"

using namespace li;

int main() {
  auto api = make_routes();

  // Create server on port 8080
  http_serve(api, 8080);

  return 0;
}