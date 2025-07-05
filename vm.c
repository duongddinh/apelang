#include <stdio.h>
#include <stdlib.h>  // For malloc
#include <string.h>

#include "vm.h"

void printValue(Value value);
static VMResult run(VM* vm);
static bool callValue(VM* vm, Value callee, int argCount);

static bool isFalsey(Value value) {
  return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}
static int findVariable(VM* vm, const char* name, int len) {
  for (int i = 0; i < vm->variableCount; i++) {
    if (vm->variables[i].nameLen == len &&
        memcmp(vm->variables[i].name, name, len) == 0) {
      return i;
    }
  }
  return -1;
}
static bool valuesEqual(Value a, Value b) {
  if (a.type != b.type) return false;
  switch (a.type) {
    case VAL_BOOL:
      return AS_BOOL(a) == AS_BOOL(b);
    case VAL_NIL:
      return true;
    case VAL_NUMBER:
      return AS_NUMBER(a) == AS_NUMBER(b);
    case VAL_OBJ:
      return AS_OBJ(a) == AS_OBJ(b);  // Objects are compared by reference
  }
  return false;
}

static bool call(VM* vm, ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    fprintf(
        stderr,
        "Runtime Error: Expected %d arguments but got %d for function %s.\n",
        function->arity, argCount, function->name->chars);
    return false;
  }
  if (vm->frameCount == FRAMES_MAX) {
    fprintf(stderr, "Runtime Error: Stack overflow!\n");
    return false;
  }

  CallFrame* frame = &vm->frames[vm->frameCount++];
  frame->function = function;
  frame->ip = function->code;
  frame->slots = vm->stackTop - argCount - 1;
  return true;
}
static bool callValue(VM* vm, Value callee, int argCount) {
  if (IS_OBJ(callee) && IS_FUNCTION(callee)) {
    return call(vm, AS_FUNCTION(callee), argCount);
  }
  fprintf(stderr, "Runtime Error: Can only call functions and tribes.\n");
  return false;
}

#define BINARY_OP(valueType, op)                                        \
  do {                                                                  \
    if (!IS_NUMBER(vm->stackTop[-1]) || !IS_NUMBER(vm->stackTop[-2])) { \
      fprintf(stderr, "Runtime Error: Operands must be numbers.\n");    \
      return VM_RESULT_RUNTIME_ERROR;                                   \
    }                                                                   \
    double b = AS_NUMBER(*--vm->stackTop);                              \
    double a = AS_NUMBER(*--vm->stackTop);                              \
    *vm->stackTop++ = valueType(a op b);                                \
  } while (false)

