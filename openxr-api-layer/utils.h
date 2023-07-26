// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
// Based on https://github.com/mbucchia/OpenXR-Layer-Template.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this softwareand associated documentation files(the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and /or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions :
//
// The above copyright noticeand this permission notice shall be included in all
// copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.

#pragma once

namespace openxr_api_layer::utilities {

    // https://docs.microsoft.com/en-us/archive/msdn-magazine/2017/may/c-use-modern-c-to-access-the-windows-registry
    static std::optional<int> RegGetDword(HKEY hKey, const std::string& subKey, const std::string& value) {
        DWORD data{};
        DWORD dataSize = sizeof(data);
        LONG retCode = ::RegGetValue(hKey,
                                     std::wstring(subKey.begin(), subKey.end()).c_str(),
                                     std::wstring(value.begin(), value.end()).c_str(),
                                     RRF_SUBKEY_WOW6464KEY | RRF_RT_REG_DWORD,
                                     nullptr,
                                     &data,
                                     &dataSize);
        if (retCode != ERROR_SUCCESS) {
            return {};
        }
        return data;
    }

    // https://stackoverflow.com/questions/7808085/how-to-get-the-status-of-a-service-programmatically-running-stopped
    static bool IsServiceRunning(const std::string& name) {
        SC_HANDLE theService, scm;
        SERVICE_STATUS_PROCESS ssStatus;
        DWORD dwBytesNeeded;

        scm = OpenSCManager(nullptr, nullptr, SC_MANAGER_ENUMERATE_SERVICE);
        if (!scm) {
            return false;
        }

        theService = OpenServiceA(scm, name.c_str(), SERVICE_QUERY_STATUS);
        if (!theService) {
            CloseServiceHandle(scm);
            return false;
        }

        auto result = QueryServiceStatusEx(theService,
                                           SC_STATUS_PROCESS_INFO,
                                           reinterpret_cast<LPBYTE>(&ssStatus),
                                           sizeof(SERVICE_STATUS_PROCESS),
                                           &dwBytesNeeded);

        CloseServiceHandle(theService);
        CloseServiceHandle(scm);

        if (result == 0) {
            return false;
        }

        return ssStatus.dwCurrentState == SERVICE_RUNNING;
    }

} // namespace openxr_api_layer::utilities
