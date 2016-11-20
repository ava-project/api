#pragma once

#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#include <atomic>
#include <cstring>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace netlib {

namespace tools {

#define __LOGIC_ERROR__(error) throw std::logic_error(error);
#define __RUNTIME_ERROR__(error) throw std::runtime_error(error);
#define __INVALID_ARG__(error) throw std::invalid_argument(error);
#define __DISPLAY_ERROR__(error) std::cerr << error << std::endl;

static unsigned int const BACKLOG = 30;
static unsigned int const BUFFER_SIZE = 8096;

}  //! namespace tools

namespace network {

using namespace tools;

namespace tcp {

// socket
class socket {
 public:
  //! ctor
  socket(void)
      : fd_(-1),
        host_("127.0.0.1"),
        port_(12345),
        addrinfo_({0}),
        v_addrinfo_({0}),
        is_socket_bound_(false) {}

  //! creates socket from existing fd
  socket(int fd, const std::string &host, unsigned int port)
      : fd_(fd),
        host_(host),
        port_(port),
        addrinfo_({0}),
        v_addrinfo_({0}),
        is_socket_bound_(false) {}

  //! copy ctor
  socket(const socket &) = default;

  //! assignment operator
  socket &operator=(const socket &) = default;

  //! dtor
  ~socket(void) { close(); }

 public:
  //!
  //! server operations
  //!

  //! assigning a name to the socket
  void bind(const std::string &host, unsigned int port) {
    int yes = 1;
    host_ = host;
    port_ = port;
    get_addr_info();
    create_socket();

    if (::setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1)
      __RUNTIME_ERROR__("tcp::socket::bind: setsockopt() failed.");

    if (::bind(fd_, v_addrinfo_.ai_addr, v_addrinfo_.ai_addrlen) == -1)
      __RUNTIME_ERROR__("tcp::socket::bind: bind() failed.");
    is_socket_bound_ = true;
  }

  //! marks the socket as passive socket
  void listen(unsigned int backlog) {
    if (not is_socket_bound_)
      __LOGIC_ERROR__(
          "tcp::socket::listen: Socket must be bound before listenning for "
          "incoming connections.");

    if (backlog > SOMAXCONN)
      __DISPLAY_ERROR__(
          "tcp::socket::listen: Param backlog greater than "
          "SOMAXCONN.\nPlease "
          "refer to the value in /proc/sys/net/core/somaxconn. Param backlog "
          "will be truncated.");

    if (::listen(fd_, backlog) == -1)
      __RUNTIME_ERROR__("tcp::socket::listen: listen() failed.");
  }

  //! accepts an incoming connection
  tcp::socket accept(void) {
    socklen_t size;
    char host[NI_MAXHOST];
    char port[NI_MAXSERV];
    struct sockaddr_storage client;

    size = sizeof(client);
    int new_fd = ::accept(fd_, (struct sockaddr *)&client, &size);

    if (new_fd == -1)
      __RUNTIME_ERROR__("tcp::socket::accpet: accept() failed.");

    int res = getnameinfo((struct sockaddr *)&client, size, host, sizeof(host),
                          port, sizeof(port), NI_NUMERICHOST | NI_NUMERICSERV);

    if (res != 0)
      __RUNTIME_ERROR__("tcp::socket::accept: getnameinfo() failed.");

    return {new_fd, std::string(host), (unsigned int)std::stoi(port)};
  }

  //!
  //! client operations
  //!

  //! connect to a remote host
  void connect(const std::string &host, unsigned int port) {
    if (is_socket_bound_)
      __LOGIC_ERROR__(
          "tcp::socket::connect: Trying to connect a socket bound on port: " +
          std::to_string(port_) +
          ". Invalid operation for a socket planned for a server application.");

    host_ = host;
    port_ = port;
    get_addr_info();
    create_socket();

    if (::connect(fd_, v_addrinfo_.ai_addr, v_addrinfo_.ai_addrlen) == -1)
      __RUNTIME_ERROR__("tcp::socket::connect: connect() failed.");
  }

  //! send amount of data
  std::size_t send(const std::string &message) {
    return send(std::vector<char>(message.begin(), message.end()),
                message.size());
  }

  //! send amount of data
  std::size_t send(const std::vector<char> &message, std::size_t message_len) {
    if (fd_ == -1)
      __LOGIC_ERROR__(
          "tcp::socket::send: Invalid operation. Trying to send data on a non "
          "connected socket.");

    std::size_t res = ::send(fd_, message.data(), message_len, 0);

    if (res == -1) __RUNTIME_ERROR__("tcp::socket::send: send() failed.");
    return res;
  }

