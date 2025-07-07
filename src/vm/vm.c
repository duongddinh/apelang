#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../compiler/compiler.h"
#include "../debug/debug.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

static void runtimeError(VM* vm, const char* format, ...);
VMResult run(VM* vm);
static bool call(VM* vm, ObjFunction* function, int argCount);
static bool callValue(VM* vm, Value callee, int argCount);
static bool valuesEqual(Value a, Value b);
static void printValue(Value value);
static uint32_t hashString(const char* key, int length);
static bool canopySet(ObjCanopy* canopy, Value key, Value value);
static int findVariable(VM* vm, const char* name, int len);
static bool isFalsey(Value value);

static void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize);
static void markObject(Obj* object);
static void markValue(Value value);
static void collectGarbage(VM* vm);
static void freeObject(VM* vm, Obj* object);

static void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
  vm->bytesAllocated += newSize - oldSize;
  
  if (oldSize == 0 && newSize > 0) {
      vm->objectsAllocated++;
  }

  if (newSize > oldSize) {
    if (vm->bytesAllocated > vm->nextGC) {
      collectGarbage(vm);
    }
  }
  if (newSize == 0) {
    free(pointer);
    return NULL;
  }
  void* result = realloc(pointer, newSize);
  if (result == NULL) exit(1);
  return result;
}

static char* readTextFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;

    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);

    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        fclose(file);
        return NULL;
    }

    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static bool writeTextFile(const char* path, const char* content) {
    FILE* file = fopen(path, "w");
    if (file == NULL) {
        return false;
    }
    size_t contentLength = strlen(content);
    size_t bytesWritten = fwrite(content, sizeof(char), contentLength, file);
    fclose(file);
    return bytesWritten == contentLength;
}


static uint32_t hashString(const char* key, int length) {
  uint32_t hash = 2166136261u;
  for (int i = 0; i < length; i++) {
    hash ^= (uint8_t)key[i];
    hash *= 16777619;
  }
  return hash;
}

static uint32_t hashValue(Value value) {
  if (IS_STRING(value)) return AS_STRING(value)->hash;
  return 0;
}

static CanopyEntry* findCanopyEntry(CanopyEntry* entries, int capacity,
                                    Value key) {
  uint32_t index = hashValue(key) % capacity;
  for (;;) {
    CanopyEntry* entry = &entries[index];
    if (IS_NIL(entry->key) || valuesEqual(entry->key, key)) {
      return entry;
    }
    index = (index + 1) % capacity;
  }
}

static bool canopySet(ObjCanopy* canopy, Value key, Value value) {
  CanopyEntry* entry = findCanopyEntry(canopy->entries, canopy->capacity, key);
  bool isNewKey = IS_NIL(entry->key);
  if (isNewKey && IS_NIL(entry->value)) canopy->count++;
  entry->key = key;
  entry->value = value;
  return isNewKey;
}

static bool canopyGet(ObjCanopy* canopy, Value key, Value* value) {
  if (canopy->count == 0) return false;
  CanopyEntry* entry = findCanopyEntry(canopy->entries, canopy->capacity, key);
  if (IS_NIL(entry->key)) return false;
  *value = entry->value;
  return true;
}

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
    case VAL_OBJ: {
      if (IS_STRING(a) && IS_STRING(b)) {
        ObjString* aString = AS_STRING(a);
        ObjString* bString = AS_STRING(b);
        return aString->length == bString->length &&
               memcmp(aString->chars, bString->chars, aString->length) == 0;
      }
      return AS_OBJ(a) == AS_OBJ(b);
    }
  }
  return false;
}
static uint8_t* readBytecodeFile(const char* path, size_t* out_fileSize) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) return NULL;

    fseek(file, 0L, SEEK_END);
    *out_fileSize = ftell(file);
    rewind(file);

    uint8_t* buffer = (uint8_t*)malloc(*out_fileSize);
    if (buffer == NULL) {
        fprintf(stderr, "Not enough memory to read bytecode file \"%s\".\n", path);
        exit(74);
    }

    size_t bytesRead = fread(buffer, sizeof(uint8_t), *out_fileSize, file);
    if (bytesRead < *out_fileSize) {
        fprintf(stderr, "Could not read entire file \"%s\".\n", path);
        exit(74);
    }

    fclose(file);
    return buffer;
}

