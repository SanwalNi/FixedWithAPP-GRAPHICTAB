#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <vector>
#include <string>
#include <iostream>

// Note: iphlpapi is linked via -liphlpapi in build.bat

class NetworkHelper {
public:
    static std::vector<std::string> get_local_ipv4_addresses() {
        std::vector<std::string> ips;
        
        ULONG outBufLen = 15000;
        std::vector<BYTE> buffer(outBufLen);
        PIP_ADAPTER_ADDRESSES pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());

        DWORD dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
        if (dwRetVal == ERROR_BUFFER_OVERFLOW) {
            buffer.resize(outBufLen);
            pAddresses = reinterpret_cast<PIP_ADAPTER_ADDRESSES>(buffer.data());
            dwRetVal = GetAdaptersAddresses(AF_INET, GAA_FLAG_INCLUDE_PREFIX, NULL, pAddresses, &outBufLen);
        }

        if (dwRetVal == NO_ERROR) {
            for (PIP_ADAPTER_ADDRESSES pCurr = pAddresses; pCurr != NULL; pCurr = pCurr->Next) {
                if (pCurr->OperStatus != IfOperStatusUp) continue;
                if (pCurr->IfType == IF_TYPE_SOFTWARE_LOOPBACK) continue;

                for (PIP_ADAPTER_UNICAST_ADDRESS pUnicast = pCurr->FirstUnicastAddress; pUnicast != NULL; pUnicast = pUnicast->Next) {
                    sockaddr_in* sa_in = reinterpret_cast<sockaddr_in*>(pUnicast->Address.lpSockaddr);
                    if (sa_in->sin_family == AF_INET) {
                        char ip_buf[INET_ADDRSTRLEN] = {0};
                        inet_ntop(AF_INET, &(sa_in->sin_addr), ip_buf, INET_ADDRSTRLEN);
                        std::string ip_str(ip_buf);
                        if (ip_str != "127.0.0.1" && !ip_str.empty()) {
                            ips.push_back(ip_str);
                        }
                    }
                }
            }
        }

        return ips;
    }
};
