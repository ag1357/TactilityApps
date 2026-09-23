# Preserve the frozen research source byte-for-byte. P4's int32_t is long,
# while desktop int32_t is int; make two printf arguments explicitly portable.
# These casts do not change parser, model, operators, verifier, or values.
file(READ "${CMAKE_CURRENT_LIST_DIR}/../core/semantic.c" CG_PORTABLE_SOURCE)
string(REPLACE "r->values[i]);" "(int)r->values[i]);" CG_PORTABLE_SOURCE "${CG_PORTABLE_SOURCE}")
string(REPLACE "f->source,name(v,f->source)" "(unsigned)f->source,name(v,f->source)" CG_PORTABLE_SOURCE "${CG_PORTABLE_SOURCE}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/semantic-portable.c" "${CG_PORTABLE_SOURCE}")
include("$ENV{TACTILITY_SDK_PATH}/TactilitySDK.cmake")
tactility_component_register(SRCS "main.c" "../world/schema.c" "../world/recipe.c" "../world/geometry.c" "../world/traversal.c" "../world/state.c" "../world/resource.c" "../world/persistence.c" "../content/cascade_adapter.c" "${CMAKE_CURRENT_BINARY_DIR}/semantic-portable.c" "../core/decision.c" "../core/general_dialogue.c" "../research/rung_a/cognition.c" "../core/world.c" "../core/state.c" "../core/dialogue.c" "../core/persistence.c" "../core/render.c" INCLUDE_DIRS "../core")

target_compile_definitions(${COMPONENT_LIB} PRIVATE CG_TRAINED_MODEL)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-misleading-indentation)
