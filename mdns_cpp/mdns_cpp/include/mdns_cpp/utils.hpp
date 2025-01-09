#pragma once

#include <string>
#include "mdns.h"

struct sockaddr;
struct sockaddr_in;
struct sockaddr_in6;

namespace mdns_cpp {

std::string getHostName();

std::string ipv4AddressToString(char *buffer, size_t capacity, const struct sockaddr_in *addr, size_t addrlen);

std::string ipv6AddressToString(char *buffer, size_t capacity, const struct sockaddr_in6 *addr, size_t addrlen);

std::string ipAddressToString(char *buffer, size_t capacity, const struct sockaddr *addr, size_t addrlen);

mdns_string_t deep_copy_mdns_string(const mdns_string_t *original);

void free_mdns_string(mdns_string_t *mdns_str);

std::string convert_to_string(const mdns_string_t &mdns_str);

}  // namespace mdns_cpp
