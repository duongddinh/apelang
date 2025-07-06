#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compiler.h"
#include "vm.h"

#define GC_HEAP_GROW_FACTOR 2

static void runtimeError(VM* vm, const char* format, ...);
static VMResult run(VM* vm);
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

static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) return NULL;
  fseek(file, 0L, SEEK_END);
  size_t fileSize = ftell(file);
  rewind(file);
  char* buffer = (char*)malloc(fileSize + 1);
  if (buffer == NULL) {
    fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
    exit(74);
  }
  size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
  buffer[bytesRead] = '\0';
  fclose(file);
  return buffer;
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

static bool call(VM* vm, ObjFunction* function, int argCount) {
  if (argCount != function->arity) {
    runtimeError(vm, "Expected %d arguments but got %d for function %s.",
                 function->arity, argCount, function->name->chars);
    return false;
  }
  if (vm->frameCount == FRAMES_MAX) {
    runtimeError(vm, "Stack overflow!");
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
  runtimeError(vm, "Can only call functions and tribes.");
  return false;
}


void markValue(Value value) {
  if (IS_OBJ(value)) markObject(AS_OBJ(value));
}

void markObject(Obj* object) {
  if (object == NULL || object->isMarked) {
    return;
  }
  object->isMarked = true;

  switch (object->type) {
    case OBJ_FUNCTION: {
      ObjFunction* function = (ObjFunction*)object;
      markObject((Obj*)function->name);
      break;
    }
    case OBJ_BUNCH: {
      ObjBunch* bunch = (ObjBunch*)object;
      for (int i = 0; i < bunch->count; i++) {
        markValue(bunch->values[i]);
      }
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
  for (Value* slot = vm->stack; slot < vm->stackTop; slot++) {
    markValue(*slot);
  }
  for (int i = 0; i < vm->variableCount; i++) {
    markValue(vm->variables[i].value);
  }
  for (int i = 0; i < vm->frameCount; i++) {
    markObject((Obj*)vm->frames[i].function);
  }
  // Mark the pending error object so it doesn't get collected
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
        // The bytecode buffer from open_memstream must be freed manually
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
      if (previous != NULL) {
        previous->next = object;
      } else {
        vm->objects = object;
      }
      freeObject(vm, unreached);
    }
  }
}

void collectGarbage(VM* vm) {
  markRoots(vm);
  sweep(vm);
  vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR;
}


static VMResult run(VM* vm) {
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
            //function->code = vm->bytecode + codeAddr;
            function->code = frame->function->code + codeAddr;
            frame->ip += sizeof(uint32_t);
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

            function->obj.isMarked = false;
            function->obj.next = vm->objects;
            vm->objects = (Obj*)function;

            nameString->obj.isMarked = false;
            nameString->obj.next = vm->objects;
            vm->objects = (Obj*)nameString;
            //function->isModule = frame->function->isModule;
            function->isModule = false;
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

          size_t newSize = sizeof(ObjString) + newLen + 1;
          ObjString* newString = (ObjString*)reallocate(vm, NULL, 0, newSize);
          newString->obj.type = OBJ_STRING;
          newString->length = newLen;
          newString->chars = (char*)(newString + 1);

          memcpy(newString->chars, AS_CSTRING(a), lenA);
          memcpy(newString->chars + lenA, AS_CSTRING(b), lenB);
          newString->chars[newLen] = '\0';
          newString->hash = hashString(newString->chars, newLen);

          newString->obj.isMarked = false;
          newString->obj.next = vm->objects;
          vm->objects = (Obj*)newString;

          *vm->stackTop++ = OBJ_VAL(newString);
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

        if (!fgets(line, sizeof line, stdin)) {
          *vm->stackTop++ = NIL_VAL;
          break;
        }
        line[strcspn(line, "\r\n")] = 0;

        char* p = line;
        if (*p == '-') ++p;
        bool isNumber = (*p != '\0');
        while (*p && isNumber) {
          isNumber &= isdigit(*p++);
        }

        if (isNumber) {
          double num = strtod(line, NULL);
          *vm->stackTop++ = NUMBER_VAL(num);
        } else {
          int len = strlen(line);
          size_t size = sizeof(ObjString) + len + 1;
          ObjString* obj = (ObjString*)reallocate(vm, NULL, 0, size);
          obj->obj.type = OBJ_STRING;
          obj->length = len;
          obj->chars = (char*)(obj + 1);
          memcpy(obj->chars, line, len + 1);

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
          // Global variable names are not garbage collected
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
          if (i < 0 || i >= bunch->count) {
            *vm->stackTop++ = NIL_VAL;
          } else {
            *vm->stackTop++ = bunch->values[i];
          }
        } else if (IS_CANOPY(collection)) {
          ObjCanopy* canopy = AS_CANOPY(collection);
          if (!IS_STRING(index)) RUNTIME_ERROR("Canopy keys must be strings.");
          Value value;
          if (canopyGet(canopy, index, &value)) {
            *vm->stackTop++ = value;
          } else {
            *vm->stackTop++ = NIL_VAL;
          }
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
      case OP_TUMBLE_END: {
        vm->tryHandlerCount--;
        break;
      }
      case OP_SUMMON: {
        Value pathValue = *--vm->stackTop;
        if (!IS_STRING(pathValue))
          RUNTIME_ERROR("summon path must be a string.");
        char* source = readFile(AS_CSTRING(pathValue));
        if (source == NULL)
          RUNTIME_ERROR("Could not open summon file '%s'.",
                        AS_CSTRING(pathValue));

        uint8_t* bytecode_buffer = NULL;
        size_t bytecode_size = 0;
        FILE* mem_file =
            open_memstream((char**)&bytecode_buffer, &bytecode_size);

        if (!compile(source, mem_file)) {
          free(source);
          fclose(mem_file);
          free(bytecode_buffer);
          RUNTIME_ERROR("Error compiling summoned file '%s'.",
                        AS_CSTRING(pathValue));
        }
        free(source);
        fflush(mem_file);

        ObjFunction* moduleFunc =
            (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
        moduleFunc->obj.type = OBJ_FUNCTION;
        moduleFunc->arity = 0;
        moduleFunc->code = bytecode_buffer;
        moduleFunc->name = AS_STRING(pathValue);
        moduleFunc->isModule = true;

        moduleFunc->obj.isMarked = false;
        moduleFunc->obj.next = vm->objects;
        vm->objects = (Obj*)moduleFunc;

        *vm->stackTop++ = OBJ_VAL(moduleFunc);
        call(vm, moduleFunc, 0);
        frame = &vm->frames[vm->frameCount - 1];

        fclose(mem_file);
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
    fprintf(stderr, "\n[line ?] in %s()", function->name->chars);
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
              if (printed < canopy->count - 1) {
                printf(", ");
              }
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
}

void freeVM(VM* vm) {
  // Free all remaining objects
  while (vm->objects) {
    Obj* obj = vm->objects;
    vm->objects = obj->next;
    freeObject(vm, obj);
  }

  // Free the global variable name strings
  for (int i = 0; i < vm->variableCount; i++) {
    free(vm->variables[i].name);
  }

  // Free the main bytecode chunk for the script
  free(vm->bytecode);
}

VMResult runBytecode(const char* path) {
  FILE* file = fopen(path, "rb");
  if (!file) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    return VM_RESULT_RUNTIME_ERROR;
  }

  VM vm = {0};
  initVM(&vm);

  fseek(file, 0L, SEEK_END);
  long fileSize = ftell(file);
  rewind(file);

  // The main script's bytecode is not garbage collected.
  vm.bytecode = (uint8_t*)malloc(fileSize + 1);
  if (vm.bytecode == NULL) exit(74);
  fread(vm.bytecode, sizeof(uint8_t), fileSize, file);
  fclose(file);
  vm.bytecode[fileSize] = 255;

  ObjFunction* topLevelFunc =
      (ObjFunction*)reallocate(&vm, NULL, 0, sizeof(ObjFunction));
  memset(topLevelFunc, 0, sizeof(ObjFunction));  // Zero memory
  topLevelFunc->obj.type = OBJ_FUNCTION;
  topLevelFunc->arity = 0;
  topLevelFunc->code = vm.bytecode;
  topLevelFunc->isModule = false;

  const char* script_name_literal = "script";
  int name_len = strlen(script_name_literal);

  size_t nameSize = sizeof(ObjString) + name_len + 1;
  ObjString* nameString = (ObjString*)reallocate(&vm, NULL, 0, nameSize);
  nameString->obj.type = OBJ_STRING;
  nameString->length = name_len;
  nameString->chars = (char*)(nameString + 1);
  strcpy(nameString->chars, script_name_literal);
  topLevelFunc->name = nameString;

  topLevelFunc->obj.isMarked = false;
  topLevelFunc->obj.next = vm.objects;
  vm.objects = (Obj*)topLevelFunc;

  nameString->obj.isMarked = false;
  nameString->obj.next = vm.objects;
  vm.objects = (Obj*)nameString;

  vm.frameCount = 1;
  CallFrame* frame = &vm.frames[0];
  frame->function = topLevelFunc;
  frame->ip = vm.bytecode;
  frame->slots = vm.stack;

  printf("🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴\n");
  printf("ApesLang VM Output\n");

  VMResult result = run(&vm);

  freeVM(&vm);

  return result;
}