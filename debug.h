// debug.h
#ifndef APE_DEBUG_H
#define APE_DEBUG_H

#include "common.h"

void disassembleBytecode(const char* name, uint8_t* bytecode, long size);
int disassembleInstruction(uint8_t* bytecode, int offset);

#endif