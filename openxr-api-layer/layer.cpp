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

#include "pch.h"

#include "layer.h"
#include "utils.h"
#include <log.h>
#include <util.h>

#include "trackers.h"

namespace openxr_api_layer {

    using namespace log;
    using namespace xr::math;

    // Our API layer implement these extensions, and their specified version.
    const std::vector<std::pair<std::string, uint32_t>> advertisedExtensions = {
        std::make_pair(XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME, 2)};

    // Initialize these vectors with arrays of extensions to block and implicitly request for the instance.
    //
    // Note that we block and implicitly request XR_EXT_eye_gaze_interaction in order to allow passthrough of it to the
    // runtime, in case we detect after instance creation that the upstream API layers or runtime are adequate.
    const std::vector<std::string> blockedExtensions = {XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME};
    const std::vector<std::string> implicitExtensions = {XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME,
                                                         XR_FB_EYE_TRACKING_SOCIAL_EXTENSION_NAME};

    // This class implements our API layer.
    class OpenXrLayer : public openxr_api_layer::OpenXrApi {
      public:
        OpenXrLayer() = default;
        ~OpenXrLayer() = default;

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetInstanceProcAddr
        XrResult xrGetInstanceProcAddr(XrInstance instance, const char* name, PFN_xrVoidFunction* function) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetInstanceProcAddr",
                              TLXArg(instance, "Instance"),
                              TLArg(name, "Name"),
                              TLArg(m_bypassApiLayer, "Bypass"));

            XrResult result = m_bypassApiLayer ? m_xrGetInstanceProcAddr(instance, name, function)
                                               : OpenXrApi::xrGetInstanceProcAddr(instance, name, function);

