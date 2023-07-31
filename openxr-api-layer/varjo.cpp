// MIT License
//
// Copyright(c) 2022-2023 Matthieu Bucchianeri
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

#include "pch.h"

#include "utils.h"
#include <log.h>

#include "trackers.h"

namespace openxr_api_layer {

    using namespace log;

    namespace {

        // Hook to fake process ID.
        decltype(GetCurrentProcessId)* g_original_GetCurrentProcessId = nullptr;
        DWORD WINAPI hooked_GetCurrentProcessId() {
            // We try to only intercept calls from the Varjo client.
            HMODULE callerModule;
            if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                                       GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                                   (LPCSTR)_ReturnAddress(),
                                   &callerModule)) {
                HMODULE libVarjo = GetModuleHandleA("VarjoLib.dll");
                HMODULE libVarjoRuntime = GetModuleHandleA("VarjoRuntime.dll");
                if (callerModule != libVarjo && callerModule != libVarjoRuntime) {
                    return g_original_GetCurrentProcessId();
                }
            }

            // Always return a process ID different from ours.
            return g_original_GetCurrentProcessId() + 42;
        }

    } // namespace

    struct VarjoEyeTracker : IEyeTracker {
        VarjoEyeTracker() {
            if (!varjo_IsAvailable()) {
                throw EyeTrackerNotSupportedException();
            }

            // We will fake process ID to make sure the Varjo SDK doesn't exit SteamVR.
            utilities::DetourDllAttach(
                "kernel32.dll", "GetCurrentProcessId", hooked_GetCurrentProcessId, g_original_GetCurrentProcessId);

            m_session = varjo_SessionInit();

            utilities::DetourDllDetach(
                "kernel32.dll", "GetCurrentProcessId", hooked_GetCurrentProcessId, g_original_GetCurrentProcessId);
            if (!m_session) {
                throw EyeTrackerNotSupportedException();
            }
        }

        ~VarjoEyeTracker() override {
            varjo_SessionShutDown(m_session);
        }

        void start() override {
            varjo_GazeInit(m_session);
        }

        void stop() override {
        }

        bool isGazeAvailable() const override {
            const auto gaze = varjo_GetGaze(m_session);
            if (gaze.leftStatus == varjo_GazeEyeStatus_Invalid || gaze.rightStatus == varjo_GazeEyeStatus_Invalid) {
                return false;
            }

            return true;
        }

        bool getGaze(XrVector3f& unitVector) override {
            const auto gaze = varjo_GetGaze(m_session);
            if (gaze.leftStatus == varjo_GazeEyeStatus_Invalid || gaze.rightStatus == varjo_GazeEyeStatus_Invalid) {
                return false;
            }

            unitVector.x = (float)(gaze.leftEye.forward[0] + gaze.rightEye.forward[0]) / 2.f;
            unitVector.y = (float)(gaze.leftEye.forward[1] + gaze.rightEye.forward[1]) / 2.f;
            unitVector.z = (float)(gaze.leftEye.forward[2] + gaze.rightEye.forward[2]) / 2.f;

            return true;
        }

        TrackerType getType() const {
            return TrackerType::Varjo;
        }

        varjo_Session* m_session{nullptr};
    };

    std::unique_ptr<IEyeTracker> createVarjoEyeTracker() {
        try {
            return std::make_unique<VarjoEyeTracker>();
        } catch (EyeTrackerNotSupportedException&) {
            return {};
        }
    }

} // namespace openxr_api_layer
