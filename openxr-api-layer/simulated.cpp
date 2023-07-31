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

#include "trackers.h"

namespace openxr_api_layer {

    using namespace log;

    struct SimulatedEyeTracker : IEyeTracker {
        SimulatedEyeTracker() {
        }

        void start(XrSession session) override {
        }

        void stop() override {
        }

        bool isGazeAvailable(XrTime time) const override {
            return true;
        }

        bool getGaze(XrTime time, XrVector3f& unitVector) override {
            RECT rect;
            rect.left = 1;
            rect.right = 999;
            rect.top = 1;
            rect.bottom = 999;
            ClipCursor(&rect);

            POINT cursor{};
            GetCursorPos(&cursor);

            XrVector2f point = {(float)cursor.x / 1000.f, (float)cursor.y / 1000.f};
            unitVector = xr::math::Normalize({point.x - 0.5f, 0.5f - point.y, -0.35f});

            return true;
        }

        TrackerType getType() const {
            return TrackerType::Simulated;
        }
    };

    std::unique_ptr<IEyeTracker> createSimulatedEyeTracker() {
        return std::make_unique<SimulatedEyeTracker>();
    }

} // namespace openxr_api_layer
