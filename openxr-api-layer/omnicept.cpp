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
    using namespace HP::Omnicept;

    struct OmniceptEyeTracker : IEyeTracker {
        OmniceptEyeTracker() {
            if (!utilities::IsServiceRunning("HP Omnicept")) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_NoService");
                throw EyeTrackerNotSupportedException();
            }

            try {
                Client::StateCallback_T stateCallback = [&](const Client::State state) {
                    if (state == Client::State::RUNNING || state == Client::State::PAUSED) {
                        Log("Omnicept client connected\n");
                    } else if (state == Client::State::DISCONNECTED) {
                        Log("Omnicept client disconnected\n");
                    }
                };

                std::unique_ptr<Glia::AsyncClientBuilder> omniceptClientBuilder = Glia::StartBuildClient_Async(
                    "OpenXR-Eye-Trackers",
                    std::move(std::make_unique<Abi::SessionLicense>("", "", Abi::LicensingModel::CORE, false)),
                    stateCallback);

                m_omniceptClient = std::move(omniceptClientBuilder->getBuildClientResultOrThrow());

                std::shared_ptr<Abi::SubscriptionList> subList = Abi::SubscriptionList::GetSubscriptionListToNone();

                Abi::Subscription eyeTrackingSub =
                    Abi::Subscription::generateSubscriptionForDomainType<Abi::EyeTracking>();
                subList->getSubscriptions().push_back(eyeTrackingSub);

                m_omniceptClient->setSubscriptions(*subList);
            } catch (const Abi::HandshakeError& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_HandshakeError", TLArg(e.what(), "Error"));
                Log("Could not connect to Omnicept runtime HandshakeError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (const Abi::TransportError& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_TransportError", TLArg(e.what(), "Error"));
                Log("Could not connect to Omnicept runtime TransportError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (const Abi::ProtocolError& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_ProtocolError", TLArg(e.what(), "Error"));
                Log("Could not connect to Omnicept runtime ProtocolError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (std::exception& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_Error", TLArg(e.what(), "Error"));
                Log("Could not connect to Omnicept runtime: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            }
        }

        void start(XrSession session) override {
            const auto result = m_omniceptClient->startClient();
            if (result != Client::Result::SUCCESS) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_Start_Error", TLArg((int)result, "Result"));
            }
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            Client::LastValueCached<Abi::EyeTracking> lvc;
            try {
                lvc = m_omniceptClient->getLastData<Abi::EyeTracking>();
            } catch (const Abi::SerializationError& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_GetLastData_Error", TLArg(e.what(), "Error"));
                return false;
            }
            TraceLoggingWrite(g_traceProvider,
                              "OmniceptEyeTracker_GetLastData",
                              TLArg(lvc.valid, "Valid"),
                              TLArg(lvc.data.combinedGazeConfidence, "CombinedGazeConfidence"));

            if (!lvc.valid || lvc.data.combinedGazeConfidence < 0.5f) {
                return false;
            }

            return true;
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            Client::LastValueCached<Abi::EyeTracking> lvc;
            try {
                lvc = m_omniceptClient->getLastData<Abi::EyeTracking>();
            } catch (const Abi::SerializationError& e) {
                TraceLoggingWrite(g_traceProvider, "OmniceptEyeTracker_GetLastData_Error", TLArg(e.what(), "Error"));
                return false;
            }
            TraceLoggingWrite(g_traceProvider,
                              "OmniceptEyeTracker_GetLastData",
                              TLArg(lvc.valid, "Valid"),
                              TLArg(lvc.data.combinedGazeConfidence, "CombinedGazeConfidence"));

            if (!lvc.valid || lvc.data.combinedGazeConfidence < 0.5f) {
                return false;
            }
            TraceLoggingWrite(
                g_traceProvider,
                "OmniceptEyeTracker_GetLastData",
                TLArg(
                    xr::ToString(XrVector3f{lvc.data.combinedGaze.x, lvc.data.combinedGaze.y, lvc.data.combinedGaze.z})
                        .c_str(),
                    "CombinedGaze"));

            unitVector.x = -lvc.data.combinedGaze.x;
            unitVector.y = lvc.data.combinedGaze.y;
            unitVector.z = -lvc.data.combinedGaze.z;

            return true;
        }

        TrackerType getType() const {
            return TrackerType::Omnicept;
        }

        std::unique_ptr<Client> m_omniceptClient;
    };

    std::unique_ptr<IEyeTracker> createOmniceptEyeTracker() {
        try {
            return std::make_unique<OmniceptEyeTracker>();
        } catch (EyeTrackerNotSupportedException&) {
            return {};
        }
    }

} // namespace openxr_api_layer
