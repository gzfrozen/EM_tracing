#pragma once

#include "gdt/random/random.h"

/* Basic random number genorater, can be replaced by better one */
typedef gdt::LCG<16> Random;

/*! per-ray data now captures random number generator, so programs
    can access RNG state */
struct PRD
{
    Random random;
    vec3f pixelColor;
};