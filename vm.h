#ifndef APE_VM_H
#define APE_VM_H

#include "common.h"

// The result of a VM execution
typedef enum {
  VM_RESULT_OK,
  VM_RESULT_COMPILE_ERROR,
  VM_RESULT_RUNTIME_ERROR
} VMResult;

#define STACK_MAX 256
#define MEMORY_MAX 256
#define FRAMES_MAX 64 // Maximum recursion depth
#define HANDLER_MAX 16 // Max nested tumble blocks

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
    uint8_t* catchIp;     // Instruction pointer of the catch block
    int frameCount;       // Which call frame this handler belongs to
    Value* stackTop;      // The stack pointer to restore to
} TryHandler;

typedef struct {
    uint8_t* bytecode;
    uint8_t* ip;

    Obj* objects; 

    Value stack[STACK_MAX];
    Value* stackTop;

    CallFrame frames[FRAMES_MAX];
    int frameCount;

    TryHandler tryHandlers[HANDLER_MAX];
    int tryHandlerCount;

    double loop_counters[STACK_MAX];
    int loop_counter_top;

    Variable variables[MEMORY_MAX];
    int variableCount;
    size_t bytesAllocated;
    size_t nextGC;

} VM;
void initVM(VM* vm);
void freeVM(VM* vm);
VMResult interpret(VM* vm, const char* source);
VMResult runBytecode(const char* path);

#endif 
