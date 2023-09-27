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
#include <util.h>

#include "trackers.h"

namespace openxr_api_layer {

    using namespace log;

    struct PimaxEyeTracker : IEyeTracker {
        PimaxEyeTracker() {
            pvrResult result = pvr_initialise(&m_pvr);
            if (result != pvr_success) {
                TraceLoggingWrite(g_traceProvider, "PimaxEyeTracker_InitError", TLArg((int)result, "Error"));
                throw EyeTrackerNotSupportedException();
            }

            result = pvr_createSession(m_pvr, &m_pvrSession);
            if (result != pvr_success) {
                TraceLoggingWrite(g_traceProvider, "PimaxEyeTracker_CreateSessionError", TLArg((int)result, "Error"));
                throw EyeTrackerNotSupportedException();
            }

            pvrHmdInfo info{};
            result = pvr_getHmdInfo(m_pvrSession, &info);
            if (result != pvr_success) {
                TraceLoggingWrite(g_traceProvider, "PimaxEyeTracker_HmdInfoError", TLArg((int)result, "Error"));
                throw EyeTrackerNotSupportedException();
            }

            // Look for a Pimax Crystal specifically.
            if (!(info.VendorId == 0x34A4 && info.ProductId == 0x0012)) {
                TraceLoggingWrite(g_traceProvider,
                                  "PimaxEyeTracker_NotSupported",
                                  TLArg(info.VendorId, "VendorId"),
                                  TLArg(info.ProductId, "ProductId"));
                throw EyeTrackerNotSupportedException();
            }
        }

        ~PimaxEyeTracker() override {
            pvr_destroySession(m_pvrSession);
            pvr_shutdown(m_pvr);
        }

        void start(XrSession session) override {
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            pvrEyeTrackingInfo state{};
            pvrResult result = pvr_getEyeTrackingInfo(m_pvrSession, pvr_getTimeSeconds(m_pvr), &state);
            if (result != pvr_success) {
                TraceLoggingWrite(
                    g_traceProvider, "PimaxEyeTracker_GetEyeTrackingInfo_Error", TLArg((int)result, "Error"));
                return false;
            }
            TraceLoggingWrite(
                g_traceProvider, "PimaxEyeTracker_GetEyeTrackingInfo", TLArg(state.TimeInSeconds, "TimeInSeconds"));

            // According to Pimax, this is how we detect gaze not valid.
            if (state.TimeInSeconds == 0) {
                return false;
            }

            return true;
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            pvrEyeTrackingInfo state{};
            // TODO: Properly convert and use XrTime.
            pvrResult result = pvr_getEyeTrackingInfo(m_pvrSession, pvr_getTimeSeconds(m_pvr), &state);
            if (result != pvr_success) {
                TraceLoggingWrite(
                    g_traceProvider, "PimaxEyeTracker_GetEyeTrackingInfo_Error", TLArg((int)result, "Error"));
                return false;
            }
            TraceLoggingWrite(
                g_traceProvider, "PimaxEyeTracker_GetEyeTrackingInfo", TLArg(state.TimeInSeconds, "TimeInSeconds"));

            // According to Pimax, this is how we detect gaze not valid.
            if (state.TimeInSeconds == 0) {
                return false;
            }
            TraceLoggingWrite(g_traceProvider,
                              "PimaxEyeTracker_GetEyeTrackingInfo",
                              TLArg(xr::ToString(XrVector2f{state.GazeTan[xr::StereoView::Left].x,
                                                            state.GazeTan[xr::StereoView::Left].y})
                                        .c_str(),
                                    "LeftGaze"),
                              TLArg(xr::ToString(XrVector2f{state.GazeTan[xr::StereoView::Right].x,
                                                            state.GazeTan[xr::StereoView::Right].y})
                                        .c_str(),
                                    "RightGaze"));

            // Compute the gaze pitch/yaw angles by averaging both eyes.
            const float angleHorizontal =
                atan((state.GazeTan[xr::StereoView::Left].x + state.GazeTan[xr::StereoView::Right].x) / 2.f);
            const float angleVertical =
                atan((state.GazeTan[xr::StereoView::Left].y + state.GazeTan[xr::StereoView::Right].y) / 2.f);

            // Use polar coordinates to create a unit vector.
            unitVector = {
                sin(angleHorizontal) * cos(angleVertical),
                sin(angleVertical),
                -cos(angleHorizontal) * cos(angleVertical),
            };

            return true;
        }

        TrackerType getType() const override {
            return TrackerType::Pimax;
        }

        pvrEnvHandle m_pvr{nullptr};
        pvrSessionHandle m_pvrSession{nullptr};
    };

    std::unique_ptr<IEyeTracker> createPimaxEyeTracker() {
        try {
            return std::make_unique<PimaxEyeTracker>();
        } catch (EyeTrackerNotSupportedException&) {
            return {};
        }
    }

} // namespace openxr_api_layer
