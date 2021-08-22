#include "http/http_message_impl.h"

#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <math.h>
#include <stdlib.h>
#include <map>
#include <sstream>
#include <memory>
#include <cstring>

#include "common/media_log.h"
#include "http/http_consts.h"
#include "common/srs_kernel_error.h"
#include "utils/protocol_utility.h"

namespace ma {

HttpMessage::HttpMessage(const std::string& body)
  : _status{SRS_CONSTS_HTTP_OK},
    _body{std::move(body)},
    _uri{std::make_unique<SrsHttpUri>()}
{
}

void HttpMessage::set_basic(
  uint8_t type, const std::string& method, uint16_t status, int64_t content_length)
{
  type_ = type;
  _method = method;
  _status = status;
  if (_content_length == -1) {
      _content_length = content_length;
  }
}

void HttpMessage::set_header(const SrsHttpHeader& header, bool keep_alive)
{
  _header = header;
  _keep_alive = keep_alive;

  // whether chunked.
  chunked = (header.get("Transfer-Encoding") == "chunked");

  // Update the content-length in header.
  std::string clv = header.get("Content-Length");
  if (!clv.empty()) {
      _content_length = ::atoll(clv.c_str());
  }
}

std::shared_ptr<IMediaConnection> HttpMessage::connection() {
  return owner_;
}

void HttpMessage::connection(std::shared_ptr<IMediaConnection> conn) {
  owner_ = conn;
}

bool srs_string_contains(const std::string& str, const std::string& flag) {
  return str.find(flag) != std::string::npos;
}

static std::string _public_internet_address;

// Get local ip, fill to @param ips
struct SrsIPAddress
{
  // The network interface name, such as eth0, en0, eth1.
  std::string ifname;
  // The IP v4 or v6 address.
  std::string ip;
  // Whether the ip is IPv4 address.
  bool is_ipv4;
  // Whether the ip is internet public IP address.
  bool is_internet;
  // Whether the ip is loopback, such as 127.0.0.1
  bool is_loopback;
};


// we detect all network device as internet or intranet device, by its ip address.
//      key is device name, for instance, eth0
//      value is whether internet, for instance, true.
static std::map<std::string, bool> _srs_device_ifs;

bool srs_net_device_is_internet(const std::string& ifname)
{ 
  if (_srs_device_ifs.find(ifname) == _srs_device_ifs.end()) {
      return false;
  }
  return _srs_device_ifs[ifname];
}

bool srs_net_device_is_internet(const sockaddr* addr)
{
  if(addr->sa_family == AF_INET) {
    const in_addr inaddr = ((sockaddr_in*)addr)->sin_addr;
    const uint32_t addr_h = ntohl(inaddr.s_addr);

    // lo, 127.0.0.0-127.0.0.1
    if (addr_h >= 0x7f000000 && addr_h <= 0x7f000001) {
        return false;
    }

    // Class A 10.0.0.0-10.255.255.255
    if (addr_h >= 0x0a000000 && addr_h <= 0x0affffff) {
        return false;
    }

    // Class B 172.16.0.0-172.31.255.255
    if (addr_h >= 0xac100000 && addr_h <= 0xac1fffff) {
        return false;
    }

    // Class C 192.168.0.0-192.168.255.255
    if (addr_h >= 0xc0a80000 && addr_h <= 0xc0a8ffff) {
        return false;
    }
  } else if(addr->sa_family == AF_INET6) {
    const sockaddr_in6* a6 = (const sockaddr_in6*)addr;

    // IPv6 loopback is ::1
    if (IN6_IS_ADDR_LOOPBACK(&a6->sin6_addr)) {
        return false;
    }

    // IPv6 unspecified is ::
    if (IN6_IS_ADDR_UNSPECIFIED(&a6->sin6_addr)) {
        return false;
    }

    // From IPv4, you might know APIPA (Automatic Private IP Addressing) or AutoNet.
    // Whenever automatic IP configuration through DHCP fails.
    // The prefix of a site-local address is FE80::/10.
    if (IN6_IS_ADDR_LINKLOCAL(&a6->sin6_addr)) {
        return false;
    }

    // Site-local addresses are equivalent to private IP addresses in IPv4.
    // The prefix of a site-local address is FEC0::/10.
    // https://4sysops.com/archives/ipv6-tutorial-part-6-site-local-addresses-and-link-local-addresses/
    if (IN6_IS_ADDR_SITELOCAL(&a6->sin6_addr)) {
       return false;
    }

    // Others.
    if (IN6_IS_ADDR_MULTICAST(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_NODELOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_LINKLOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_SITELOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_ORGLOCAL(&a6->sin6_addr)) {
        return false;
    }
    if (IN6_IS_ADDR_MC_GLOBAL(&a6->sin6_addr)) {
        return false;
    }
  }
  
  return true;
}

void discover_network_iface(ifaddrs* cur, 
                            std::vector<SrsIPAddress*>& ips, 
                            std::stringstream& ss0, 
                            std::stringstream& ss1, 
                            bool ipv6,
                            bool loopback)
{
    char saddr[64];
    char* h = (char*)saddr;
    socklen_t nbh = (socklen_t)sizeof(saddr);
    const int r0 = getnameinfo(cur->ifa_addr, sizeof(sockaddr_storage), h, nbh, NULL, 0, NI_NUMERICHOST);
    if(r0) {
        OS_WARNING_TRACE("convert local ip failed: " << gai_strerror(r0));
        return;
    }
    
    std::string ip(saddr, strlen(saddr));
    ss0 << ", iface[" << (int)ips.size() << "] " << cur->ifa_name << " " << (ipv6? "ipv6":"ipv4")
    << " 0x" << std::hex << cur->ifa_flags  << std::dec << " " << ip;

    SrsIPAddress* ip_address = new SrsIPAddress();
    ip_address->ip = ip;
    ip_address->is_ipv4 = !ipv6;
    ip_address->is_loopback = loopback;
    ip_address->ifname = cur->ifa_name;
    ip_address->is_internet = srs_net_device_is_internet(cur->ifa_addr);
    ips.push_back(ip_address);
    
    // set the device internet status.
    if (!ip_address->is_internet) {
        ss1 << ", intranet ";
        _srs_device_ifs[cur->ifa_name] = false;
    } else {
        ss1 << ", internet ";
        _srs_device_ifs[cur->ifa_name] = true;
    }
    ss1 << cur->ifa_name << " " << ip;
}

static std::vector<SrsIPAddress*> _srs_system_ips;

void retrieve_local_ips()
{
  std::vector<SrsIPAddress*>& ips = _srs_system_ips;

  // Release previous IPs.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      srs_freep(ip);
  }
  ips.clear();

  // Get the addresses.
  ifaddrs* ifap;
  if (getifaddrs(&ifap) == -1) {
      OS_WARNING_TRACE("retrieve local ips, getifaddrs failed.");
      return;
  }
  
  std::stringstream ss0;
  ss0 << "ips";
  
  std::stringstream ss1;
  ss1 << "devices";
  
  // Discover IPv4 first.
  for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
      ifaddrs* cur = p;
      
      // Ignore if no address for this interface.
      // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
      if (!cur->ifa_addr) {
          continue;
      }
      
      // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
      // @see: https://github.com/ossrs/srs/issues/141
      bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
      bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
      // Ignore IFF_PROMISC(Interface is in promiscuous mode), which may be set by Wireshark.
      bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_LOOPBACK) || (cur->ifa_flags & IFF_POINTOPOINT);
      bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
      if (ipv4 && ready && !ignored) {
          discover_network_iface(cur, ips, ss0, ss1, false, loopback);
      }
  }
  
  // Then, discover IPv6 addresses.
  for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
      ifaddrs* cur = p;
      
      // Ignore if no address for this interface.
      // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
      if (!cur->ifa_addr) {
          continue;
      }
      
      // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
      // @see: https://github.com/ossrs/srs/issues/141
      bool ipv6 = (cur->ifa_addr->sa_family == AF_INET6);
      bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
      bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC) || (cur->ifa_flags & IFF_LOOPBACK);
      bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
      if (ipv6 && ready && !ignored) {
          discover_network_iface(cur, ips, ss0, ss1, true, loopback);
      }
  }
  
  // If empty, disover IPv4 loopback.
  if (ips.empty()) {
      for (ifaddrs* p = ifap; p ; p = p->ifa_next) {
          ifaddrs* cur = p;
          
          // Ignore if no address for this interface.
          // @see https://github.com/ossrs/srs/issues/1087#issuecomment-408847115
          if (!cur->ifa_addr) {
              continue;
          }
          
          // retrieve IP address, ignore the tun0 network device, whose addr is NULL.
          // @see: https://github.com/ossrs/srs/issues/141
          bool ipv4 = (cur->ifa_addr->sa_family == AF_INET);
          bool ready = (cur->ifa_flags & IFF_UP) && (cur->ifa_flags & IFF_RUNNING);
          bool ignored = (!cur->ifa_addr) || (cur->ifa_flags & IFF_POINTOPOINT) || (cur->ifa_flags & IFF_PROMISC);
          bool loopback = (cur->ifa_flags & IFF_LOOPBACK);
          if (ipv4 && ready && !ignored) {
              discover_network_iface(cur, ips, ss0, ss1, false, loopback);
          }
      }
  }
  
  OS_INFO_TRACE(ss0.str().c_str() << "," << ss1.str().c_str());
  
  freeifaddrs(ifap);
}

