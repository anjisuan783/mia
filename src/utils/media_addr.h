#include <vector>
#include <sys/socket.h>
#include <arpa/inet.h>

#include "common/media_kernel_error.h"

#ifndef __MEDIA_ADDRESS_H__
#define __MEDIA_ADDRESS_H__

namespace ma {

class MediaAddress {
 public:
  MediaAddress();
  ~MediaAddress();

  MediaAddress(uint16_t family);

  // Creates an <MediaAddress> from a <port> and the remote
  // <host_name>. The port number is assumed to be in host byte order.
  MediaAddress(const char* host_name, uint16_t port);

  /**
   * Initializes an <MediaAddress> from the <ip_port>, which can be
   * "ip-number:port-number" (e.g., "tango.cs.wustl.edu:1234" or
   * "128.252.166.57:1234").  If there is no ':' in the <address> it
   * is assumed to be a port number, with the IP address being
   * INADDR_ANY.
   * IPV4 only
   */
  MediaAddress(const char* ip_port);

  int Set(const char* host_name, uint16_t port);
  int SetV4(const char* ip_port);

  int SetIpAddr(const char* ipAddr);
  int SetIpAddr(uint16_t family, void*);
  void SetIpAddr(struct sockaddr* addr);
  void SetPort(uint16_t port);

  // Compare two addresses for equality.  The addresses are considered
  // equal if they contain the same IP address and port number.
  bool operator==(const MediaAddress& right) const;

  /**
   * Returns true if <this> is less than <right>.  In this context,
   * "less than" is defined in terms of IP address and TCP port
   * number.  This operator makes it possible to use <ACE_INET_Addr>s
   * in STL maps.
   */
  bool operator<(const MediaAddress& right) const;

  std::string ToString() const;
  uint16_t GetPort() const;
  uint32_t GetSize() const;
  uint16_t GetType() const { return sock_addr_.sin_family; }
  const sockaddr_in* GetPtr() const;

  // void ConvertV4Tov6();
  bool IsResolved() const { return host_name_.empty(); }
  std::string GetHostName() const { return host_name_; }
  
 public:
  static char* Inet_ntop(int af, const void* src, char* buf, size_t size);
  static int Inet_pton(int, const char*, void*);
  static void GetIpWithHostName(const char* host_name,
                                std::vector<std::string>& result);
  static MediaAddress addr_null_;

 private:
  srs_error_t TryResolve();

  bool IsIpv4Legal(const char*);
  union {
    sockaddr_in sock_addr_;
    sockaddr_in6 sock_addr6_;
  };
  // m_strHostName is empty that indicates resovled successfully,
  // otherwise it needs resolving.
  std::string host_name_;
};

}

#endif //!__MEDIA_ADDRESS_H__