static bool call(VM* vm, ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError(vm, "Expected %d arguments but got %d for function %s.",
                 function->arity, argCount,
                 function->name ? function->name->chars : "<script>");
    return false;
  }
  if (vm->frameCount == FRAMES_MAX) {
    runtimeError(vm, "Stack overflow!");
    return false;
  }

  if (vm->frameCount + 1 > vm->maxFrameCount) {
      vm->maxFrameCount = vm->frameCount + 1;
  }

  CallFrame* frame = &vm->frames[vm->frameCount++];
  frame->function = function;

  ObjFunction* owner = function->owner ? function->owner : function;
  frame->ip = owner->code + function->code_offset;

  frame->slots = vm->stackTop - argCount - 1;
  return true;
}

static bool callValue(VM* vm, Value callee, int argCount) {
  if (IS_OBJ(callee) && IS_FUNCTION(callee)) {
    return call(vm, AS_FUNCTION(callee), argCount);
  }
  runtimeError(vm, "Can only call functions and tribes.");
  return false;
}

void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void markObject(Obj* object) {
  if (object == NULL || object->isMarked) return;
  object->isMarked = true;
  switch (object->type) {
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      markObject((Obj*)function->owner);
      break;
    }
    case OBJ_BUNCH: {
      ObjBunch* bunch = (ObjBunch*)object;
      for (int i = 0; i < bunch->count; i++) markValue(bunch->values[i]);
      break;
    }
    case OBJ_CANOPY: {
      ObjCanopy* canopy = (ObjCanopy*)object;
      for (int i = 0; i < canopy->capacity; i++) {
        markValue(canopy->entries[i].key);
        markValue(canopy->entries[i].value);
      }
      break;
    }
    case OBJ_STRING:
      break;
  }
}

static void markRoots(VM* vm) {
  for (Value* slot = vm->stack; slot < vm->stackTop; slot++) markValue(*slot);
  for (int i = 0; i < vm->variableCount; i++) markValue(vm->variables[i].value);
  for (int i = 0; i < vm->frameCount; i++)
    markObject((Obj*)vm->frames[i].function);
  markValue(vm->stack[STACK_MAX - 1]);
}

static void freeObject(VM* vm, Obj* object) {
  switch (object->type) {
    case OBJ_STRING: {
      ObjString* string = (ObjString*)object;
      reallocate(vm, object, sizeof(ObjString) + string->length + 1, 0);
      break;
    }
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      if (function->isModule) {
        free(function->code);
      }
      reallocate(vm, object, sizeof(ObjFunction), 0);
      break;
    }
    case OBJ_BUNCH: {
      ObjBunch* bunch = (ObjBunch*)object;
      reallocate(vm, bunch->values, sizeof(Value) * bunch->capacity, 0);
      reallocate(vm, object, sizeof(ObjBunch), 0);
      break;
    }
    case OBJ_CANOPY: {
      ObjCanopy* canopy = (ObjCanopy*)object;
      reallocate(vm, canopy->entries, sizeof(CanopyEntry) * canopy->capacity,
                 0);
      reallocate(vm, object, sizeof(ObjCanopy), 0);
      break;
    }
  }
}

static void sweep(VM* vm) {
  Obj* previous = NULL;
  Obj* object = vm->objects;
  while (object != NULL) {
    if (object->isMarked) {
      object->isMarked = false;
      previous = object;
      object = object->next;
    } else {
      Obj* unreached = object;
      object = object->next;
      if (previous != NULL)
        previous->next = object;
      else
        vm->objects = object;
      freeObject(vm, unreached);
    }
  }
}

void collectGarbage(VM* vm) {
  vm->gcCycles++;
  markRoots(vm);
  sweep(vm);
  vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;
}

