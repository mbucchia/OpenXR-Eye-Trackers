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
    using namespace HP::Omnicept;

    struct OmniceptEyeTracker : IEyeTracker {
        OmniceptEyeTracker() {
            if (!utilities::IsServiceRunning("HP Omnicept")) {
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
                Log("Could not connect to Omnicept runtime HandshakeError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (const Abi::TransportError& e) {
                Log("Could not connect to Omnicept runtime TransportError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (const Abi::ProtocolError& e) {
                Log("Could not connect to Omnicept runtime ProtocolError: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            } catch (std::exception& e) {
                Log("Could not connect to Omnicept runtime: %s\n", e.what());
                throw EyeTrackerNotSupportedException();
            }
        }

        void start() override {
            m_omniceptClient->startClient();
        }

        void stop() override {
        }

        bool isGazeAvailable() const override {
            Client::LastValueCached<Abi::EyeTracking> lvc = m_omniceptClient->getLastData<Abi::EyeTracking>();
            if (!lvc.valid || lvc.data.combinedGazeConfidence < 0.5f) {
                return false;
            }

            return true;
        }

        bool getGaze(XrVector3f& unitVector) override {
            Client::LastValueCached<Abi::EyeTracking> lvc = m_omniceptClient->getLastData<Abi::EyeTracking>();
            if (!lvc.valid || lvc.data.combinedGazeConfidence < 0.5f) {
                return false;
            }

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
