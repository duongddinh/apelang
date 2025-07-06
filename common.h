#ifndef APE_COMMON_H
#define APE_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


typedef enum {
    // Constants, Literals
    OP_PUSH,
    OP_NIL,
    OP_TRUE,
    OP_FALSE,
    OP_POP,
    // Unary
    OP_NOT,
    // Binary Arithmetic
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    // Binary Comparison
    OP_EQUAL,
    OP_GREATER,
    OP_LESS,
    // Control Flow
    OP_JUMP_IF_FALSE,
    OP_JUMP,
    OP_JUMP_BACK,
    OP_LOOP_START,
    // Statements
    OP_PRINT,
    OP_ASK,
    OP_GET_GLOBAL, 
    OP_SET_GLOBAL, 
    OP_GET_LOCAL,  
    OP_SET_LOCAL,  
    // Functions
    OP_CALL,
    OP_RETURN,

    OP_BUILD_BUNCH,    //  arrays/lists
    OP_BUILD_CANOPY,   //  maps/dictionaries
    OP_GET_SUBSCRIPT,  
    OP_SET_SUBSCRIPT,  

    OP_TUMBLE_SETUP, //  try/catch
    OP_TUMBLE_END,   //  try/catch
    OP_SUMMON,       //  modules

    OP_LOOP,
    
} OpCode;


typedef struct Obj Obj;
typedef struct ObjString ObjString;
typedef struct ObjFunction ObjFunction;

typedef struct ObjBunch ObjBunch;
typedef struct ObjCanopy ObjCanopy;


typedef enum { VAL_BOOL, VAL_NIL, VAL_NUMBER, VAL_OBJ } ValueType;
typedef struct {
    ValueType type;
    union { bool boolean; double number; Obj* obj; } as;
} Value;

#define IS_BOOL(value)    ((value).type == VAL_BOOL)
#define IS_NIL(value)     ((value).type == VAL_NIL)
#define IS_NUMBER(value)  ((value).type == VAL_NUMBER)
#define IS_OBJ(value)     ((value).type == VAL_OBJ)
#define AS_BOOL(value)    ((value).as.boolean)
#define AS_NUMBER(value)  ((value).as.number)
#define AS_OBJ(value)     ((value).as.obj)
#define BOOL_VAL(value)   ((Value){VAL_BOOL, {.boolean = value}})
#define NIL_VAL           ((Value){VAL_NIL, {.number = 0}})
#define NUMBER_VAL(value) ((Value){VAL_NUMBER, {.number = value}})
#define OBJ_VAL(object)   ((Value){VAL_OBJ, {.obj = (Obj*)object}})

typedef enum {
    OBJ_STRING,
    OBJ_FUNCTION,

    OBJ_BUNCH,
    OBJ_CANOPY,
} ObjType;

struct Obj {
    ObjType type;

    bool isMarked; 
    struct Obj* next;  

};

struct ObjString {
    Obj obj;
    int length;
    char* chars;
    uint32_t hash;
};

struct ObjFunction {
    Obj obj;
    int arity;
    uint8_t* code;
    ObjString* name;

    bool isModule;
};

struct ObjBunch { // Arrays/Lists
    Obj obj;
    int count;
    int capacity;
    Value* values;
};

typedef struct { // Map Entries
    Value key;
    Value value;
} CanopyEntry;

struct ObjCanopy { // Maps/Dictionaries
    Obj obj;
    int count;
    int capacity;
    CanopyEntry* entries;
};


#define OBJ_TYPE(value)   (AS_OBJ(value)->type)
#define IS_STRING(value)  (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_STRING)
#define IS_FUNCTION(value) (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_FUNCTION)
#define IS_BUNCH(value)   (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_BUNCH)
#define IS_CANOPY(value)  (IS_OBJ(value) && OBJ_TYPE(value) == OBJ_CANOPY)

#define AS_STRING(value)  ((ObjString*)AS_OBJ(value))
#define AS_CSTRING(value) (((ObjString*)AS_OBJ(value))->chars)
#define AS_FUNCTION(value) ((ObjFunction*)AS_OBJ(value))
#define AS_BUNCH(value)   ((ObjBunch*)AS_OBJ(value))
#define AS_CANOPY(value)  ((ObjCanopy*)AS_OBJ(value))

#endif