            TraceLoggingWrite(g_traceProvider, "xrGetInstanceProcAddr", TLPArg(*function, "Function"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateInstance
        XrResult xrCreateInstance(const XrInstanceCreateInfo* createInfo) override {
            if (createInfo->type != XR_TYPE_INSTANCE_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            // Needed to resolve the requested function pointers.
            OpenXrApi::xrCreateInstance(createInfo);

            // Dump the application name, OpenXR runtime information and other useful things for debugging.
            TraceLoggingWrite(g_traceProvider,
                              "xrCreateInstance",
                              TLArg(xr::ToString(createInfo->applicationInfo.apiVersion).c_str(), "ApiVersion"),
                              TLArg(createInfo->applicationInfo.applicationName, "ApplicationName"),
                              TLArg(createInfo->applicationInfo.applicationVersion, "ApplicationVersion"),
                              TLArg(createInfo->applicationInfo.engineName, "EngineName"),
                              TLArg(createInfo->applicationInfo.engineVersion, "EngineVersion"),
                              TLArg(createInfo->createFlags, "CreateFlags"));
            Log(fmt::format("Application: {}\n", createInfo->applicationInfo.applicationName));

            for (uint32_t i = 0; i < createInfo->enabledApiLayerCount; i++) {
                TraceLoggingWrite(
                    g_traceProvider, "xrCreateInstance", TLArg(createInfo->enabledApiLayerNames[i], "ApiLayerName"));
            }

            // Bypass the API layer unless the application requested the eye gaze interaction extension.
            bool requestedEyeGazeInteraction = false;
            for (uint32_t i = 0; i < createInfo->enabledExtensionCount; i++) {
                const std::string_view ext(createInfo->enabledExtensionNames[i]);
                TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(ext.data(), "ExtensionName"));
                if (ext == XR_EXT_EYE_GAZE_INTERACTION_EXTENSION_NAME) {
                    requestedEyeGazeInteraction = true;
                }
            }

            m_bypassApiLayer = !requestedEyeGazeInteraction;
            if (m_bypassApiLayer) {
                Log(fmt::format("{} layer will be bypassed\n", LayerName));
                return XR_SUCCESS;
            }

            XrInstanceProperties instanceProperties = {XR_TYPE_INSTANCE_PROPERTIES};
            CHECK_XRCMD(OpenXrApi::xrGetInstanceProperties(GetXrInstance(), &instanceProperties));
            const auto runtimeName = fmt::format("{} {}.{}.{}",
                                                 instanceProperties.runtimeName,
                                                 XR_VERSION_MAJOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_MINOR(instanceProperties.runtimeVersion),
                                                 XR_VERSION_PATCH(instanceProperties.runtimeVersion));
            TraceLoggingWrite(g_traceProvider, "xrCreateInstance", TLArg(runtimeName.c_str(), "RuntimeName"));
            Log(fmt::format("Using OpenXR runtime: {}\n", runtimeName));

            return XR_SUCCESS;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetSystem
        XrResult xrGetSystem(XrInstance instance, const XrSystemGetInfo* getInfo, XrSystemId* systemId) override {
            if (getInfo->type != XR_TYPE_SYSTEM_GET_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrGetSystem",
                              TLXArg(instance, "Instance"),
                              TLArg(xr::ToCString(getInfo->formFactor), "FormFactor"));

            const XrResult result = OpenXrApi::xrGetSystem(instance, getInfo, systemId);

            if (XR_SUCCEEDED(result) && getInfo->formFactor == XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY) {
                if (*systemId != m_systemId) {
                    // Check if the system supports eye tracking.
                    XrSystemEyeGazeInteractionPropertiesEXT eyeGazeInteractionProperties{
                        XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT};
                    XrSystemEyeTrackingPropertiesFB eyeTrackingProperties{XR_TYPE_SYSTEM_EYE_TRACKING_PROPERTIES_FB,
                                                                          &eyeGazeInteractionProperties};
                    XrSystemProperties systemProperties{XR_TYPE_SYSTEM_PROPERTIES};
                    systemProperties.next = &eyeTrackingProperties;
                    CHECK_XRCMD(OpenXrApi::xrGetSystemProperties(instance, *systemId, &systemProperties));
                    TraceLoggingWrite(
                        g_traceProvider,
                        "xrGetSystem",
                        TLArg(systemProperties.systemName, "SystemName"),
                        TLArg(eyeGazeInteractionProperties.supportsEyeGazeInteraction, "SupportsEyeGazeInteraction"),
                        TLArg(eyeTrackingProperties.supportsEyeTracking, "SupportsEyeTracking"));
                    std::string_view systemName(systemProperties.systemName);
                    Log(fmt::format("Using OpenXR system: {}\n", systemName.data()));

                    m_trackerType = TrackerType::None;
                    if (eyeGazeInteractionProperties.supportsEyeGazeInteraction &&
                        systemName.find("Windows Mixed Reality") == std::string::npos) {
                        // If the upstream API layers or runtime already support eye gaze interaction, we passthrough to
                        // it.
                        // Note: that WMR advertises eye gaze interaction, but it is not real on PC platforms.
                        m_trackerType = TrackerType::EyeGazeInteraction;
                    } else if (utilities::RegGetDword(
                                   HKEY_LOCAL_MACHINE, "SOFTWARE\\OpenXR-Eye-Trackers", "SimulateTracker")
                                   .value_or(false)) {
                        // Configuration requested the mouse simulated eye tracking.
                        m_tracker = createSimulatedEyeTracker();
                    } else if (eyeTrackingProperties.supportsEyeTracking) {
                        // Quest Pro only supports "social eye tracking", which we can translate into eye gaze
                        // interaction.
                        // TODO: Quest Pro eye tracking.
                    } else {
                        // Attempt to initialize external eye tracking API.
                        if (systemName.find("Windows Mixed Reality") != std::string::npos ||
                            systemName.find("SteamVR/OpenXR : holographic") != std::string::npos) {
                            m_tracker = createOmniceptEyeTracker();
                        } else if (systemName.find("SteamVR/OpenXR : aapvr") != std::string::npos) {
                            // TODO: Pimax Crystal eye tracking.
                        } else if (systemName.find("SteamVR/OpenXR") != std::string::npos) {
                            m_tracker = createVarjoEyeTracker();
                        }
                    }
                    if (m_tracker) {
                        m_trackerType = m_tracker->getType();
                    }
                }

                // Remember the XrSystemId to use.
                m_systemId = *systemId;
            }

            TraceLoggingWrite(g_traceProvider, "xrGetSystem", TLArg((int)*systemId, "SystemId"));

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetSystemProperties
        XrResult xrGetSystemProperties(XrInstance instance,
                                       XrSystemId systemId,
                                       XrSystemProperties* properties) override {
            TraceLoggingWrite(g_traceProvider,
                              "xrGetSystemProperties",
                              TLXArg(instance, "Instance"),
                              TLArg((int)systemId, "SystemId"));

            const XrResult result = OpenXrApi::xrGetSystemProperties(instance, systemId, properties);

            if (XR_SUCCEEDED(result)) {
                if (isSystemHandled(systemId) && !isPassthrough()) {
                    XrSystemEyeGazeInteractionPropertiesEXT* eyeGazeInteractionProperties =
                        reinterpret_cast<XrSystemEyeGazeInteractionPropertiesEXT*>(properties->next);
                    while (eyeGazeInteractionProperties) {
                        if (eyeGazeInteractionProperties->type == XR_TYPE_SYSTEM_EYE_GAZE_INTERACTION_PROPERTIES_EXT) {
                            eyeGazeInteractionProperties->supportsEyeGazeInteraction =
                                m_trackerType != TrackerType::None ? XR_TRUE : XR_FALSE;

                            TraceLoggingWrite(g_traceProvider,
                                              "xrGetSystemProperties",
                                              TLArg(!!eyeGazeInteractionProperties->supportsEyeGazeInteraction,
                                                    "SupportsEyeGazeInteraction"));
                            break;
                        }
                        eyeGazeInteractionProperties = reinterpret_cast<XrSystemEyeGazeInteractionPropertiesEXT*>(
                            eyeGazeInteractionProperties->next);
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateSession
        XrResult xrCreateSession(XrInstance instance,
                                 const XrSessionCreateInfo* createInfo,
                                 XrSession* session) override {
            if (createInfo->type != XR_TYPE_SESSION_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateSession",
                              TLXArg(instance, "Instance"),
                              TLArg((int)createInfo->systemId, "SystemId"),
                              TLArg(createInfo->createFlags, "CreateFlags"));

            const XrResult result = OpenXrApi::xrCreateSession(instance, createInfo, session);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrCreateSession", TLXArg(*session, "Session"));

                if (isSystemHandled(createInfo->systemId)) {
                    m_session = *session;

                    if (m_tracker) {
                        m_tracker->start();
                    }
                    {
                        XrReferenceSpaceCreateInfo referenceSpaceInfo{XR_TYPE_REFERENCE_SPACE_CREATE_INFO};
                        referenceSpaceInfo.referenceSpaceType = XR_REFERENCE_SPACE_TYPE_VIEW;
                        referenceSpaceInfo.poseInReferenceSpace = Pose::Identity();
                        CHECK_XRCMD(OpenXrApi::xrCreateReferenceSpace(m_session, &referenceSpaceInfo, &m_viewSpace));
                    }
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrDestroySession
        XrResult xrDestroySession(XrSession session) override {
            TraceLoggingWrite(g_traceProvider, "xrDestroySession", TLXArg(session, "Session"));

            const XrResult result = OpenXrApi::xrDestroySession(session);

            if (XR_SUCCEEDED(result)) {
                if (isSessionHandled(session)) {
                    if (m_tracker) {
                        m_tracker->stop();
                    }

                    m_session = XR_NULL_HANDLE;
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrSuggestInteractionProfileBindings
        XrResult xrSuggestInteractionProfileBindings(
            XrInstance instance, const XrInteractionProfileSuggestedBinding* suggestedBindings) override {
            if (suggestedBindings->type != XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrSuggestInteractionProfileBindings",
                              TLXArg(instance, "Instance"),
                              TLArg(getXrPath(suggestedBindings->interactionProfile).c_str(), "InteractionProfile"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            const std::string& interactionProfile = getXrPath(suggestedBindings->interactionProfile);
            if (!isPassthrough() && interactionProfile == "/interaction_profiles/ext/eye_gaze_interaction") {
                std::unique_lock lock(m_actionsAndSpacesMutex);

                for (uint32_t i = 0; i < suggestedBindings->countSuggestedBindings; i++) {
                    TraceLoggingWrite(
                        g_traceProvider,
                        "xrSuggestInteractionProfileBindings",
                        TLXArg(suggestedBindings->suggestedBindings[i].action, "Action"),
                        TLArg(getXrPath(suggestedBindings->suggestedBindings[i].binding).c_str(), "Path"));

                    const std::string& path = getXrPath(suggestedBindings->suggestedBindings[i].binding);
                    // TODO: Add conformance checks.
                    if (path == "/user/eyes_ext/input/gaze_ext/pose" || path == "/user/eyes_ext/input/gaze_ext") {
                        m_eyeGazeActions.insert(suggestedBindings->suggestedBindings[i].action);
                    }
                }

                // We don't actually suggest the bindings, they would cause an error since the interaction profile is
                // not supported.
                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrSuggestInteractionProfileBindings(instance, suggestedBindings);
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrCreateActionSpace
        XrResult xrCreateActionSpace(XrSession session,
                                     const XrActionSpaceCreateInfo* createInfo,
                                     XrSpace* space) override {
            if (createInfo->type != XR_TYPE_ACTION_SPACE_CREATE_INFO) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrCreateActionSpace",
                              TLXArg(session, "Session"),
                              TLXArg(createInfo->action, "Action"),
                              TLArg(getXrPath(createInfo->subactionPath).c_str(), "SubactionPath"),
                              TLArg(xr::ToString(createInfo->poseInActionSpace).c_str(), "PoseInActionSpace"));

            const XrResult result = OpenXrApi::xrCreateActionSpace(session, createInfo, space);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrCreateActionSpace", TLXArg(*space, "Space"));

                if (isSessionHandled(session) && !isPassthrough()) {
                    std::unique_lock lock(m_actionsAndSpacesMutex);

                    ActionSpace actionSpace{};
                    actionSpace.action = createInfo->action;
                    actionSpace.pose = createInfo->poseInActionSpace;
                    m_actionSpaces.insert_or_assign(*space, actionSpace);
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrDestroySpace
        XrResult xrDestroySpace(XrSpace space) override {
            TraceLoggingWrite(g_traceProvider, "xrDestroySpace", TLXArg(space, "Space"));

            std::unique_lock lock(m_actionsAndSpacesMutex);

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (!isPassthrough() && m_actionSpaces.erase(space)) {
                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrDestroySpace(space);
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrWaitFrame
        XrResult xrWaitFrame(XrSession session,
                             const XrFrameWaitInfo* frameWaitInfo,
                             XrFrameState* frameState) override {
            TraceLoggingWrite(g_traceProvider, "xrWaitFrame", TLXArg(session, "Session"));

            const XrResult result = OpenXrApi::xrWaitFrame(session, frameWaitInfo, frameState);

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider,
                                  "xrWaitFrame",
                                  TLArg(!!frameState->shouldRender, "ShouldRender"),
                                  TLArg(frameState->predictedDisplayTime, "PredictedDisplayTime"),
                                  TLArg(frameState->predictedDisplayPeriod, "PredictedDisplayPeriod"));

                if (isSessionHandled(session)) {
                    m_lastFrameWaitedTime = frameState->predictedDisplayTime;
                }
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrBeginFrame
        XrResult xrBeginFrame(XrSession session, const XrFrameBeginInfo* frameBeginInfo) override {
            TraceLoggingWrite(g_traceProvider, "xrBeginFrame", TLXArg(session, "Session"));

            const XrResult result = OpenXrApi::xrBeginFrame(session, frameBeginInfo);

            if (XR_SUCCEEDED(result) && isSessionHandled(session)) {
                m_lastFrameBegunTime = m_lastFrameWaitedTime;
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrLocateSpace
        XrResult xrLocateSpace(XrSpace space, XrSpace baseSpace, XrTime time, XrSpaceLocation* location) override {
            if (location->type != XR_TYPE_SPACE_LOCATION) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrLocateSpace",
                              TLXArg(space, "Space"),
                              TLXArg(baseSpace, "BaseSpace"),
                              TLArg(time, "Time"));

            std::unique_lock lock(m_actionsAndSpacesMutex);

            XrPosef queryPoseOffset;
            bool isQueryEyeGaze = false;
            {
                auto it = m_actionSpaces.find(space);
                if (it != m_actionSpaces.end()) {
                    if (!it->second.isEyeGaze) {
                        it->second.isEyeGaze = !!m_eyeGazeActions.count(it->second.action);
                    }
                    isQueryEyeGaze = it->second.isEyeGaze.value();
                    queryPoseOffset = it->second.pose;
                }
            }

            XrPosef basePoseOffset;
            bool isBaseEyeGaze = false;
            {
                auto it = m_actionSpaces.find(baseSpace);
                if (it != m_actionSpaces.end()) {
                    if (!it->second.isEyeGaze) {
                        it->second.isEyeGaze = !!m_eyeGazeActions.count(it->second.action);
                    }
                    isBaseEyeGaze = it->second.isEyeGaze.value();
                    basePoseOffset = it->second.pose;
                }
            }

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (isQueryEyeGaze || isBaseEyeGaze) {
                if (isQueryEyeGaze && isBaseEyeGaze) {
                    location->pose = Pose::Multiply(queryPoseOffset, Pose::Invert(basePoseOffset));
                    location->locationFlags =
                        XR_SPACE_LOCATION_ORIENTATION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_TRACKED_BIT |
                        XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_POSITION_TRACKED_BIT;
                } else {
                    location->locationFlags = 0;
                    XrVector3f gazeUnitVector;
                    if (getEyeGaze(time, false, gazeUnitVector)) {
                        XrSpaceLocation viewToSpace{XR_TYPE_SPACE_LOCATION};
                        CHECK_XRCMD(OpenXrApi::xrLocateSpace(
                            m_viewSpace, isQueryEyeGaze ? baseSpace : space, time, &viewToSpace));
                        if (Pose::IsPoseValid(viewToSpace.locationFlags)) {
                            const XrPosef eyeGazeToView = Pose::MakePose(
                                Quaternion::RotationRollPitchYaw({tan(gazeUnitVector.y), -tan(gazeUnitVector.x), 0.f}),
                                XrVector3f{0, 0, 0});

                            location->pose = Pose::Multiply(
                                Pose::Multiply(eyeGazeToView, isQueryEyeGaze ? queryPoseOffset : basePoseOffset),
                                viewToSpace.pose);
                            if (isBaseEyeGaze) {
                                location->pose = Pose::Invert(location->pose);
                            }

                            location->locationFlags = viewToSpace.locationFlags;
                            // TODO: P2: Handle XrEyeGazeSampleTimeEXT
                        }
                    }
                }
                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrLocateSpace(space, baseSpace, time, location);
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider,
                                  "xrLocateSpace",
                                  TLArg(location->locationFlags, "LocationFlags"),
                                  TLArg(xr::ToString(location->pose).c_str(), "Pose"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetActionStatePose
        XrResult xrGetActionStatePose(XrSession session,
                                      const XrActionStateGetInfo* getInfo,
                                      XrActionStatePose* state) override {
            if (getInfo->type != XR_TYPE_ACTION_STATE_GET_INFO || state->type != XR_TYPE_ACTION_STATE_POSE) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrGetActionStatePose",
                              TLXArg(session, "Session"),
                              TLXArg(getInfo->action, "Action"),
                              TLArg(getXrPath(getInfo->subactionPath).c_str(), "SubactionPath"));

            std::unique_lock lock(m_actionsAndSpacesMutex);

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (isSessionHandled(session) && !isPassthrough() && m_eyeGazeActions.count(getInfo->action)) {
                // TODO: Support the notion of (in)active actionsets and actionset priority.
                XrVector3f dummy{};
                state->isActive = getEyeGaze(m_lastFrameBegunTime, true, dummy) ? XR_TRUE : XR_FALSE;
                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrGetActionStatePose(session, getInfo, state);
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(g_traceProvider, "xrGetActionStatePose", TLArg(!!state->isActive, "Active"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetCurrentInteractionProfile
        XrResult xrGetCurrentInteractionProfile(XrSession session,
                                                XrPath topLevelUserPath,
                                                XrInteractionProfileState* interactionProfile) override {
            if (interactionProfile->type != XR_TYPE_INTERACTION_PROFILE_STATE) {
                return XR_ERROR_VALIDATION_FAILURE;
            }

            TraceLoggingWrite(g_traceProvider,
                              "xrGetCurrentInteractionProfile",
                              TLXArg(session, "Session"),
                              TLArg(getXrPath(topLevelUserPath).c_str(), "TopLevelUserPath"));

            XrResult result = XR_ERROR_RUNTIME_FAILURE;
            if (isSessionHandled(session) && !isPassthrough() && getXrPath(topLevelUserPath) == "/user/eyes_ext") {
                CHECK_XRCMD(OpenXrApi::xrStringToPath(GetXrInstance(),
                                                      "/interaction_profiles/ext/eye_gaze_interaction",
                                                      &interactionProfile->interactionProfile));
                result = XR_SUCCESS;
            } else {
                result = OpenXrApi::xrGetCurrentInteractionProfile(session, topLevelUserPath, interactionProfile);
            }

            if (XR_SUCCEEDED(result)) {
                TraceLoggingWrite(
                    g_traceProvider,
                    "xrGetCurrentInteractionProfile",
                    TLArg(getXrPath(interactionProfile->interactionProfile).c_str(), "InteractionProfile"));
            }

            return result;
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrEnumerateBoundSourcesForAction
        XrResult xrEnumerateBoundSourcesForAction(XrSession session,
                                                  const XrBoundSourcesForActionEnumerateInfo* enumerateInfo,
                                                  uint32_t sourceCapacityInput,
                                                  uint32_t* sourceCountOutput,
                                                  XrPath* sources) override {
            // TODO: P2: Add path for eye tracker to bound action
            return OpenXrApi::xrEnumerateBoundSourcesForAction(
                session, enumerateInfo, sourceCapacityInput, sourceCountOutput, sources);
        }

        // https://www.khronos.org/registry/OpenXR/specs/1.0/html/xrspec.html#xrGetInputSourceLocalizedName
        XrResult xrGetInputSourceLocalizedName(XrSession session,
                                               const XrInputSourceLocalizedNameGetInfo* getInfo,
                                               uint32_t bufferCapacityInput,
                                               uint32_t* bufferCountOutput,
                                               char* buffer) override {
            // TODO: P2: Resolved localized name.
            return OpenXrApi::xrGetInputSourceLocalizedName(
                session, getInfo, bufferCapacityInput, bufferCountOutput, buffer);
        }

      private:
        bool getEyeGaze(XrTime time, bool getStateOnly, XrVector3f& unitVector) {
            bool result = false;
            switch (m_trackerType) {
            case TrackerType::EyeTrackingFB:
                // TODO: Quest Pro eye tracking.
                break;

            default:
                if (m_tracker) {
                    if (!getStateOnly) {
                        result = m_tracker->getGaze(unitVector);
                    } else {
                        result = m_tracker->isGazeAvailable();
                    }
                }
                break;

            case TrackerType::None:
                break;
            }

            TraceLoggingWrite(g_traceProvider,
                              "EyeGaze",
                              TLArg(result, "Valid"),
                              TLArg(xr::ToString(unitVector).c_str(), "GazeUnitVector"));

            return result;
        }

        const std::string getXrPath(XrPath path) {
            if (path == XR_NULL_PATH) {
                return "";
            }

            char buf[XR_MAX_PATH_LENGTH];
            uint32_t count;
            CHECK_XRCMD(xrPathToString(GetXrInstance(), path, sizeof(buf), &count, buf));
            std::string str;
            str.assign(buf, count - 1);
            return str;
        }

        bool isSystemHandled(XrSystemId systemId) const {
            return systemId == m_systemId;
        }

        bool isSessionHandled(XrSession session) const {
            return session == m_session;
        }

        bool isPassthrough() const {
            return m_trackerType == TrackerType::EyeGazeInteraction;
        }

        struct ActionSpace {
            XrAction action;
            XrPosef pose;

            std::optional<bool> isEyeGaze;
        };

        bool m_bypassApiLayer{false};
        XrSystemId m_systemId{XR_NULL_SYSTEM_ID};
        XrSession m_session{XR_NULL_HANDLE};
        XrSpace m_viewSpace{XR_NULL_HANDLE};
        std::unique_ptr<IEyeTracker> m_tracker{};
        TrackerType m_trackerType{TrackerType::None};

        XrTime m_lastFrameBegunTime{};
        XrTime m_lastFrameWaitedTime{};

        std::mutex m_actionsAndSpacesMutex;
        std::unordered_set<XrAction> m_eyeGazeActions;
        std::unordered_map<XrSpace, ActionSpace> m_actionSpaces;
    };

    // This method is required by the framework to instantiate your OpenXrApi implementation.
    OpenXrApi* GetInstance() {
        if (!g_instance) {
            g_instance = std::make_unique<OpenXrLayer>();
        }
        return g_instance.get();
    }

} // namespace openxr_api_layer

BOOL APIENTRY DllMain(HMODULE hModule, DWORD ul_reason_for_call, LPVOID lpReserved) {
    switch (ul_reason_for_call) {
    case DLL_PROCESS_ATTACH:
        DetourRestoreAfterWith();
        TraceLoggingRegister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_PROCESS_DETACH:
        TraceLoggingUnregister(openxr_api_layer::log::g_traceProvider);
        break;

    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
        break;
    }
    return TRUE;
}
