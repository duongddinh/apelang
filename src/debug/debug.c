#include <stdio.h>
#include <string.h>

#include "debug.h"

// Helper to print a simple instruction with commentary
static int simpleInstruction(const char* name, int offset) {
    printf("%s\n", name);
    return offset + 1;
}

// Helper to print an instruction with a one-byte operand (like a local variable slot or argument count)
static int byteInstruction(const char* name, uint8_t* bytecode, int offset) {
    uint8_t slot = bytecode[offset + 1];
    printf("%-16s %4d\n", name, slot);
    return offset + 2;
}

// Helper to print a jump instruction with its 2-byte offset
static int jumpInstruction(const char* name, int sign, uint8_t* bytecode, int offset) {
    uint16_t jump = (uint16_t)(bytecode[offset + 1] << 8 | bytecode[offset + 2]);
    printf("%-16s %4d -> %d\n", name, offset, offset + 3 + sign * jump);
    return offset + 3;
}

// Helper for instructions dealing with global variables, which are stored by name
static int globalInstruction(const char* name, uint8_t* bytecode, int offset) {
    uint8_t len = bytecode[offset + 1];
    printf("%-16s '", name);
    // Print the variable name character by character
    for(int i = 0; i < len; i++) {
        printf("%c", bytecode[offset + 2 + i]);
    }
    printf("'\n");
    return offset + 2 + len;
}

// Helper for the complex OP_PUSH instruction, which handles all literals
static int constantInstruction(const char* name, uint8_t* bytecode, int offset) {
    printf("%-16s ", name);
    // The byte after OP_PUSH tells us the type of literal
    ValueType type = (ValueType)bytecode[offset + 1];
    int current_offset = offset + 2;

    switch(type) {
        case VAL_NUMBER: {
            double num;
            memcpy(&num, &bytecode[current_offset], sizeof(double));
            printf("NUMBER %g\n", num);
            current_offset += sizeof(double);
            break;
        }
        case VAL_OBJ: {
            // If it's an object, we need to know what kind of object
            ObjType objType = (ObjType)bytecode[current_offset];
            current_offset++;
            switch(objType) {
                case OBJ_STRING: {
                    uint8_t len = bytecode[current_offset];
                    current_offset++;
                    printf("STRING \"%.*s\"\n", len, &bytecode[current_offset]);
                    current_offset += len;
                    break;
                }
                case OBJ_FUNCTION: {
                    uint8_t arity = bytecode[current_offset];
                    current_offset++;
                    uint32_t codeAddr;
                    memcpy(&codeAddr, &bytecode[current_offset], sizeof(uint32_t));
                    current_offset += sizeof(uint32_t);
                    uint8_t nameLen = bytecode[current_offset];
                    current_offset++;
                    printf("FUNCTION <tribe %.*s> (arity: %d, addr: %u)\n", nameLen, &bytecode[current_offset], arity, codeAddr);
                    current_offset += nameLen;
                    break;
                }
                default:
                    printf("UNKNOWN_OBJ_TYPE %d\n", objType);
                    break;
            }
            break;
        }
        default:
            printf("UNKNOWN_VAL_TYPE %d\n", type);
            break;
    }
    return current_offset;
}


