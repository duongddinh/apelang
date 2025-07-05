#ifndef APE_COMPILER_H
#define APE_COMPILER_H

#include "common.h"

// Compiles source code from a .ape file and writes bytecode to a .apb file.
// Returns 1 on success, 0 on failure.
int compile(const char* source, const char* outputPath);

#endif // APE_COMPILER_H

