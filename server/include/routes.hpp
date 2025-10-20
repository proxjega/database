#pragma once
#include <lithium_http_server.hh>
#include <string>
#include <optional>

using namespace li;

inline auto make_routes() {
  http_api api;

  // GET /greet?name=YourName
  api.get("/greet") = [](http_request& req, http_response& res) {
    // request parameters: try to read optional 'name'
    auto params = req.get_parameters(s::name = std::optional<std::string>());
    if (params.name) {
      res.write("Hello, " + *params.name + "!\n");
    } else {
      res.write("Hello, stranger!\n");
    }
  };

  // POST /echo  (raw body expected, reply in plaintext)
  api.post("/echo") = [](http_request& req, http_response& res) {
    std::string_view body_sv = req.http_ctx.read_whole_body();
    std::string body_str(body_sv);

    auto hdr = req.header("X-Custom-Header");
    std::string custom_header_value = hdr.empty() ? "No Header" : std::string(hdr);

    res.write("Received body: " + body_str + "\n");
    res.write("X-Custom-Header: " + custom_header_value + "\n");
  };

  return api;
}