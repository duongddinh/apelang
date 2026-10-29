#include <stdarg.h>
#ifndef APE_FREESTANDING
#include <dirent.h>
#endif

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
static bool canopySet(VM* vm, ObjCanopy* canopy, Value key, Value value);
static int findVariable(VM* vm, const char* name, int len);
static bool isFalsey(Value value);

static void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize);
static void markObject(Obj* object);
static void markValue(Value value);
static void collectGarbage(VM* vm);
static void freeObject(VM* vm, Obj* object);



static void pushNewString(VM* vm, const char* chars, int length) {
    size_t size = sizeof(ObjString) + length + 1;
    ObjString* stringObj = (ObjString*)reallocate(vm, NULL, 0, size);
    stringObj->obj.type = OBJ_STRING;
    stringObj->length = length;
    stringObj->chars = (char*)(stringObj + 1);
    memcpy(stringObj->chars, chars, length);
    stringObj->chars[length] = '\0';
    stringObj->hash = hashString(stringObj->chars, length);
    stringObj->obj.isMarked = false;
    stringObj->obj.next = vm->objects;
    vm->objects = (Obj*)stringObj;
    *vm->stackTop++ = OBJ_VAL(stringObj);
}

static void* reallocate(VM* vm, void* pointer, size_t oldSize, size_t newSize) {
  vm->bytesAllocated += newSize - oldSize;
  
  if (oldSize == 0 && newSize > 0) {
      vm->objectsAllocated++;
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


static char* listFiles(void) {
#ifdef APE_FREESTANDING
  return ape_list_files();
#else
  DIR* dir=opendir(".");
  if (!dir) return NULL;
  size_t used=0, capacity=256;
  char* text=malloc(capacity);
  if (!text) { closedir(dir); return NULL; }
  struct dirent* entry;
  while ((entry=readdir(dir))) {
    if (!strcmp(entry->d_name,".") || !strcmp(entry->d_name,"..")) continue;
    size_t n=strlen(entry->d_name);
    if (used+n+2>capacity) {
      capacity=(used+n+2)*2;
      char* grown=realloc(text,capacity);
      if (!grown) { free(text); closedir(dir); return NULL; }
      text=grown;
    }
    memcpy(text+used,entry->d_name,n); used+=n; text[used++]='\n';
  }
  text[used]=0; closedir(dir); return text;
#endif
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

static bool canopySet(VM* vm, ObjCanopy* canopy, Value key, Value value) {
  if ((canopy->count + 1) * 4 > canopy->capacity * 3) {
    int oldCapacity = canopy->capacity;
    CanopyEntry* old = canopy->entries;
    canopy->capacity *= 2;
    canopy->entries = reallocate(vm, NULL, 0, sizeof(CanopyEntry) * canopy->capacity);
    if (!canopy->entries) exit(1);
    for (int i = 0; i < canopy->capacity; i++) {
      canopy->entries[i].key = NIL_VAL;
      canopy->entries[i].value = NIL_VAL;
    }
    canopy->count = 0;
    for (int i = 0; i < oldCapacity; i++)
      if (!IS_NIL(old[i].key)) canopySet(vm, canopy, old[i].key, old[i].value);
    reallocate(vm, old, sizeof(CanopyEntry) * oldCapacity, 0);
  }
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
    long length = ftell(file);
    if (length <= 0 || length > 1024 * 1024) { fclose(file); return NULL; }
    *out_fileSize = (size_t)length;
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

  frame->loopDepth = vm->loop_counter_top;
  frame->handlerDepth = vm->tryHandlerCount;
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
  markValue(vm->lastError);
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
        free(function->path);
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
  vm->nextGC = vm->bytesAllocated * GC_HEAP_GROW_FACTOR + 1024;
}

// Decode one instruction without reading past its bytecode buffer.
static size_t instructionSize(const uint8_t* code, size_t size, size_t at) {
  if (at >= size) return 0;
  size_t n = 1;
  switch (code[at]) {
    case OP_PUSH:
      if (size - at < 2) return 0;
      if (code[at+1] == VAL_NUMBER) n = 2 + sizeof(double);
      else if (code[at+1] == VAL_OBJ) {
        if (size - at < 4) return 0;
        if (code[at+2] == OBJ_STRING) n = 4 + code[at+3];
        else if (code[at+2] == OBJ_FUNCTION) {
          if (size - at < 9) return 0;
          n = 9 + code[at+8];
        } else return 0;
      } else return 0;
      break;
    case OP_GET_GLOBAL: case OP_SET_GLOBAL:
      if (size - at < 2) return 0;
      n = 2 + code[at+1]; break;
    case OP_GET_LOCAL: case OP_SET_LOCAL: case OP_CALL:
    case OP_BUILD_BUNCH: case OP_BUILD_CANOPY: n = 2; break;
    case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_LOOP:
    case OP_TUMBLE_SETUP: n = 3; break;
    case OP_JUMP_BACK: n = 5; break;
    default:
      if (code[at] > OP_RANDOM) return 0;
  }
  return n <= size - at ? n : 0;
}

bool validateBytecode(const uint8_t* code, size_t size) {
  if (!size || size > 1024 * 1024) return false;
  uint8_t* starts = calloc(size, 1);
  if (!starts) return false;
  bool valid = true;
  size_t at = 0;
  while (at < size) {
    size_t n = instructionSize(code, size, at);
    if (!n) { valid = false; break; }
    starts[at] = 1; at += n;
  }
  for (at = 0; valid && at < size;) {
    size_t n = instructionSize(code, size, at), target = size;
    bool jump = true;
    switch (code[at]) {
      case OP_JUMP: case OP_JUMP_IF_FALSE: case OP_TUMBLE_SETUP:
        target = at + n + ((unsigned)code[at+1] << 8 | code[at+2]); break;
      case OP_LOOP: {
        size_t distance = (unsigned)code[at+1] << 8 | code[at+2];
        if (distance <= at + n) target = at + n - distance;
        break;
      }
      case OP_JUMP_BACK: { uint32_t t; memcpy(&t, code+at+1, 4); target=t; break; }
      case OP_PUSH:
        if (code[at+1] == VAL_OBJ && code[at+2] == OBJ_FUNCTION) {
          uint32_t t; memcpy(&t, code+at+4, 4); target=t;
        } else jump = false;
        break;
      default: jump = false;
    }
    if (jump && (target >= size || !starts[target])) valid = false;
    at += n;
  }
  free(starts);
  return valid;
}

VMResult run(VM* vm) {
  CallFrame* frame = &vm->frames[vm->frameCount - 1];
#define RUNTIME_ERROR(...) do { runtimeError(vm, __VA_ARGS__); goto handle_error; } while (false)
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
    if (vm->bytesAllocated > vm->nextGC) collectGarbage(vm);
    if (vm->stackTop < vm->stack || vm->stackTop >= vm->stack + STACK_MAX - 2) {
      RUNTIME_ERROR("Value stack overflow.");
    }
    if (vm->instructionLimit && ++vm->instructions > vm->instructionLimit) {
      // A budget violation cannot be caught and restarted indefinitely.
      runtimeError(vm, "Instruction budget exceeded.");
      return VM_RESULT_RUNTIME_ERROR;
    }
    ObjFunction* chunk = frame->function->owner ? frame->function->owner : frame->function;
    size_t at = (size_t)(frame->ip - chunk->code);
    if (!instructionSize(chunk->code, chunk->code_size, at)) RUNTIME_ERROR("Invalid or truncated bytecode.");
    uint8_t instruction = *frame->ip++;
    int need = 0;
    switch (instruction) {
      case OP_WAIT: case OP_RANDOM:
      case OP_NUMBER: case OP_ERASE:
      case OP_POP: case OP_NOT: case OP_PRINT: case OP_SET_GLOBAL: case OP_SET_LOCAL:
      case OP_JUMP_IF_FALSE: case OP_RETURN: case OP_FORAGE: case OP_SUMMON:
      case OP_STRLEN: case OP_SHED: case OP_LOOP_START: need=1; break;
      case OP_ADD: case OP_SUB: case OP_MUL: case OP_DIV: case OP_EQUAL:
      case OP_GREATER: case OP_LESS: case OP_GET_SUBSCRIPT: case OP_INSCRIBE:
      case OP_GRAFT: case OP_SCAN: need=2; break;
      case OP_SLICE: case OP_SET_SUBSCRIPT: need=3; break;
      case OP_PAINT: need=5; break;
      case OP_DRAWTEXT: need=4; break;
      case OP_CALL: need=frame->ip[0]+1; break;
      case OP_BUILD_BUNCH: need=frame->ip[0]; break;
      case OP_BUILD_CANOPY: need=frame->ip[0]*2; break;
      default: break;
    }
    if (vm->stackTop - frame->slots < need) RUNTIME_ERROR("Value stack underflow.");
    switch (instruction) {
      case OP_STRLEN: {
        Value value = *--vm->stackTop;
        if (!IS_STRING(value)) {
          RUNTIME_ERROR("Operand must be a string.");
        }
        ObjString* string = AS_STRING(value);
        *vm->stackTop++ = NUMBER_VAL((double)string->length);
        break;
      }
      case OP_GRAFT: {
        Value b_val = *--vm->stackTop;
        Value a_val = *--vm->stackTop;
        if (!IS_STRING(a_val) || !IS_STRING(b_val)) {
          RUNTIME_ERROR("Operands for 'graft' must be strings.");
        }
        ObjString* a = AS_STRING(a_val);
        ObjString* b = AS_STRING(b_val);
        int new_length = a->length + b->length;
        char* new_chars = (char*)malloc(new_length + 1);
        memcpy(new_chars, a->chars, a->length);
        memcpy(new_chars + a->length, b->chars, b->length);
        new_chars[new_length] = '\0';
        pushNewString(vm, new_chars, new_length);
        free(new_chars);
        break;
      }

      case OP_SLICE: {
        Value end_val = *--vm->stackTop;
        Value start_val = *--vm->stackTop;
        Value str_val = *--vm->stackTop;
        if (!IS_STRING(str_val) || !IS_NUMBER(start_val) || !IS_NUMBER(end_val)) {
            RUNTIME_ERROR("'slice' requires a string and two number indices.");
        }
        ObjString* string = AS_STRING(str_val);
        double first = AS_NUMBER(start_val), last = AS_NUMBER(end_val);
        if (!(first >= 0 && first <= last && last <= string->length))
            RUNTIME_ERROR("Slice indices out of bounds.");
        int start = (int)first, end = (int)last;
        if (first != start || last != end) RUNTIME_ERROR("Slice indices must be integers.");
        int slice_len = end - start;
        pushNewString(vm, string->chars + start, slice_len);
        break;
      }

      case OP_SCAN: {
        Value needle_val = *--vm->stackTop;
        Value haystack_val = *--vm->stackTop;
        if (!IS_STRING(haystack_val) || !IS_STRING(needle_val)) {
            RUNTIME_ERROR("'scan' requires two strings.");
        }
        char* haystack = AS_CSTRING(haystack_val);
        char* needle = AS_CSTRING(needle_val);
        char* found = strstr(haystack, needle);
        if (found) {
            *vm->stackTop++ = NUMBER_VAL(found - haystack);
        } else {
            *vm->stackTop++ = NUMBER_VAL(-1);
        }
        break;
      }

      case OP_SHED: {
        Value str_val = *--vm->stackTop;
        if (!IS_STRING(str_val)) {
            RUNTIME_ERROR("'shed' requires a string.");
        }
        ObjString* string = AS_STRING(str_val);
        char* start = string->chars;
        while (isspace((unsigned char)*start)) start++;
        
        char* end = string->chars + string->length;
        while (end > start && isspace((unsigned char)end[-1])) end--;
        int shed_len = (int)(end - start);
        pushNewString(vm, start, shed_len);
        break;
      }

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

            function->path = NULL;
            function->owner = frame->function->owner ? frame->function->owner : frame->function;
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
        if (IS_NUMBER(vm->stackTop[-1]) && AS_NUMBER(vm->stackTop[-1]) == 0)
          RUNTIME_ERROR("Division by zero.");
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
        if (vm->loop_counter_top == STACK_MAX || !IS_NUMBER(vm->stackTop[-1]))
          RUNTIME_ERROR("Invalid swing count or excessive loop nesting.");
        vm->loop_counters[vm->loop_counter_top++] = AS_NUMBER(*--vm->stackTop);
        break;
      case OP_JUMP_BACK: {
        if (!vm->loop_counter_top) RUNTIME_ERROR("Invalid loop state.");
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
      case OP_RANDOM: {
        Value limit=*--vm->stackTop;
        if (!IS_NUMBER(limit) || !(AS_NUMBER(limit)>=1 && AS_NUMBER(limit)<=2147483647))
          RUNTIME_ERROR("random() requires a positive integer limit.");
        uint32_t n=(uint32_t)AS_NUMBER(limit);
        if (n!=AS_NUMBER(limit)) RUNTIME_ERROR("random() requires an integer limit.");
        uint32_t x=vm->randomState;
        x^=x<<13; x^=x>>17; x^=x<<5; vm->randomState=x;
        *vm->stackTop++=NUMBER_VAL(x%n); break;
      }
      case OP_PAINT: case OP_DRAWTEXT: {
        int count=instruction==OP_PAINT ? 5 : 4;
        Value* args=vm->stackTop-count;
        for (int i=0;i<count;i++) {
          if (instruction==OP_DRAWTEXT && i==2) continue;
          if (!IS_NUMBER(args[i]) || !(AS_NUMBER(args[i])>=-32768 && AS_NUMBER(args[i])<=32767))
            RUNTIME_ERROR("Drawing coordinates and colors must be bounded numbers.");
        }
        int color=(int)AS_NUMBER(args[count-1]);
        if (color<0 || color>9) RUNTIME_ERROR("Drawing color must be 0 through 9.");
#ifdef APE_FREESTANDING
        if (instruction==OP_PAINT) {
          ape_paint((int)AS_NUMBER(args[0]),(int)AS_NUMBER(args[1]),
                    (int)AS_NUMBER(args[2]),(int)AS_NUMBER(args[3]),color);
        } else {
          char buffer[64]; const char* text;
          if (IS_STRING(args[2])) text=AS_CSTRING(args[2]);
          else if (IS_NUMBER(args[2])) { sprintf(buffer,"%g",AS_NUMBER(args[2])); text=buffer; }
          else RUNTIME_ERROR("drawtext() requires text or a number.");
          ape_drawtext((int)AS_NUMBER(args[0]),(int)AS_NUMBER(args[1]),text,color);
        }
        vm->graphicsUsed=true;
        vm->stackTop-=count; *vm->stackTop++=NIL_VAL;
#else
        RUNTIME_ERROR("Drawing requires JordyOS.");
#endif
        break;
      }
      case OP_KEY: {
#ifdef APE_FREESTANDING
        const char* key=ape_key(); pushNewString(vm,key,(int)strlen(key));
#else
        RUNTIME_ERROR("Nonblocking keys require JordyOS.");
#endif
        break;
      }
      case OP_WAIT: {
        Value ms=*--vm->stackTop;
        if (!IS_NUMBER(ms) || !(AS_NUMBER(ms)>=1 && AS_NUMBER(ms)<=1000))
          RUNTIME_ERROR("wait() requires 1 through 1000 milliseconds.");
#ifdef APE_FREESTANDING
        ape_wait((unsigned)AS_NUMBER(ms)); *vm->stackTop++=NIL_VAL;
#else
        RUNTIME_ERROR("Frame timing requires JordyOS.");
#endif
        break;
      }
      case OP_FILES: {
        char* names=listFiles();
        if (!names) RUNTIME_ERROR("Cannot list files.");
        pushNewString(vm,names,(int)strlen(names)); free(names); break;
      }
      case OP_ERASE: {
        Value path=*--vm->stackTop;
        if (!IS_STRING(path)) RUNTIME_ERROR("erase() requires a string path.");
        *vm->stackTop++=BOOL_VAL(remove(AS_CSTRING(path))==0); break;
      }
      case OP_NUMBER: {
        Value input=*--vm->stackTop;
        if (IS_NUMBER(input)) { *vm->stackTop++=input; break; }
        if (!IS_STRING(input)) RUNTIME_ERROR("number() requires text or a number.");
        const char* begin=AS_CSTRING(input); char* end;
        double value=strtod(begin,&end);
        if (end==begin) RUNTIME_ERROR("Invalid number.");
        while (isspace((unsigned char)*end)) end++;
        if (*end) RUNTIME_ERROR("Invalid number.");
        *vm->stackTop++=NUMBER_VAL(value); break;
      }
      case OP_ASK_TEXT:
      case OP_ASK: {
        char line[1024];
        if (!fgets(line, sizeof(line), stdin)) {
          *vm->stackTop++ = NIL_VAL;
          break;
        }

        line[strcspn(line, "\r\n")] = 0;

        if (instruction == OP_ASK_TEXT) {
          pushNewString(vm,line,(int)strlen(line)); break;
        }
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
        if (frame->slots + *frame->ip >= vm->stackTop) RUNTIME_ERROR("Invalid local slot.");
        *vm->stackTop++ = frame->slots[*frame->ip++];
        break;
      case OP_SET_LOCAL:
        if (frame->slots + *frame->ip >= vm->stackTop) RUNTIME_ERROR("Invalid local slot.");
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
          if (vm->variableCount == MEMORY_MAX) RUNTIME_ERROR("Too many global variables.");
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
        if (itemCount) memcpy(bunch->values, vm->stackTop - itemCount,
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
          canopySet(vm, canopy, key, value);
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
          double raw = AS_NUMBER(index);
          if (!(raw >= 0 && raw < bunch->count)) *vm->stackTop++ = NIL_VAL;
          else {
            int i = (int)raw;
            if (raw != i) RUNTIME_ERROR("Bunch index must be an integer.");
            *vm->stackTop++ = bunch->values[i];
          }
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
          double raw = AS_NUMBER(index);
          if (!(raw >= 0 && raw < bunch->count)) RUNTIME_ERROR("Bunch index out of bounds.");
          int i = (int)raw;
          if (raw != i) RUNTIME_ERROR("Bunch index must be an integer.");
          bunch->values[i] = value;
          *vm->stackTop++ = value;
        } else if (IS_CANOPY(collection)) {
          ObjCanopy* canopy = AS_CANOPY(collection);
          if (!IS_STRING(index)) RUNTIME_ERROR("Canopy keys must be strings.");
          canopySet(vm, canopy, index, value);
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
          goto handle_error;
        }
        frame = &vm->frames[vm->frameCount - 1];
        break;
      }
      case OP_RETURN: {
        Value result = *--vm->stackTop;
        vm->loop_counter_top = frame->loopDepth;
        vm->tryHandlerCount = frame->handlerDepth;
        vm->frameCount--;
        if (vm->frameCount == 0) {
          vm->stackTop = vm->stack;
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
        handler->loopDepth = vm->loop_counter_top;
        handler->stackTop = vm->stackTop;
        break;
      }
      case OP_TUMBLE_END:
        if (!vm->tryHandlerCount) RUNTIME_ERROR("Invalid tumble end.");
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
        ObjFunction* owner = frame->function->owner ? frame->function->owner : frame->function;
        const char* slash = owner->path ? strrchr(owner->path, '/') : NULL;
        size_t prefix = ape_path[0] != '/' && slash ? (size_t)(slash - owner->path + 1) : 0;
        if (prefix + path_len >= sizeof(apb_path)) RUNTIME_ERROR("Module path too long.");
        if (prefix) memcpy(apb_path, owner->path, prefix);
        memcpy(apb_path + prefix, ape_path, path_len + 1);
        memcpy(apb_path + prefix + path_len - 4, ".apb", 5);

        // Read the bytecode from the .apb file.
        size_t bytecode_size = 0;
        uint8_t* bytecode_buffer = readBytecodeFile(apb_path, &bytecode_size);
        if (bytecode_buffer == NULL) {
            RUNTIME_ERROR("Cannot open or read module file '%s'. Compile it first.", apb_path);
        }
        
        if (!validateBytecode(bytecode_buffer, bytecode_size)) {
          free(bytecode_buffer); RUNTIME_ERROR("Invalid module bytecode.");
        }
        // Create the module function that will own this bytecode.
        ObjFunction* moduleFunc =
            (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
        memset(moduleFunc, 0, sizeof(*moduleFunc));
        moduleFunc->path = malloc(strlen(apb_path) + 1);
        strcpy(moduleFunc->path, apb_path);
        moduleFunc->obj.type = OBJ_FUNCTION;
        moduleFunc->arity = 0;
        moduleFunc->name = AS_STRING(pathValue);

        // This function owns the new bytecode chunk.
        moduleFunc->code = bytecode_buffer;
        moduleFunc->code_size = bytecode_size;
        moduleFunc->isModule = true; // This tells the GC to free the code buffer later.
        
        moduleFunc->obj.isMarked = false;
        moduleFunc->obj.next = vm->objects;
        vm->objects = (Obj*)moduleFunc;
        
        *vm->stackTop++ = OBJ_VAL(moduleFunc);
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
    continue;
handle_error:
    if (vm->tryHandlerCount == 0) return VM_RESULT_RUNTIME_ERROR;
    TryHandler handler = vm->tryHandlers[--vm->tryHandlerCount];
    vm->frameCount = handler.frameCount;
    vm->loop_counter_top = handler.loopDepth;
    vm->stackTop = handler.stackTop;
    *vm->stackTop++ = vm->lastError;
    frame = &vm->frames[vm->frameCount - 1];
    frame->ip = handler.catchIp;
  }
}

static void runtimeError(VM* vm, const char* format, ...) {
  char buffer[1024];
  va_list args;
  va_start(args, format);
  vsnprintf(buffer, sizeof(buffer), format, args);
  va_end(args);
  if (vm->tryHandlerCount == 0) fprintf(stderr, "Runtime Error: %s\n", buffer);
  for (int i = vm->tryHandlerCount ? -1 : vm->frameCount - 1; i >= 0; i--) {
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
  vm->lastError = OBJ_VAL(errObj);
}

static void printValueDepth(Value value, int depth) {
  if (depth >= 32) { printf("<...>"); return; }
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
            printValueDepth(bunch->values[i], depth + 1);
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
              printValueDepth(canopy->entries[i].key, depth + 1);
              printf(": ");
              printValueDepth(canopy->entries[i].value, depth + 1);
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

static void printValue(Value value) { printValueDepth(value, 0); }

void initVM(VM* vm) {
  memset(vm, 0, sizeof(*vm));
  vm->lastError = NIL_VAL;
  vm->randomState = 0x9e3779b9u;
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
  return executeSource(vm, source, true);
}

VMResult executeSource(VM* vm, const char* source, bool isRepl) {
  uint8_t* bytecode_buffer = NULL;
  size_t bytecode_size = 0;
  FILE* mem_file = open_memstream((char**)&bytecode_buffer, &bytecode_size);
  if (!mem_file) {
    fprintf(stderr, "Failed to open memory stream for compilation.\n");
    return VM_RESULT_RUNTIME_ERROR;
  }

  if (!compile(source, mem_file, isRepl)) {
    fclose(mem_file);
    free(bytecode_buffer);
    return VM_RESULT_COMPILE_ERROR;
  }

  fclose(mem_file);
  if (!validateBytecode(bytecode_buffer, bytecode_size)) {
    free(bytecode_buffer);
    return VM_RESULT_COMPILE_ERROR;
  }
  vm->stackTop = vm->stack;
  vm->frameCount = vm->tryHandlerCount = vm->loop_counter_top = 0;
  vm->instructions = 0;

  ObjFunction* function =
      (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
  memset(function, 0, sizeof(*function));
  function->obj.type = OBJ_FUNCTION;
  function->arity = 0;
  function->name = NULL;

  function->code = bytecode_buffer;
  function->code_size = bytecode_size;
  function->owner = NULL;
  function->code_offset = 0;
  function->isModule = true; 

  function->obj.next = vm->objects;
  vm->objects = (Obj*)function;
  *vm->stackTop++ = OBJ_VAL(function);
  call(vm, function, 0);
  
  if (vm->frameCount > vm->maxFrameCount) {
      vm->maxFrameCount = vm->frameCount;
  }

  VMResult result = run(vm);
  vm->stackTop = vm->stack;
  vm->frameCount = vm->tryHandlerCount = vm->loop_counter_top = 0;

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
  if (fileSize <= 0 || fileSize > 1024 * 1024) { fclose(file); return VM_RESULT_RUNTIME_ERROR; }
  rewind(file);
  vm->bytecode = (uint8_t*)malloc(fileSize + 1);
  if (vm->bytecode == NULL) exit(74);
  if (fread(vm->bytecode, sizeof(uint8_t), fileSize, file) != (size_t)fileSize) {
    fclose(file); free(vm->bytecode); vm->bytecode = NULL; return VM_RESULT_RUNTIME_ERROR;
  }
  fclose(file);
  if (!validateBytecode(vm->bytecode, fileSize)) {
    free(vm->bytecode); vm->bytecode = NULL;
    fprintf(stderr, "Invalid bytecode.\n"); return VM_RESULT_RUNTIME_ERROR;
  }
  vm->bytecode[fileSize] = 255;

  ObjFunction* topLevelFunc =
      (ObjFunction*)reallocate(vm, NULL, 0, sizeof(ObjFunction));
  memset(topLevelFunc, 0, sizeof(ObjFunction));
  topLevelFunc->obj.type = OBJ_FUNCTION;
  topLevelFunc->arity = 0;
  
  topLevelFunc->code = vm->bytecode;
  topLevelFunc->code_size = fileSize;
  topLevelFunc->owner = NULL;
  topLevelFunc->code_offset = 0;
  topLevelFunc->isModule = true;
  topLevelFunc->path = malloc(strlen(path) + 1);
  strcpy(topLevelFunc->path, path);
  vm->bytecode = NULL;

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
  vm->stackTop = vm->stack;
  *vm->stackTop++ = OBJ_VAL(topLevelFunc);
  vm->frameCount = 1;

  if (vm->frameCount > vm->maxFrameCount) {
      vm->maxFrameCount = vm->frameCount;
  }

  CallFrame* frame = &vm->frames[0];
  frame->function = topLevelFunc;
  frame->ip = topLevelFunc->code;
  frame->slots = vm->stack;
  frame->loopDepth = frame->handlerDepth = 0;
  printf("🌴 🦍  OOH-OOH-AAH-AAH!  WELCOME TO THE BANANA JUNGLE  🦍 🌴\n");
  printf("ApesLang VM Output\n");
  VMResult result = run(vm);
  return result;
}