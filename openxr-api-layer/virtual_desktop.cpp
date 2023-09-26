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

#include "layer.h"
#include "utils.h"
#include <log.h>
#include <util.h>

#include "trackers.h"

namespace openxr_api_layer {

    using namespace log;

    // See type definitions from Virtual Desktop.
    // https://github.com/guygodin/VirtualDesktop.VRCFaceTracking/blob/main/FaceState.cs
    // https://github.com/guygodin/VirtualDesktop.VRCFaceTracking/blob/main/Pose.cs
    // https://github.com/guygodin/VirtualDesktop.VRCFaceTracking/blob/main/Quaternion.cs
    // https://github.com/guygodin/VirtualDesktop.VRCFaceTracking/blob/main/Vector3.cs

    struct Vector3 {
        float x, y, z;
    };

    struct Quaternion {
        float x, y, z, w;
    };

    struct Pose {
        Quaternion orientation;
        Vector3 position;
    };

    constexpr int ExpressionCount = 63;
    constexpr int ConfidenceCount = 2;

    struct FaceState {
        uint8_t FaceIsValid;
        uint8_t IsEyeFollowingBlendshapesValid;
        float ExpressionWeights[ExpressionCount];
        float ExpressionConfidences[ConfidenceCount];
        uint8_t LeftEyeIsValid;
        uint8_t RightEyeIsValid;
        Pose LeftEyePose;
        Pose RightEyePose;
        float LeftEyeConfidence;
        float RightEyeConfidence;
    };

    struct VirtualDesktopEyeTracker : IEyeTracker {
        VirtualDesktopEyeTracker() {
            *m_faceStateFile.put() = OpenFileMapping(FILE_MAP_READ, false, L"VirtualDesktop.FaceState");
            if (!m_faceStateFile) {
                TraceLoggingWrite(g_traceProvider, "VirtualDesktopEyeTracker_NotAvailable");
                throw EyeTrackerNotSupportedException();
            }

            m_sharedState = reinterpret_cast<FaceState*>(
                MapViewOfFile(m_faceStateFile.get(), FILE_MAP_READ, 0, 0, sizeof(FaceState)));
            if (!m_sharedState) {
                TraceLoggingWrite(g_traceProvider, "VirtualDesktopEyeTracker_MappingError");
                throw EyeTrackerNotSupportedException();
            }
        }

        ~VirtualDesktopEyeTracker() override {
            UnmapViewOfFile(m_sharedState);
        }

        void start(XrSession session) override {
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            TraceLoggingWrite(g_traceProvider,
                              "VirtualDesktopEyeTracker",
                              TLArg(!!m_sharedState->LeftEyeIsValid, "LeftValid"),
                              TLArg(m_sharedState->LeftEyeConfidence, "LeftConfidence"),
                              TLArg(!!m_sharedState->RightEyeIsValid, "RightValid"),
                              TLArg(m_sharedState->RightEyeConfidence, "RightConfidence"));

            if (!(m_sharedState->LeftEyeIsValid && m_sharedState->RightEyeIsValid)) {
                return false;
            }
            if (!(m_sharedState->LeftEyeConfidence > 0.5f && m_sharedState->RightEyeConfidence > 0.5f)) {
                return false;
            }

            return true;
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            if (!isGazeAvailable(time)) {
                return false;
            }

            // TODO: Any file locking scheme?
            Pose leftEyePose = m_sharedState->LeftEyePose;
            Pose rightEyePose = m_sharedState->RightEyePose;
            XrPosef eyeGaze[] = {
                xr::math::Pose::MakePose(
                    XrQuaternionf{leftEyePose.orientation.x,
                                  leftEyePose.orientation.y,
                                  leftEyePose.orientation.z,
                                  leftEyePose.orientation.w},
                    XrVector3f{leftEyePose.position.x, leftEyePose.position.y, leftEyePose.position.z}),
                xr::math::Pose::MakePose(
                    XrQuaternionf{leftEyePose.orientation.x,
                                  leftEyePose.orientation.y,
                                  leftEyePose.orientation.z,
                                  leftEyePose.orientation.w},
                    XrVector3f{leftEyePose.position.x, leftEyePose.position.y, leftEyePose.position.z})};

            TraceLoggingWrite(g_traceProvider,
                              "VirtualDesktopEyeTracker",
                              TLArg(xr::ToString(eyeGaze[xr::StereoView::Left]).c_str(), "LeftGazePose"),
                              TLArg(xr::ToString(eyeGaze[xr::StereoView::Right]).c_str(), "RightGazePose"));

            // Average the poses from both eyes.
            const auto gaze = xr::math::LoadXrPose(
                xr::math::Pose::Slerp(eyeGaze[xr::StereoView::Left], eyeGaze[xr::StereoView::Right], 0.5f));
            const auto gazeProjectedPoint =
                DirectX::XMVector3Transform(DirectX::XMVectorSet(0.f, 0.f, -1.f, 1.f), gaze);

            unitVector = xr::math::Normalize(
                {gazeProjectedPoint.m128_f32[0], gazeProjectedPoint.m128_f32[1], gazeProjectedPoint.m128_f32[2]});

            return true;
        }

        TrackerType getType() const {
            return TrackerType::VirtualDesktop;
        }

        wil::unique_handle m_faceStateFile;
        FaceState* m_sharedState{nullptr};
    };

    std::unique_ptr<IEyeTracker> createVirtualDesktopEyeTracker() {
        return std::make_unique<VirtualDesktopEyeTracker>();
    }

} // namespace openxr_api_layer