std::vector<SrsIPAddress*>& srs_get_local_ips()
{
    if (_srs_system_ips.empty()) {
        retrieve_local_ips();
    }
    
    return _srs_system_ips;
}

std::string srs_get_public_internet_address(bool ipv4_only)
{
  if (!_public_internet_address.empty()) {
      return _public_internet_address;
  }
  
  std::vector<SrsIPAddress*>& ips = srs_get_local_ips();
  
  // find the best match public address.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      if (!ip->is_internet) {
          continue;
      }
      if (ipv4_only && !ip->is_ipv4) {
          continue;
      }

      MLOG_WARN("use public address as ip:"<< ip->ip.c_str() <<
        ", ifname=" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  // no public address, use private address.
  for (int i = 0; i < (int)ips.size(); i++) {
      SrsIPAddress* ip = ips[i];
      if (ip->is_loopback) {
          continue;
      }
      if (ipv4_only && !ip->is_ipv4) {
          continue;
      }

      MLOG_WARN("use private address as ip: "<< ip->ip.c_str() <<
        ", ifname:" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  // Finally, use first whatever kind of address.
  if (!ips.empty() && _public_internet_address.empty()) {
      SrsIPAddress* ip = ips[0];

      MLOG_WARN("use first address as ip:"<< ip->ip.c_str() << 
        ", ifname=" << ip->ifname.c_str());
      _public_internet_address = ip->ip;
      return ip->ip;
  }
  
  return "";
}

std::string srs_path_filext(const std::string& path)
{
  size_t pos = std::string::npos;
  
  if ((pos = path.rfind(".")) != std::string::npos) {
    return path.substr(pos);
  }
  
  return "";
}

srs_error_t HttpMessage::set_url(const std::string& url, bool allow_jsonp)
{
  srs_error_t err = srs_success;
  
  _url = url;

  // parse uri from schema/server:port/path?query
  std::string uri = _url;

  if (!srs_string_contains(uri, "://")) {
    // use server public ip when host not specified.
    // to make telnet happy.
    std::string host = _header.get("Host");

    // If no host in header, we use local discovered IP, IPv4 first.
    if (host.empty()) {
        host = srs_get_public_internet_address(true);
    }

    if (!host.empty()) {
        uri = "http://" + host + _url;
    }
  }

  if ((err = _uri->initialize(uri)) != srs_success) {
      return srs_error_wrap(err, "init uri %s", uri.c_str());
  }
  
  // parse ext.
  _ext = srs_path_filext(_uri->get_path());
  
  // parse query string.
  srs_parse_query_string(_uri->get_query(), _query);
  
  // parse jsonp request message.
  if (allow_jsonp) {
    if (!query_get("callback").empty()) {
        jsonp = true;
    }
    if (jsonp) {
        jsonp_method = query_get("method");
    }
  }
  
  return err;
}

void HttpMessage::set_https(bool v)
{
  schema_ = v? "https" : "http";
  _uri->set_schema(schema_);
}

const std::string& HttpMessage::schema()
{
  return schema_;
}

const std::string& HttpMessage::method()
{
  return _method;
}

uint16_t HttpMessage::status_code()
{
  return _status;
}

bool HttpMessage::is_http_get()
{
  return method() == "GET";
}

bool HttpMessage::is_http_put()
{
  return method() == "PUT";
}

bool HttpMessage::is_http_post()
{
  return method() == "POST";
}

bool HttpMessage::is_http_delete()
{
  return method() == "DELETE";
}

bool HttpMessage::is_http_options()
{
  return method() == "OPTIONS";
}

bool HttpMessage::is_chunked()
{
  return chunked;
}

bool HttpMessage::is_keep_alive()
{
  return _keep_alive;
}

std::string HttpMessage::uri()
{
  std::string uri = _uri->get_schema();
  if (uri.empty()) {
      uri += "http";
  }
  uri += "://";
  
  uri += host();
  uri += path();
  
  return uri;
}

std::string HttpMessage::url()
{
  return _uri->get_url();
}

std::string HttpMessage::host()
{
  auto it = _query.find("vhost");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  it = _query.find("domain");
  if (it != _query.end() && !it->second.empty()) {
      return it->second;
  }

  return _uri->get_host();
}

int HttpMessage::port()
{
  return _uri->get_port();
}

std::string HttpMessage::path()
{
  return _uri->get_path();
}

std::string HttpMessage::query()
{
  return _uri->get_query();
}

std::string HttpMessage::ext()
{
  return _ext;
}

std::string HttpMessage::parse_rest_id(std::string pattern)
{
  std::string p = _uri->get_path();
  if (p.length() <= pattern.length()) {
      return "";
  }
  
  std::string id = p.substr((int)pattern.length());
  if (!id.empty()) {
      return id;
  }
  
  return "";
}

bool HttpMessage::is_jsonp(){
  return jsonp;
}

std::string HttpMessage::query_get(const std::string& key)
{
  std::string v;
  
  if (_query.find(key) != _query.end()) {
    v = _query[key];
  }
  
  return v;
}

SrsHttpHeader& HttpMessage::header()
{
  return _header;
}

const std::string& HttpMessage::get_body()
{
  return _body;
}

int64_t HttpMessage::content_length()
{
  return _content_length;
}

}

