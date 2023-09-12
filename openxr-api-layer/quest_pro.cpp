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

    struct QuestProEyeTracker : IEyeTracker {
        QuestProEyeTracker(OpenXrApi& openXrApi) : m_openXrApi(openXrApi) {
        }

        void start(XrSession session) override {
            XrEyeTrackerCreateInfoFB createInfo{XR_TYPE_EYE_TRACKER_CREATE_INFO_FB};
            CHECK_XRCMD(m_openXrApi.xrCreateEyeTrackerFB(session, &createInfo, &m_eyeTracker));

            XrReferenceSpaceCreateInfo referenceSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
            referenceSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
            referenceSpaceInfo.poseInReferenceSpace = xr::math::Pose::Identity();
            CHECK_XRCMD(m_openXrApi.xrCreateReferenceSpace(session, &referenceSpaceInfo, &m_viewSpace));
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            XrEyeGazesInfoFB eyeGazeInfo{XR_TYPE_EYE_GAZES_INFO_FB};
            eyeGazeInfo.baseSpace = m_viewSpace;
            eyeGazeInfo.time = time;

            XrEyeGazesFB eyeGaze{XR_TYPE_EYE_GAZES_FB};
            CHECK_XRCMD(m_openXrApi.xrGetEyeGazesFB(m_eyeTracker, &eyeGazeInfo, &eyeGaze));
            TraceLoggingWrite(g_traceProvider,
                              "EyeTrackerFB",
                              TLArg(!!eyeGaze.gaze[xr::StereoView::Left].isValid, "LeftValid"),
                              TLArg(eyeGaze.gaze[xr::StereoView::Left].gazeConfidence, "LeftConfidence"),
                              TLArg(!!eyeGaze.gaze[xr::StereoView::Right].isValid, "RightValid"),
                              TLArg(eyeGaze.gaze[xr::StereoView::Right].gazeConfidence, "RightConfidence"));

            if (!(eyeGaze.gaze[xr::StereoView::Left].isValid && eyeGaze.gaze[xr::StereoView::Right].isValid)) {
                return false;
            }
            if (!(eyeGaze.gaze[xr::StereoView::Left].gazeConfidence > 0.5f &&
                  eyeGaze.gaze[xr::StereoView::Right].gazeConfidence > 0.5f)) {
                return false;
            }

            return true;
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            XrEyeGazesInfoFB eyeGazeInfo{XR_TYPE_EYE_GAZES_INFO_FB};
            eyeGazeInfo.baseSpace = m_viewSpace;
            eyeGazeInfo.time = time;

            XrEyeGazesFB eyeGaze{XR_TYPE_EYE_GAZES_FB};
            CHECK_XRCMD(m_openXrApi.xrGetEyeGazesFB(m_eyeTracker, &eyeGazeInfo, &eyeGaze));
            TraceLoggingWrite(g_traceProvider,
                              "EyeTrackerFB",
                              TLArg(!!eyeGaze.gaze[xr::StereoView::Left].isValid, "LeftValid"),
                              TLArg(eyeGaze.gaze[xr::StereoView::Left].gazeConfidence, "LeftConfidence"),
                              TLArg(!!eyeGaze.gaze[xr::StereoView::Right].isValid, "RightValid"),
                              TLArg(eyeGaze.gaze[xr::StereoView::Right].gazeConfidence, "RightConfidence"));

            if (!(eyeGaze.gaze[xr::StereoView::Left].isValid && eyeGaze.gaze[xr::StereoView::Right].isValid)) {
                return false;
            }
            if (!(eyeGaze.gaze[xr::StereoView::Left].gazeConfidence > 0.5f &&
                  eyeGaze.gaze[xr::StereoView::Right].gazeConfidence > 0.5f)) {
                return false;
            }
            TraceLoggingWrite(
                g_traceProvider,
                "EyeTrackerFB",
                TLArg(xr::ToString(eyeGaze.gaze[xr::StereoView::Left].gazePose).c_str(), "LeftGazePose"),
                TLArg(xr::ToString(eyeGaze.gaze[xr::StereoView::Right].gazePose).c_str(), "RightGazePose"));

            // Average the poses from both eyes.
            const auto gaze = xr::math::LoadXrPose(xr::math::Pose::Slerp(
                eyeGaze.gaze[xr::StereoView::Left].gazePose, eyeGaze.gaze[xr::StereoView::Right].gazePose, 0.5f));
            const auto gazeProjectedPoint = DirectX::XMVector3Transform(DirectX::XMVectorSet(0.f, 0.f, -1.f, 1.f), gaze);

            unitVector = xr::math::Normalize(
                {gazeProjectedPoint.m128_f32[0], gazeProjectedPoint.m128_f32[1], gazeProjectedPoint.m128_f32[2]});

            return true;
        }

        TrackerType getType() const {
            return TrackerType::QuestPro;
        }

        OpenXrApi& m_openXrApi;
        XrEyeTrackerFB m_eyeTracker{XR_NULL_HANDLE};
        XrSpace m_viewSpace{XR_NULL_HANDLE};
    };

    std::unique_ptr<IEyeTracker> createQuestProEyeTracker(OpenXrApi& openXrApi) {
        return std::make_unique<QuestProEyeTracker>(openXrApi);
    }

} // namespace openxr_api_layer
