#include "include/routes.hpp"
#include <csignal>
#include <atomic>
#include <iostream>

using namespace li;

using std::cout;

#define PORT 8080

// Global flag for graceful shutdown
std::atomic<bool> shutdown_requested{false};

void signal_handler(int signum) {
  cout << "\nShutdown signal received (signal " << signum << "). Exiting immediately...\n";
  shutdown_requested = true;

  // Force exit immediately - Lithium doesn't have clean shutdown API
  // This ensures the socket is released quickly
  _exit(0);
}

int main() {
  // Install signal handlers for graceful shutdown
  std::signal(SIGINT, signal_handler);   // Ctrl+C
  std::signal(SIGTERM, signal_handler);  // kill command

  cout << "Starting HTTP server on port " << PORT << "...\n";
  cout << "Press Ctrl+C to stop the server\n";

  auto api = make_routes();

  // Create server on port PORT
  http_serve(api, PORT);

  return 0;
}