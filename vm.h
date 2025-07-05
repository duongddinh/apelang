#ifndef APE_VM_H
#define APE_VM_H

#include "common.h"

// The result of a VM execution
typedef enum {
  VM_RESULT_OK,
  VM_RESULT_RUNTIME_ERROR
} VMResult;

#define STACK_MAX 256
#define MEMORY_MAX 256
#define FRAMES_MAX 64 // Maximum recursion depth

typedef struct {
    ObjFunction* function;
    uint8_t* ip;      
    Value* slots;      // Points to the first stack slot this function can use
} CallFrame;

typedef struct {
    char* name;
    int nameLen;
    Value value;
} Variable;

typedef struct {
    uint8_t* bytecode;
    uint8_t* ip;

    Value stack[STACK_MAX];
    Value* stackTop;

    CallFrame frames[FRAMES_MAX];
    int frameCount;

    double loop_counters[STACK_MAX];
    int loop_counter_top;

    Variable variables[MEMORY_MAX];
    int variableCount;

} VM;

VMResult runBytecode(const char* path);

#endif 