static VMResult run(VM* vm) {
  CallFrame* frame = &vm->frames[vm->frameCount - 1];

  for (;;) {
    uint8_t instruction = *frame->ip++;
    switch (instruction) {
      case OP_PUSH: {
        ValueType type = (ValueType)*frame->ip++;
        if (type == VAL_NUMBER) {
          double num;
          memcpy(&num, frame->ip, sizeof(double));
          *vm->stackTop++ = NUMBER_VAL(num);
          frame->ip += sizeof(double);
        } else if (type == VAL_OBJ) {
          ObjType objType = (ObjType)*frame->ip++;
          if (objType == OBJ_STRING) {
            uint8_t len = *frame->ip++;
            char* str = (char*)malloc(len + 1);
            memcpy(str, frame->ip, len);
            str[len] = '\0';
            frame->ip += len;
            ObjString* stringObj = (ObjString*)malloc(sizeof(ObjString));
            stringObj->obj.type = OBJ_STRING;
            stringObj->chars = str;
            stringObj->length = len;
            *vm->stackTop++ = OBJ_VAL(stringObj);
          } else if (objType == OBJ_FUNCTION) {
            ObjFunction* function = (ObjFunction*)malloc(sizeof(ObjFunction));
            function->obj.type = OBJ_FUNCTION;

            function->arity = *frame->ip++;

            uint32_t codeAddr;
            memcpy(&codeAddr, frame->ip, sizeof(uint32_t));
            function->code = vm->bytecode + codeAddr;
            frame->ip += sizeof(uint32_t);

            uint8_t nameLen = *frame->ip++;
            char* nameChars = (char*)malloc(nameLen + 1);
            memcpy(nameChars, frame->ip, nameLen);
            nameChars[nameLen] = '\0';
            frame->ip += nameLen;

            ObjString* nameString = (ObjString*)malloc(sizeof(ObjString));
            nameString->obj.type = OBJ_STRING;
            nameString->length = nameLen;
            nameString->chars = nameChars;
            function->name = nameString;

            *vm->stackTop++ = OBJ_VAL(function);
          }
        }
        break;
      }
      case OP_POP:
        --vm->stackTop;
        break;
      case OP_NIL:
        *vm->stackTop++ = NIL_VAL;
        break;
      case OP_TRUE:
        *vm->stackTop++ = BOOL_VAL(true);
        break;
      case OP_FALSE:
        *vm->stackTop++ = BOOL_VAL(false);
        break;
      case OP_NOT: {
        //pop the operand 
        Value v = *(--vm->stackTop);
        // push the negation 
        *vm->stackTop++ = BOOL_VAL(isFalsey(v));
        break;
      }
      case OP_EQUAL: {
        Value b = *--vm->stackTop;
        Value a = *--vm->stackTop;
        *vm->stackTop++ = BOOL_VAL(valuesEqual(a, b));
        break;
      }
      case OP_GREATER:
        BINARY_OP(BOOL_VAL, >);
        break;
      case OP_LESS:
        BINARY_OP(BOOL_VAL, <);
        break;
      case OP_ADD: {
        Value b = vm->stackTop[-1];
        Value a = vm->stackTop[-2];
        if (IS_NUMBER(a) && IS_NUMBER(b)) {
          vm->stackTop -= 2;
          *vm->stackTop++ = NUMBER_VAL(AS_NUMBER(a) + AS_NUMBER(b));
        } else if (IS_STRING(a) && IS_STRING(b)) {
          vm->stackTop -= 2;
          int lenA = AS_STRING(a)->length;
          int lenB = AS_STRING(b)->length;
          int newLen = lenA + lenB;
          char* newChars = (char*)malloc(newLen + 1);
          memcpy(newChars, AS_CSTRING(a), lenA);
          memcpy(newChars + lenA, AS_CSTRING(b), lenB);
          newChars[newLen] = '\0';
          ObjString* newString = (ObjString*)malloc(sizeof(ObjString));
          newString->obj.type = OBJ_STRING;
          newString->chars = newChars;
          newString->length = newLen;
          *vm->stackTop++ = OBJ_VAL(newString);
        } else {
          fprintf(
              stderr,
              "Runtime Error: Operands must be two numbers or two strings.\n");
          return VM_RESULT_RUNTIME_ERROR;
        }
        break;
      }
      case OP_SUB:
        BINARY_OP(NUMBER_VAL, -);
        break;
      case OP_MUL:
        BINARY_OP(NUMBER_VAL, *);
        break;
      case OP_DIV:
        BINARY_OP(NUMBER_VAL, /);
        break;
      case OP_JUMP_IF_FALSE: {
        uint16_t offset = (uint16_t)(frame->ip[0] << 8 | frame->ip[1]);
        frame->ip += 2;
        if (isFalsey(vm->stackTop[-1])) frame->ip += offset;
        break;
      }
      case OP_JUMP: {
        uint16_t offset = (uint16_t)(frame->ip[0] << 8 | frame->ip[1]);
        frame->ip += 2 + offset;
        break;
      }
      case OP_LOOP_START: {
        vm->loop_counters[vm->loop_counter_top++] = AS_NUMBER(*--vm->stackTop);
        break;
      }
      case OP_JUMP_BACK: {
        uint32_t target_offset;
        memcpy(&target_offset, frame->ip, sizeof(uint32_t));
        vm->loop_counters[vm->loop_counter_top - 1]--;
        if (vm->loop_counters[vm->loop_counter_top - 1] > 0) {
          frame->ip = vm->bytecode + target_offset;
        } else {
          vm->loop_counter_top--;
          frame->ip += sizeof(uint32_t);
        }
        break;
      }
      case OP_PRINT: {
        printValue(*--vm->stackTop);
        printf("\n");
        break;
      }
      case OP_ASK: {
        char line[1024];
        if (fgets(line, sizeof(line), stdin)) {
          line[strcspn(line, "\r\n")] = 0;
          int len = strlen(line);
          char* str = (char*)malloc(len + 1);
          memcpy(str, line, len);
          str[len] = '\0';
          ObjString* stringObj = (ObjString*)malloc(sizeof(ObjString));
          stringObj->obj.type = OBJ_STRING;
          stringObj->chars = str;
          stringObj->length = len;
          *vm->stackTop++ = OBJ_VAL(stringObj);
        } else {
          *vm->stackTop++ = NIL_VAL;
        }
        break;
      }

      case OP_GET_LOCAL: {
        uint8_t slot = *frame->ip++;
        *vm->stackTop++ = frame->slots[slot];
        break;
      }
      case OP_SET_LOCAL: {
        uint8_t slot = *frame->ip++;
        frame->slots[slot] = vm->stackTop[-1];
        break;
      }
      case OP_GET_GLOBAL: {
        uint8_t len = *frame->ip++;
        char name[256];
        memcpy(name, frame->ip, len);
        frame->ip += len;
        int index = findVariable(vm, name, len);
        if (index == -1) {
          fprintf(stderr, "Runtime Error: Undefined variable '%.*s'.\n", len,
                  name);
          return VM_RESULT_RUNTIME_ERROR;
        }
        *vm->stackTop++ = vm->variables[index].value;
        break;
      }
      case OP_SET_GLOBAL: {
        uint8_t len = *frame->ip++;
        char name[256];
        memcpy(name, frame->ip, len);
        name[len] = '\0';
        frame->ip += len;
        int index = findVariable(vm, name, len);
        if (index == -1) {
          index = vm->variableCount++;
          vm->variables[index].name = (char*)malloc(len + 1);
          strcpy(vm->variables[index].name, name);
          vm->variables[index].nameLen = len;
        }
        vm->variables[index].value = vm->stackTop[-1];
        break;
      }
      case OP_CALL: {
        uint8_t argCount = *frame->ip++;
        if (!callValue(vm, vm->stackTop[-1 - argCount], argCount)) {
          return VM_RESULT_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }
      case OP_RETURN: {
        Value result = *--vm->stackTop;
        vm->frameCount--;
        if (vm->frameCount == 0) {
          --vm->stackTop;
          return VM_RESULT_OK;
        }
        vm->stackTop = frame->slots;
        *vm->stackTop++ = result;
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }
      case 255:
        return VM_RESULT_OK;
      default:
        fprintf(stderr, "Runtime Error: Unknown opcode %d\n", instruction);
        return VM_RESULT_RUNTIME_ERROR;
    }
  }
}

VMResult runBytecode(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) return VM_RESULT_RUNTIME_ERROR;

  VM vm = {0};
  vm.stackTop = vm.stack;

  fseek(file, 0L, SEEK_END);
  long fileSize = ftell(file);
  rewind(file);

  vm.bytecode = (uint8_t*)malloc(fileSize + 1);
  fread(vm.bytecode, sizeof(uint8_t), fileSize, file);
  fclose(file);
  vm.bytecode[fileSize] = 255;

  vm.ip = vm.bytecode;

  vm.frameCount = 1;
  CallFrame* frame = &vm.frames[0];
  ObjFunction* topLevelFunc = (ObjFunction*)malloc(sizeof(ObjFunction));
  topLevelFunc->arity = 0;
  topLevelFunc->code = vm.bytecode;
  char* script_name = "script";
  ObjString* nameString = (ObjString*)malloc(sizeof(ObjString));
  nameString->obj.type = OBJ_STRING;
  nameString->length = strlen(script_name);
  nameString->chars = script_name;
  topLevelFunc->name = nameString;

  frame->function = topLevelFunc;
  frame->ip = vm.bytecode;
  frame->slots = vm.stack;
  printf("🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴");
  printf("ApesLang VM Output\n");
  VMResult result = run(&vm);

  free(topLevelFunc->name); 
  free(topLevelFunc);
  free(vm.bytecode);
  return result;
}

void printValue(Value value) {
  switch (value.type) {
    case VAL_BOOL:
      printf(AS_BOOL(value) ? "true" : "false");
      break;
    case VAL_NIL:
      printf("nil");
      break;
    case VAL_NUMBER:
      printf("%g", AS_NUMBER(value));
      break;
    case VAL_OBJ:
      if (IS_STRING(value)) printf("%s", AS_CSTRING(value));
      if (IS_FUNCTION(value))
        printf("<tribe %s>", AS_FUNCTION(value)->name->chars);
      break;
  }
}