VMResult run(VM* vm) {
  CallFrame* frame = &vm->frames[vm->frameCount - 1];
#define RUNTIME_ERROR(...)                                           \
  do {                                                               \
    runtimeError(vm, __VA_ARGS__);                                   \
    if (vm->tryHandlerCount > 0) {                                   \
      TryHandler* handler = &vm->tryHandlers[--vm->tryHandlerCount]; \
      vm->frameCount = handler->frameCount;                          \
      vm->stackTop = handler->stackTop;                              \
      *vm->stackTop++ = vm->stack[STACK_MAX - 1];                    \
      frame = &vm->frames[vm->frameCount - 1];                       \
      frame->ip = handler->catchIp;                                  \
      continue;                                                      \
    } else {                                                         \
      return VM_RESULT_RUNTIME_ERROR;                                \
    }                                                                \
  } while (false)
#define BINARY_OP(valueType, op)                                        \
  do {                                                                  \
    if (!IS_NUMBER(vm->stackTop[-1]) || !IS_NUMBER(vm->stackTop[-2])) { \
      RUNTIME_ERROR("Operands must be numbers.");                       \
    }                                                                   \
    double b = AS_NUMBER(*--vm->stackTop);                              \
    double a = AS_NUMBER(*--vm->stackTop);                              \
    *vm->stackTop++ = valueType(a op b);                                \
  } while (false)

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
            size_t size = sizeof(ObjString) + len + 1;
            ObjString* stringObj = (ObjString*)reallocate(vm, NULL, 0, size);
            stringObj->obj.type = OBJ_STRING;
            stringObj->length = len;
            stringObj->chars = (char*)(stringObj + 1);
            memcpy(stringObj->chars, frame->ip, len);
            stringObj->chars[len] = '\0';
            frame->ip += len;
            stringObj->hash = hashString(stringObj->chars, len);
            stringObj->obj.isMarked = false;
            stringObj->obj.next = vm->objects;
            vm->objects = (Obj*)stringObj;
            *vm->stackTop++ = OBJ_VAL(stringObj);
          } else if (objType == OBJ_FUNCTION) {
            ObjFunction* function =
                (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
            function->obj.type = OBJ_FUNCTION;
            function->arity = *frame->ip++;
            uint32_t codeAddr;
            memcpy(&codeAddr, frame->ip, sizeof(uint32_t));
            frame->ip += sizeof(uint32_t);

            function->owner = frame->function;
            function->code_offset = codeAddr;
            function->code = NULL; // This function doesn't own a code chunk.

            uint8_t nameLen = *frame->ip++;
            size_t nameSize = sizeof(ObjString) + nameLen + 1;
            ObjString* nameString =
                (ObjString*)reallocate(vm, NULL, 0, nameSize);
            nameString->obj.type = OBJ_STRING;
            nameString->length = nameLen;
            nameString->chars = (char*)(nameString + 1);
            memcpy(nameString->chars, frame->ip, nameLen);
            nameString->chars[nameLen] = '\0';
            frame->ip += nameLen;

            function->name = nameString;
            function->isModule = false;
            function->obj.isMarked = false;
            function->obj.next = vm->objects;
            vm->objects = (Obj*)function;
            nameString->obj.isMarked = false;
            nameString->obj.next = vm->objects;
            vm->objects = (Obj*)nameString;
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
        Value v = *(--vm->stackTop);
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
        if (IS_NUMBER(vm->stackTop[-1]) && IS_NUMBER(vm->stackTop[-2])) {
            double b = AS_NUMBER(*--vm->stackTop);
            double a = AS_NUMBER(*--vm->stackTop);
            *vm->stackTop++ = NUMBER_VAL(a + b);
        } else if (IS_STRING(vm->stackTop[-1]) && IS_STRING(vm->stackTop[-2])) {
            // Peek at the stack, don't pop. This keeps them safe from the GC.
            ObjString* b = AS_STRING(vm->stackTop[-1]);
            ObjString* a = AS_STRING(vm->stackTop[-2]);

            int length = a->length + b->length;
            size_t size = sizeof(ObjString) + length + 1;
            
            // Allocation might trigger GC. 'a' and 'b' are safe on the stack.
            ObjString* result = (ObjString*)reallocate(vm, NULL, 0, size);
            result->obj.type = OBJ_STRING;
            result->length = length;
            result->chars = (char*)(result + 1);
            memcpy(result->chars, a->chars, a->length);
            memcpy(result->chars + a->length, b->chars, b->length);
            result->chars[length] = '\0';
            
            result->hash = hashString(result->chars, length);
            result->obj.isMarked = false;
            result->obj.next = vm->objects;
            vm->objects = (Obj*)result;

            // Now that the new string is created, pop the operands and push the result.
            vm->stackTop -= 2;
            *vm->stackTop++ = OBJ_VAL(result);
        } else {
            RUNTIME_ERROR("Operands must be two numbers or two strings.");
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
      case OP_LOOP: {
        uint16_t offset = (uint16_t)(frame->ip[0] << 8 | frame->ip[1]);
        frame->ip += 2;
        frame->ip -= offset;
        break;
      }
      case OP_LOOP_START:
        vm->loop_counters[vm->loop_counter_top++] = AS_NUMBER(*--vm->stackTop);
        break;
      case OP_JUMP_BACK: {
        uint32_t target_offset;
        memcpy(&target_offset, frame->ip, sizeof(uint32_t));
        vm->loop_counters[vm->loop_counter_top - 1]--;
        if (vm->loop_counters[vm->loop_counter_top - 1] > 0) {
          ObjFunction* owner = frame->function->owner ? frame->function->owner : frame->function;
          frame->ip = owner->code + target_offset;
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
        if (!fgets(line, sizeof(line), stdin)) {
          *vm->stackTop++ = NIL_VAL;
          break;
        }

        line[strcspn(line, "\r\n")] = 0;

        if (line[0] == '\0') {
          *vm->stackTop++ = NIL_VAL;
          break;
        }

        char* end;
        double value = strtod(line, &end);

        if (*end == '\0') {
          *vm->stackTop++ = NUMBER_VAL(value);
        } else {
          int len = strlen(line);
          size_t size = sizeof(ObjString) + len + 1;
          ObjString* obj = (ObjString*)reallocate(vm, NULL, 0, size);
          obj->obj.type = OBJ_STRING;
          obj->length = len;
          obj->chars = (char*)(obj + 1);
          memcpy(obj->chars, line, len + 1);
          obj->hash = hashString(obj->chars, len);
          obj->obj.isMarked = false;
          obj->obj.next = vm->objects;
          vm->objects = (Obj*)obj;
          *vm->stackTop++ = OBJ_VAL(obj);
        }
        break;
      }
      case OP_GET_LOCAL:
        *vm->stackTop++ = frame->slots[*frame->ip++];
        break;
      case OP_SET_LOCAL:
        frame->slots[*frame->ip++] = vm->stackTop[-1];
        break;
      case OP_GET_GLOBAL: {
        uint8_t len = *frame->ip++;
        char name[256];
        memcpy(name, frame->ip, len);
        frame->ip += len;
        int index = findVariable(vm, name, len);
        if (index == -1) {
          RUNTIME_ERROR("Undefined variable '%.*s'.", len, name);
        } else {
          *vm->stackTop++ = vm->variables[index].value;
        }
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
      case OP_BUILD_BUNCH: {
        uint8_t itemCount = *frame->ip++;
        ObjBunch* bunch = (ObjBunch*)reallocate(vm, NULL, 0, sizeof(ObjBunch));
        bunch->obj.type = OBJ_BUNCH;
        bunch->values =
            (Value*)reallocate(vm, NULL, 0, sizeof(Value) * itemCount);
        bunch->count = itemCount;
        bunch->capacity = itemCount;
        memcpy(bunch->values, vm->stackTop - itemCount,
               sizeof(Value) * itemCount);
        vm->stackTop -= itemCount;
        bunch->obj.isMarked = false;
        bunch->obj.next = vm->objects;
        vm->objects = (Obj*)bunch;
        *vm->stackTop++ = OBJ_VAL(bunch);
        break;
      }
      case OP_BUILD_CANOPY: {
        uint8_t itemCount = *frame->ip++;
        ObjCanopy* canopy =
            (ObjCanopy*)reallocate(vm, NULL, 0, sizeof(ObjCanopy));
        canopy->obj.type = OBJ_CANOPY;
        canopy->count = 0;
        canopy->capacity = itemCount > 0 ? itemCount * 2 : 8;
        canopy->entries = (CanopyEntry*)reallocate(
            vm, NULL, 0, sizeof(CanopyEntry) * canopy->capacity);
        for (int i = 0; i < canopy->capacity; i++) {
          canopy->entries[i].key = NIL_VAL;
          canopy->entries[i].value = NIL_VAL;
        }
        for (int i = 0; i < itemCount; i++) {
          Value value = vm->stackTop[-1];
          Value key = vm->stackTop[-2];
          vm->stackTop -= 2;
          canopySet(canopy, key, value);
        }
        canopy->obj.isMarked = false;
        canopy->obj.next = vm->objects;
        vm->objects = (Obj*)canopy;
        *vm->stackTop++ = OBJ_VAL(canopy);
        break;
      }
      case OP_GET_SUBSCRIPT: {
        Value index = *--vm->stackTop;
        Value collection = *--vm->stackTop;
        if (IS_BUNCH(collection)) {
          ObjBunch* bunch = AS_BUNCH(collection);
          if (!IS_NUMBER(index)) RUNTIME_ERROR("Bunch index must be a number.");
          int i = (int)AS_NUMBER(index);
          if (i < 0 || i >= bunch->count)
            *vm->stackTop++ = NIL_VAL;
          else
            *vm->stackTop++ = bunch->values[i];
        } else if (IS_CANOPY(collection)) {
          ObjCanopy* canopy = AS_CANOPY(collection);
          if (!IS_STRING(index)) RUNTIME_ERROR("Canopy keys must be strings.");
          Value value;
          if (canopyGet(canopy, index, &value))
            *vm->stackTop++ = value;
          else
            *vm->stackTop++ = NIL_VAL;
        } else {
          RUNTIME_ERROR(
              "Subscript operator can only be used on bunches and canopies.");
        }
        break;
      }
      case OP_SET_SUBSCRIPT: {
        Value value = *--vm->stackTop;
        Value index = *--vm->stackTop;
        Value collection = *--vm->stackTop;
        if (IS_BUNCH(collection)) {
          ObjBunch* bunch = AS_BUNCH(collection);
          if (!IS_NUMBER(index)) RUNTIME_ERROR("Bunch index must be a number.");
          int i = (int)AS_NUMBER(index);
          if (i < 0 || i >= bunch->count)
            RUNTIME_ERROR("Bunch index out of bounds.");
          bunch->values[i] = value;
          *vm->stackTop++ = value;
        } else if (IS_CANOPY(collection)) {
          ObjCanopy* canopy = AS_CANOPY(collection);
          if (!IS_STRING(index)) RUNTIME_ERROR("Canopy keys must be strings.");
          canopySet(canopy, index, value);
          *vm->stackTop++ = value;
        } else {
          RUNTIME_ERROR(
              "Subscript operator can only be used on bunches and canopies.");
        }
        break;
      }
      case OP_CALL: {
        uint8_t argCount = *frame->ip++;
        if (!callValue(vm, vm->stackTop[-1 - argCount], argCount)) {
          if (vm->tryHandlerCount > 0) {
            TryHandler* handler = &vm->tryHandlers[--vm->tryHandlerCount];
            vm->frameCount = handler->frameCount;
            vm->stackTop = handler->stackTop;
            *vm->stackTop++ = vm->stack[STACK_MAX - 1];
            frame = &vm->frames[vm->frameCount - 1];
            frame->ip = handler->catchIp;
            continue;
          } else {
            return VM_RESULT_RUNTIME_ERROR;
          }
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
      case OP_TUMBLE_SETUP: {
        if (vm->tryHandlerCount == HANDLER_MAX)
          RUNTIME_ERROR("Exceeded maximum nested tumble blocks.");
        uint16_t offset = (uint16_t)(frame->ip[0] << 8 | frame->ip[1]);
        frame->ip += 2;
        TryHandler* handler = &vm->tryHandlers[vm->tryHandlerCount++];
        handler->catchIp = frame->ip + offset;
        handler->frameCount = vm->frameCount;
        handler->stackTop = vm->stackTop;
        break;
      }
      case OP_TUMBLE_END:
        vm->tryHandlerCount--;
        break;
      case OP_FORAGE: {
        Value pathValue = *--vm->stackTop;
        if (!IS_STRING(pathValue)) {
            RUNTIME_ERROR("'forage' path must be a string.");
        }
        char* path = AS_CSTRING(pathValue);
        char* content = readTextFile(path);

        if (content == NULL) {
            *vm->stackTop++ = NIL_VAL; // Push nil on failure
        } else {
            int len = strlen(content);
            size_t size = sizeof(ObjString) + len + 1;
            ObjString* obj = (ObjString*)reallocate(vm, NULL, 0, size);
            obj->obj.type = OBJ_STRING;
            obj->length = len;
            obj->chars = (char*)(obj + 1);
            memcpy(obj->chars, content, len + 1);
            obj->hash = hashString(obj->chars, len);
            obj->obj.isMarked = false;
            obj->obj.next = vm->objects;
            vm->objects = (Obj*)obj;
            *vm->stackTop++ = OBJ_VAL(obj);
            free(content);
        }
        break;
      }
      case OP_INSCRIBE: {
        Value contentValue = *--vm->stackTop;
        Value pathValue = *--vm->stackTop;
        if (!IS_STRING(pathValue) || !IS_STRING(contentValue)) {
            RUNTIME_ERROR("'inscribe' arguments must be strings.");
        }
        char* path = AS_CSTRING(pathValue);
        char* content = AS_CSTRING(contentValue);

        bool success = writeTextFile(path, content);
        *vm->stackTop++ = BOOL_VAL(success);
        break;
      }
       case OP_SUMMON: {
        Value pathValue = *--vm->stackTop;
        if (!IS_STRING(pathValue)) {
          RUNTIME_ERROR("summon path must be a string.");
        }
        
        char* ape_path = AS_CSTRING(pathValue);
        int path_len = strlen(ape_path);
        if (path_len <= 4 || strcmp(ape_path + path_len - 4, ".ape") != 0) {
            RUNTIME_ERROR("Summon path must end in .ape");
        }
        char apb_path[1024];
        strncpy(apb_path, ape_path, path_len - 4);
        apb_path[path_len - 4] = '\0';
        strcat(apb_path, ".apb");

        // Read the bytecode from the .apb file.
        size_t bytecode_size = 0;
        uint8_t* bytecode_buffer = readBytecodeFile(apb_path, &bytecode_size);
        if (bytecode_buffer == NULL) {
            RUNTIME_ERROR("Cannot open or read module file '%s'. Compile it first.", apb_path);
        }
        
        // Create the module function that will own this bytecode.
        ObjFunction* moduleFunc =
            (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
        moduleFunc->obj.type = OBJ_FUNCTION;
        moduleFunc->arity = 0;
        moduleFunc->name = AS_STRING(pathValue);

        // This function owns the new bytecode chunk.
        moduleFunc->code = bytecode_buffer;
        moduleFunc->isModule = true; // This tells the GC to free the code buffer later.
        
        moduleFunc->obj.isMarked = false;
        moduleFunc->obj.next = vm->objects;
        vm->objects = (Obj*)moduleFunc;
        
        if (!call(vm, moduleFunc, 0)) {
            return VM_RESULT_RUNTIME_ERROR;
        }
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }

      case 255:
        return VM_RESULT_OK;
      default:
        RUNTIME_ERROR("Unknown opcode %d\n", instruction);
    }
  }
}

static void runtimeError(VM* vm, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  fprintf(stderr, "Runtime Error: %s", buffer);
  for (int i = vm->frameCount - 1; i >= 0; i--) {
    CallFrame* frame = &vm->frames[i];
    ObjFunction* function = frame->function;
    fprintf(stderr, "\n[line ?] in %s()",
            function->name ? function->name->chars : "<script>");
  }
  int len = strlen(buffer);
  size_t size = sizeof(ObjString) + len + 1;
  ObjString* errObj = (ObjString*)reallocate(vm, NULL, 0, size);
  errObj->obj.type = OBJ_STRING;
  errObj->length = len;
  errObj->chars = (char*)(errObj + 1);
  memcpy(errObj->chars, buffer, len + 1);
  errObj->hash = hashString(errObj->chars, len);
  errObj->obj.isMarked = false;
  errObj->obj.next = vm->objects;
  vm->objects = (Obj*)errObj;
  vm->stack[STACK_MAX - 1] = OBJ_VAL(errObj);
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
      switch (OBJ_TYPE(value)) {
        case OBJ_STRING:
          printf("%s", AS_CSTRING(value));
          break;
        case OBJ_FUNCTION:
          if (AS_FUNCTION(value)->name == NULL)
            printf("<script>");
          else
            printf("<tribe %s>", AS_FUNCTION(value)->name->chars);
          break;
        case OBJ_BUNCH: {
          ObjBunch* bunch = AS_BUNCH(value);
          printf("[");
          for (int i = 0; i < bunch->count; i++) {
            printValue(bunch->values[i]);
            if (i < bunch->count - 1) printf(", ");
          }
          printf("]");
          break;
        }
        case OBJ_CANOPY: {
          ObjCanopy* canopy = AS_CANOPY(value);
          printf("{");
          int printed = 0;
          for (int i = 0; i < canopy->capacity; i++) {
            if (!IS_NIL(canopy->entries[i].key)) {
              printValue(canopy->entries[i].key);
              printf(": ");
              printValue(canopy->entries[i].value);
              if (printed < canopy->count - 1) printf(", ");
              printed++;
            }
          }
          printf("}");
          break;
        }
      }
      break;
  }
}

void initVM(VM* vm) {
  vm->stackTop = vm->stack;
  vm->frameCount = 0;
  vm->variableCount = 0;
  vm->tryHandlerCount = 0;
  vm->loop_counter_top = 0;
  vm->objects = NULL;
  vm->bytesAllocated = 0;
  vm->nextGC = 1024 * 1024;
  vm->bytecode = NULL;

  vm->maxFrameCount = 0;
  vm->objectsAllocated = 0;
  vm->gcCycles = 0;
}

void freeVM(VM* vm) {
  while (vm->objects) {
    Obj* obj = vm->objects;
    vm->objects = obj->next;
    freeObject(vm, obj);
  }
  for (int i = 0; i < vm->variableCount; i++) free(vm->variables[i].name);
  free(vm->bytecode);
}

VMResult interpret(VM* vm, const char* source) {
  uint8_t* bytecode_buffer = NULL;
  size_t bytecode_size = 0;
  FILE* mem_file = open_memstream((char**)&bytecode_buffer, &bytecode_size);
  if (!mem_file) {
    fprintf(stderr, "Failed to open memory stream for compilation.\n");
    return VM_RESULT_RUNTIME_ERROR;
  }

  if (!compile(source, mem_file, true)) {
    fclose(mem_file);
    free(bytecode_buffer);
    return VM_RESULT_COMPILE_ERROR;
  }

  fflush(mem_file);

  ObjFunction* function =
      (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
  function->obj.type = OBJ_FUNCTION;
  function->arity = 0;
  function->name = NULL;

  function->code = bytecode_buffer;
  function->owner = NULL;
  function->code_offset = 0;
  function->isModule = true; 

  *vm->stackTop++ = OBJ_VAL(function);
  call(vm, function, 0);
  
  if (vm->frameCount > vm->maxFrameCount) {
      vm->maxFrameCount = vm->frameCount;
  }

  VMResult result = run(vm);
  fclose(mem_file);

  return result;
}

VMResult runBytecode(VM* vm, const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    return VM_RESULT_RUNTIME_ERROR;
  }

  fseek(file, 0L, SEEK_END);
  long fileSize = ftell(file);
  rewind(file);
  vm->bytecode = (uint8_t*)malloc(fileSize + 1);
  if (vm->bytecode == NULL) exit(74);
  fread(vm->bytecode, sizeof(uint8_t), fileSize, file);
  fclose(file);
  vm->bytecode[fileSize] = 255;

  ObjFunction* topLevelFunc =
      (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
  memset(topLevelFunc, 0, sizeof(ObjFunction));
  topLevelFunc->obj.type = OBJ_FUNCTION;
  topLevelFunc->arity = 0;
  
  topLevelFunc->code = vm->bytecode;
  topLevelFunc->owner = NULL;
  topLevelFunc->code_offset = 0;
  topLevelFunc->isModule = false; 

  const char* script_name_literal = "script";
  int name_len = strlen(script_name_literal);
  size_t nameSize = sizeof(ObjString) + name_len + 1;
  ObjString* nameString = (ObjString*)reallocate(vm, NULL, 0, nameSize);
  nameString->obj.type = OBJ_STRING;
  nameString->length = name_len;
  nameString->chars = (char*)(nameString + 1);
  strcpy(nameString->chars, script_name_literal);
  topLevelFunc->name = nameString;

  topLevelFunc->obj.isMarked = false;
  topLevelFunc->obj.next = vm->objects;
  vm->objects = (Obj*)topLevelFunc;
  nameString->obj.isMarked = false;
  nameString->obj.next = vm->objects;
  vm->objects = (Obj*)nameString;
  vm->frameCount = 1;

  if (vm->frameCount > vm->maxFrameCount) {
      vm->maxFrameCount = vm->frameCount;
  }

  CallFrame* frame = &vm->frames[0];
  frame->function = topLevelFunc;
  frame->ip = vm->bytecode;
  frame->slots = vm->stack;
  printf("🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴\n");
  printf("ApesLang VM Output\n");
  VMResult result = run(vm);
  return result;
}