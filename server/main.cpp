#include <lithium_http_server.hh>
#include "include/routes.hpp"
#include <csignal>
#include <atomic>
#include <iostream>

using namespace li;

// Global flag for graceful shutdown
std::atomic<bool> shutdown_requested{false};

void signal_handler(int signum) {
  std::cout << "\nShutdown signal received (signal " << signum << "). Exiting immediately..." << std::endl;
  shutdown_requested = true;

  // Force exit immediately - Lithium doesn't have clean shutdown API
  // This ensures the socket is released quickly
  _exit(0);
}

int main() {
  // Install signal handlers for graceful shutdown
  std::signal(SIGINT, signal_handler);   // Ctrl+C
  std::signal(SIGTERM, signal_handler);  // kill command

  std::cout << "Starting HTTP server on port 8080..." << std::endl;
  std::cout << "Press Ctrl+C to stop the server" << std::endl;

  auto api = make_routes();

  // Create server on port 8080
  http_serve(api, 8080);

  return 0;
}