  //! receive amount of data
  std::vector<char> receive(std::size_t size_to_read) {
    if (fd_ == -1)
      __LOGIC_ERROR__(
          "tcp::socket::send: Invalid operation. Trying to receive data on a "
          "non connected socket.");

    std::vector<char> buffer(size_to_read, 0);

    std::size_t bytes_read =
        ::recv(fd_, const_cast<char *>(buffer.data()), size_to_read, 0);

    switch (bytes_read) {
      case -1:
        __RUNTIME_ERROR__("tcp::socket::receive: recv() failed.");
        break;
      case 0:
        std::cout << "Connection closed by peer.\n";
        close();
        break;
      default:
        break;
    }

    return buffer;
  }

  //!
  //! common operation
  //!

  //! close filedescriptor associated to the socket
  void close(void) {
    if (fd_ != -1) {
      if (::close(fd_) == -1)
        __RUNTIME_ERROR__("tcp::socket::close: close() failed.");
    }
    fd_ = -1;
  }

 public:
  //! get filedescriptor associated to the socket
  int get_fd() const { return fd_; }

  //! get the socket adress
  const std::string &get_host() const { return host_; }

  //! get the socket port
  int get_port() const { return port_; }

  //! returns true or false whether the socket is bound
  bool is_socket_bound() const { return is_socket_bound_; }

 private:
  //! retrieve address informations
  void get_addr_info() {
    int status;
    struct addrinfo hints;
    struct addrinfo *infos;

    ::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if ((status = ::getaddrinfo(host_.c_str(), std::to_string(port_).c_str(),
                                &hints, &infos)) != 0)
      __RUNTIME_ERROR__("tcp::socket::get_addr_info: getaddrinfo() failed.");

    if (infos) {
      ::memcpy(&addrinfo_, infos, sizeof(*infos));
      // NOTE:  TODO WHY ? find why bytes are not addressable
      // ::freeaddrinfo(infos);
    }
  }

  //! creates an endpoint for communication
  void create_socket() {
    if (fd_ != -1) return;

    for (auto p = &addrinfo_; p != NULL; p = p->ai_next) {
      if ((fd_ = ::socket(p->ai_family, p->ai_socktype, p->ai_protocol)) ==
          -1) {
        __DISPLAY_ERROR__("failed to create socket ...");
        continue;
      }
      ::memcpy(&v_addrinfo_, p, sizeof(*p));
      break;
    }
    if (fd_ == -1)
      __RUNTIME_ERROR__("tcp::socket::create_socket: socket failed().");
  }

 private:
  //! filedescriptor associated to the socket
  int fd_;

  //! socket address
  std::string host_;

  //! socket port
  unsigned int port_;

  //! socket address informations
  struct addrinfo addrinfo_;

  //! valid socket address informations
  struct addrinfo v_addrinfo_;

  //! boolean to know if the socket is bound
  bool is_socket_bound_;
};

//!
//! tcp server
//!

class server {
 public:
  //! ctor
  server(void) : is_running_(false), callback_(nullptr) {}

  //! copy ctor
  server(const server &) = delete;

  //! assignment operator
  server &operator=(const server &) = delete;

  //! dtor
  ~server(void) { stop(); }

 public:
  //! set callback
  void on_accept(
      const std::function<void(const std::string &, socket &)> &callback) {
    callback_ = callback;
  }

  // starts the server
  void run(const std::string &host, unsigned int port) {
    if (is_running_)
      __RUNTIME_ERROR__("tcp::server::run: Server is aldready running.");

    socket_.bind(host, port);
    socket_.listen(tools::BACKLOG);
    is_running_ = true;
    process();
  }

  //! stops the server
  void stop(void) {
    if (not is_running_) return;

    socket_.close();
    is_running_ = false;
  }

 private:
  //!
  void process() {
    auto client = socket_.accept();
    auto rcv = client.receive(tools::BUFFER_SIZE);
    std::string cmd(rcv.data());
    std::string response("Executing command [");
    cmd.erase(cmd.end() - 1);
    response += cmd;
    response += "] ...\n";
    client.send(response);
    if (callback_) callback_(cmd, client);
  }

 private:
  //! server socket
  socket socket_;

  //! boolean to know if the server is already running
  std::atomic_bool is_running_;

  //! server behaviour
  std::function<void(const std::string &, socket &client)> callback_;
};

}  //! namespace tcp
}  //! namespace network
}  //! namespace netlib
