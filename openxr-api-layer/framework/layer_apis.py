# The list of OpenXR functions our layer will override.
override_functions = [
    "xrGetSystem",
    "xrGetSystemProperties",
    "xrSuggestInteractionProfileBindings",
    "xrCreateSession",
    "xrDestroySession",
    "xrGetCurrentInteractionProfile",
    "xrCreateActionSpace",
    "xrDestroySpace",
    "xrGetActionStatePose",
    "xrWaitFrame",
    "xrBeginFrame",
    "xrLocateSpace",
    "xrEnumerateBoundSourcesForAction",
    "xrGetInputSourceLocalizedName",
]

# The list of OpenXR functions our layer will use from the runtime.
# Might repeat entries from override_functions above.
requested_functions = [
    "xrGetInstanceProperties",
    "xrGetSystemProperties",
    "xrCreateReferenceSpace",
    "xrStringToPath",
    "xrPathToString",
]

# The list of OpenXR extensions our layer will either override or use.
extensions = ['XR_EXT_eye_gaze_interaction']
