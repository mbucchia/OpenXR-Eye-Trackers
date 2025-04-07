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

#pragma once

namespace openxr_api_layer {

    struct EyeTrackerNotSupportedException : public std::exception {
        const char* what() const throw() {
            return "Eye tracker is not supported";
        }
    };

    enum class TrackerType {
        None = 0,
        EyeGazeInteraction, // Passthru
        Simulated,
#ifdef _WIN64
        Omnicept,
#endif
        Varjo,
        QuestPro,
        Pimax,
        VirtualDesktop,
        SteamLink,
        OpenXr,
    };

    static inline std::string getTrackerType(TrackerType type) {
        switch (type) {
        case TrackerType::None:
            return "None";
        case TrackerType::EyeGazeInteraction:
            return "Passthrough";
        case TrackerType::Simulated:
            return "Simulated";
#ifdef _WIN64
        case TrackerType::Omnicept:
            return "HP Omnicept";
#endif
        case TrackerType::Varjo:
            return "Varjo";
        case TrackerType::QuestPro:
            return "Quest Pro";
        case TrackerType::Pimax:
            return "Pimax";
        case TrackerType::VirtualDesktop:
            return "Virtual Desktop";
        case TrackerType::SteamLink:
            return "Steam Link";
        case TrackerType::OpenXr:
            return "OpenXR";
        }
        return "<Unknown>";
    }

    struct IEyeTracker {
        virtual ~IEyeTracker() = default;

        virtual void start(XrSession session) = 0;
        virtual void stop() = 0;
        virtual bool isGazeAvailable(XrTime time) const = 0;
        virtual bool getGaze(XrTime time, XrVector3f& unitVector) = 0;
        virtual TrackerType getType() const = 0;
    };

    std::unique_ptr<IEyeTracker> createSimulatedEyeTracker();
#ifdef _WIN64
    std::unique_ptr<IEyeTracker> createOmniceptEyeTracker();
#endif
    std::unique_ptr<IEyeTracker> createVarjoEyeTracker();
    std::unique_ptr<IEyeTracker> createQuestProEyeTracker(OpenXrApi& openXrApi);
    std::unique_ptr<IEyeTracker> createPimaxEyeTracker();
    std::unique_ptr<IEyeTracker> createVirtualDesktopEyeTracker();
    std::unique_ptr<IEyeTracker> createSteamLinkEyeTracker();

} // namespace openxr_api_layer
