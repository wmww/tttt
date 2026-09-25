#pragma once

#include "prelude.h"

// tsc's getScriptTargetFeatures: the globals and members later libs add, with the first lib that has them
typedef struct LibFeature {
    char const* type;
    char const* lib;
    char const* const* members;
    u32 member_count;
} LibFeature;

// The lib a global type or value comes from (tsc's getSuggestedLibForNonExistentName), null if it's no such global
char const* lib_feature_of_global(csview name);
// The lib that adds `member` to the global `type` (tsc's getSuggestedLibForNonExistentProperty), null if none does
char const* lib_feature_of_member(csview type, csview member);