int disassembleInstruction(uint8_t* bytecode, int offset) {
    printf("%04d ", offset);

    uint8_t instruction = bytecode[offset];
    switch (instruction) {
        case OP_PUSH:           return constantInstruction("OP_PUSH", bytecode, offset);
        case OP_NIL:            return simpleInstruction("OP_NIL           ; nil, the absence of bananas", offset);
        case OP_TRUE:           return simpleInstruction("OP_TRUE          ; true, the banana is ripe", offset);
        case OP_FALSE:          return simpleInstruction("OP_FALSE         ; false, the banana is not ripe", offset);
        case OP_POP:            return simpleInstruction("OP_POP           ; ape drops a banana from the stack", offset);
        case OP_NOT:            return simpleInstruction("OP_NOT           ; is it not a banana?", offset);
        case OP_ADD:            return simpleInstruction("OP_ADD           ; gather more bananas", offset);
        case OP_SUB:            return simpleInstruction("OP_SUB           ; eat a banana", offset);
        case OP_MUL:            return simpleInstruction("OP_MUL           ; multiply the banana bunch", offset);
        case OP_DIV:            return simpleInstruction("OP_DIV           ; share the bananas", offset);
        case OP_EQUAL:          return simpleInstruction("OP_EQUAL         ; are the banana bunches equal?", offset);
        case OP_GREATER:        return simpleInstruction("OP_GREATER       ; more bananas than the other ape", offset);
        case OP_LESS:           return simpleInstruction("OP_LESS          ; fewer bananas than the other ape", offset);
        case OP_JUMP_IF_FALSE:  return jumpInstruction("OP_JUMP_IF_FALSE ; jump if the banana is falsey", 1, bytecode, offset);
        case OP_JUMP:           return jumpInstruction("OP_JUMP          ; swing to another branch", 1, bytecode, offset);
        case OP_JUMP_BACK: {
            uint32_t target_offset;
            memcpy(&target_offset, &bytecode[offset + 1], sizeof(uint32_t));
            printf("%-16s -> %u ; swing way back\n", "OP_JUMP_BACK", target_offset);
            return offset + 1 + sizeof(uint32_t);
        }
        case OP_LOOP_START:     return simpleInstruction("OP_LOOP_START    ; begin the banana-counting dance", offset);
        case OP_PRINT:          return simpleInstruction("OP_PRINT         ; ape screeches about bananas", offset);
        case OP_ASK:            return simpleInstruction("OP_ASK           ; ask the jungle for wisdom (and input)", offset);
        case OP_GET_GLOBAL:     return globalInstruction("OP_GET_GLOBAL    ; find a banana in the jungle", bytecode, offset);
        case OP_SET_GLOBAL:     return globalInstruction("OP_SET_GLOBAL    ; place a banana in the jungle", bytecode, offset);
        case OP_GET_LOCAL:      return byteInstruction("OP_GET_LOCAL     ; grab a nearby banana", bytecode, offset);
        case OP_SET_LOCAL:      return byteInstruction("OP_SET_LOCAL     ; place a banana nearby", bytecode, offset);
        case OP_CALL:           return byteInstruction("OP_CALL          ; summon the tribe", bytecode, offset);
        case OP_RETURN:         return simpleInstruction("OP_RETURN        ; ape returns to the tribe's canopy", offset);
        case OP_BUILD_BUNCH:    return byteInstruction("OP_BUILD_BUNCH   ; gather a bunch of bananas (array)", bytecode, offset);
        case OP_BUILD_CANOPY:   return byteInstruction("OP_BUILD_CANOPY  ; build a sturdy canopy (map)", bytecode, offset);
        case OP_GET_SUBSCRIPT:  return simpleInstruction("OP_GET_SUBSCRIPT ; grab a specific banana from the bunch", offset);
        case OP_SET_SUBSCRIPT:  return simpleInstruction("OP_SET_SUBSCRIPT ; put a banana back in the bunch", offset);
        case OP_TUMBLE_SETUP:   return jumpInstruction("OP_TUMBLE_SETUP  ; prepare for a clumsy tumble (try)", 1, bytecode, offset);
        case OP_TUMBLE_END:     return simpleInstruction("OP_TUMBLE_END    ; the tumble is over, safe now", offset);
        case OP_SUMMON:         return simpleInstruction("OP_SUMMON        ; summon another ape spirit (module)", offset);
        case OP_LOOP:           return jumpInstruction("OP_LOOP          ; swing back on the vine", -1, bytecode, offset);
        default:
            printf("Unknown opcode %d\n", instruction);
            return offset + 1;
    }
}

void disassembleBytecode(const char* name, uint8_t* bytecode, long size) {
    printf("== %s: The Ape Scrolls ==\n", name);
    for (int offset = 0; offset < size; ) {
        offset = disassembleInstruction(bytecode, offset);
    }
}
