include("$ENV{TACTILITY_SDK_PATH}/TactilitySDK.cmake")
tactility_component_register(SRCS "main.c" "../core/world.c" "../core/state.c" "../core/dialogue.c" "../core/persistence.c" "../core/render.c" INCLUDE_DIRS "../core")
