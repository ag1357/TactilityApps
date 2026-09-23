option(CT_PRESENT_PIE "Compile experimental P4 PIE nearest-neighbor backend" OFF)
option(CT_PRESENT_PPA "Compile experimental PPA backend; requires firmware PPA exports" OFF)
set(CT_PRESENT_SOURCES "presentation_p4.c" "../core/presentation.c")
if(CT_PRESENT_PIE)
    list(APPEND CT_PRESENT_SOURCES "presentation_pie.S")
endif()
# Preserve the frozen research source byte-for-byte. P4's int32_t is long,
# while desktop int32_t is int; make two printf arguments explicitly portable.
# These casts do not change parser, model, operators, verifier, or values.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../core/semantic.c" CG_PORTABLE_SOURCE)
string(REPLACE "r->values[i]);" "(int)r->values[i]);" CG_PORTABLE_SOURCE "${CG_PORTABLE_SOURCE}")
string(REPLACE "f->source,name(v,f->source)" "(unsigned)f->source,name(v,f->source)" CG_PORTABLE_SOURCE "${CG_PORTABLE_SOURCE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/semantic-portable.c" "${CG_PORTABLE_SOURCE}")
include("$ENV{TACTILITY_SDK_PATH}/TactilitySDK.cmake")
tactility_component_register(SRCS "main.c" ${CT_PRESENT_SOURCES} "../world/schema.c" "../world/recipe.c" "../world/geometry.c" "../world/traversal.c" "../world/state.c" "../world/resource.c" "../world/persistence.c" "../content/cascade_adapter.c" "${CMAKE_CURRENT_BINARY_DIR}/semantic-portable.c" "../core/decision.c" "../core/general_dialogue.c" "../research/rung_a/cognition.c" "../core/world.c" "../core/state.c" "../core/dialogue.c" "../core/persistence.c" "../core/render.c" INCLUDE_DIRS "../core" PRIV_REQUIRES esp_driver_ppa)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CG_TRAINED_MODEL)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)

# Qualification fallback; default uses conservative incremental row spans.
option(CT_RASTER_REFERENCE "Use original bounding-box raster traversal" OFF)
target_compile_definitions(${COMPONENT_LIB} PRIVATE CT_RASTER_REFERENCE=$<BOOL:${CT_RASTER_REFERENCE}>)

target_compile_definitions(${COMPONENT_LIB} PRIVATE CT_PRESENT_PIE=$<BOOL:${CT_PRESENT_PIE}> CT_PRESENT_PPA=$<BOOL:${CT_PRESENT_PPA}>)
