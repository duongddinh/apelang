#include <stdio.h>
#include <stdlib.h>

#include "compiler.h"
#include "../lexer/lexer.h"

typedef struct {
  Token name;
  int depth;
} Local;

typedef struct Compiler {
  struct Compiler* enclosing;
  Local locals[256];
  int localCount;
  int scopeDepth;
} Compiler;

typedef struct {
  Lexer lexer;
  Token current;
  Token previous;
  bool hadError;
  FILE* outFile;
  Compiler* compiler;
  bool isRepl; // Flag to indicate if we are in REPL mode
} Parser;

typedef enum {
  PREC_NONE,
  PREC_ASSIGNMENT,
  PREC_YELLOW,      // Logical OR
  PREC_RIPE,        // Logical AND
  PREC_EQUALITY,
  PREC_COMPARISON,
  PREC_TERM,
  PREC_FACTOR,
  PREC_UNARY,
  PREC_CALL,
  PREC_PRIMARY
} Precedence;

typedef void (*ParseFn)(Parser*, bool);

typedef struct {
  ParseFn prefix;
  ParseFn infix;
  Precedence precedence;
} ParseRule;

static void expression(Parser* p);
static void statement(Parser* p);
static void declaration(Parser* p);
static ParseRule* getRule(TokenType type);
static void parsePrecedence(Parser* p, Precedence precedence);
static void block(Parser* p);
static void variable(Parser* p, bool canAssign);
static void call(Parser* p, bool canAssign);
static void bunchLiteral(Parser* p, bool canAssign);
static void canopyLiteral(Parser* p, bool canAssign);
static void subscript(Parser* p, bool canAssign);
static void summonStatement(Parser* p);
static void tumbleStatement(Parser* p);
static void funDeclaration(Parser* p);
static void varDeclaration(Parser* p);
static void consume(Parser* p, TokenType type, const char* message);
static void beginScope(Parser* p);
static void endScope(Parser* p);
static void declareVariable(Parser* p);
static void bananaStatement(Parser* p);
static void ripe_(Parser* p, bool canAssign);
static void yellow_(Parser* p, bool canAssign);
static void expressionStatement(Parser* p);

static void errorAt(Parser* p, Token* token, const char* message) {
  if (p->hadError) return;
  p->hadError = true;
  fprintf(stderr, "[Error] ");
  if (token->type == TOKEN_EOF) {
    fprintf(stderr, "at end");
  } else if (token->type != TOKEN_ERROR) {
    fprintf(stderr, "at '%.*s'", token->length, token->start);
  }
  fprintf(stderr, ": %s\n", message);
}
static void error(Parser* p, const char* message) { errorAt(p, &p->previous, message); }
static void emitByte(Parser* p, uint8_t byte) { fwrite(&byte, sizeof(uint8_t), 1, p->outFile); }
static void emitBytes(Parser* p, uint8_t byte1, uint8_t byte2) {
  emitByte(p, byte1);
  emitByte(p, byte2);
}
static long emitJump(Parser* p, uint8_t instruction) {
  emitByte(p, instruction);
  emitByte(p, 0xff);
  emitByte(p, 0xff);
  return ftell(p->outFile) - 2;
}
static void patchJump(Parser* p, long offset) {
  long jump = ftell(p->outFile) - offset - 2;
  if (jump > UINT16_MAX) {
    error(p, "Too much code to jump over.");
  }
  fseek(p->outFile, offset, SEEK_SET);
  emitByte(p, (jump >> 8) & 0xff);
  emitByte(p, jump & 0xff);
  fseek(p->outFile, 0, SEEK_END);
}
static void emitAddress(Parser* p, uint32_t address) {
  fwrite(&address, sizeof(uint32_t), 1, p->outFile);
}

static void emitLoop(Parser* p, long loopStart) {
    emitByte(p, OP_LOOP);

    long offset = ftell(p->outFile) - loopStart + 2;
     if (offset > UINT16_MAX) {
        error(p, "Loop body too large.");
    }
    emitByte(p, (offset >> 8) & 0xff);
    emitByte(p, offset & 0xff);
}
static void ripe_(Parser* p, bool canAssign) {
    long endJump = emitJump(p, OP_JUMP_IF_FALSE);
    emitByte(p, OP_POP);
    parsePrecedence(p, PREC_RIPE);
    patchJump(p, endJump);
}

static void yellow_(Parser* p, bool canAssign) {
    long elseJump = emitJump(p, OP_JUMP_IF_FALSE);
    long endJump = emitJump(p, OP_JUMP);
    patchJump(p, elseJump);
    emitByte(p, OP_POP);
    parsePrecedence(p, PREC_YELLOW);
    patchJump(p, endJump);
}
static void bananaStatement(Parser* p) {
    long loopStart = ftell(p->outFile);
    consume(p, TOKEN_LPAREN, "Expect '(' after 'banana'.");
    expression(p);
    consume(p, TOKEN_RPAREN, "Expect ')' after banana condition.");
    consume(p, TOKEN_LBRACE, "Expect '{' after banana condition.");
    long exitJump = emitJump(p, OP_JUMP_IF_FALSE);
    emitByte(p, OP_POP);
    block(p);
    emitLoop(p, loopStart);
    patchJump(p, exitJump);
    emitByte(p, OP_POP);
}

static void emitReturn(Parser* p) {
  emitByte(p, OP_NIL);
  emitByte(p, OP_RETURN);
}
static void advance(Parser* p) {
  p->previous = p->current;
  p->current = scanToken(&p->lexer);
  if (p->current.type == TOKEN_ERROR) {
    errorAt(p, &p->current, p->current.start);
  }
}
static void consume(Parser* p, TokenType type, const char* message) {
  if (p->current.type == type) {
    advance(p);
    return;
  }
  errorAt(p, &p->current, message);
}
static bool check(Parser* p, TokenType type) { return p->current.type == type; }
static bool match(Parser* p, TokenType type) {
  if (!check(p, type)) return false;
  advance(p);
  return true;
}

static void number(Parser* p, bool canAssign) {
  double value = strtod(p->previous.start, NULL);
  emitByte(p, OP_PUSH);
  emitByte(p, VAL_NUMBER);
  fwrite(&value, sizeof(double), 1, p->outFile);
}
static void string(Parser* p, bool canAssign) {
  const char* str = p->previous.start + 1;
  int length = p->previous.length - 2;
  emitByte(p, OP_PUSH);
  emitByte(p, VAL_OBJ);
  emitByte(p, OBJ_STRING);
  emitByte(p, (uint8_t)length);
  fwrite(str, sizeof(char), length, p->outFile);
}
static void literal(Parser* p, bool canAssign) {
  switch (p->previous.type) {
    case TOKEN_FALSE: emitByte(p, OP_FALSE); break;
    case TOKEN_TRUE:  emitByte(p, OP_TRUE); break;
    case TOKEN_NIL:   emitByte(p, OP_NIL); break;
    default: return;
  }
}
static void unary(Parser* p, bool canAssign) {
  TokenType operatorType = p->previous.type;
  parsePrecedence(p, PREC_UNARY);
  if (operatorType == TOKEN_BANG) emitByte(p, OP_NOT);
}
static void binary(Parser* p, bool canAssign) {
  TokenType operatorType = p->previous.type;
  ParseRule* rule = getRule(operatorType);
  parsePrecedence(p, (Precedence)(rule->precedence + 1));
  switch (operatorType) {
    case TOKEN_PLUS:          emitByte(p, OP_ADD); break;
    case TOKEN_MINUS:         emitByte(p, OP_SUB); break;
    case TOKEN_MUL:           emitByte(p, OP_MUL); break;
    case TOKEN_DIV:           emitByte(p, OP_DIV); break;
    case TOKEN_EQUAL_EQUAL:   emitByte(p, OP_EQUAL); break;
    case TOKEN_BANG_EQUAL:    emitBytes(p, OP_EQUAL, OP_NOT); break;
    case TOKEN_GREATER:       emitByte(p, OP_GREATER); break;
    case TOKEN_GREATER_EQUAL: emitBytes(p, OP_LESS, OP_NOT); break;
    case TOKEN_LESS:          emitByte(p, OP_LESS); break;
    case TOKEN_LESS_EQUAL:    emitBytes(p, OP_GREATER, OP_NOT); break;
    default: return;
  }
}
static void grouping(Parser* p, bool canAssign) {
  expression(p);
  consume(p, TOKEN_RPAREN, "Expect ')' after expression.");
}
static void ask(Parser* p, bool canAssign) {
  consume(p, TOKEN_LPAREN, "Expect '(' after 'ask'.");
  consume(p, TOKEN_RPAREN, "Expect ')' after ask arguments.");
  emitByte(p, OP_ASK);
}
static uint8_t argumentList(Parser* p) {
  uint8_t argCount = 0;
  if (!check(p, TOKEN_RPAREN)) {
    do {
      expression(p);
      if (argCount == 255) error(p, "Can't have more than 255 arguments.");
      argCount++;
    } while (match(p, TOKEN_COMMA));
  }
  consume(p, TOKEN_RPAREN, "Expect ')' after arguments.");
  return argCount;
}
static void call(Parser* p, bool canAssign) {
  uint8_t argCount = argumentList(p);
  emitBytes(p, OP_CALL, argCount);
}
static void bunchLiteral(Parser* p, bool canAssign) {
    uint8_t itemCount = 0;
    if (!check(p, TOKEN_RBRACKET)) {
        do {
            expression(p);
            if (itemCount == 255) error(p, "Can't have more than 255 items in a bunch literal.");
            itemCount++;
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RBRACKET, "Expect ']' after bunch items.");
    emitBytes(p, OP_BUILD_BUNCH, itemCount);
}
static void canopyLiteral(Parser* p, bool canAssign) {
    uint8_t itemCount = 0;
    if (!check(p, TOKEN_RBRACE)) {
        do {
            consume(p, TOKEN_STRING, "Expect string as canopy key.");
            string(p, false);
            consume(p, TOKEN_COLON, "Expect ':' after canopy key.");
            expression(p);
            if (itemCount == 255) error(p, "Can't have more than 255 items in a canopy literal.");
            itemCount++;
        } while (match(p, TOKEN_COMMA));
    }
    consume(p, TOKEN_RBRACE, "Expect '}' after canopy items.");
    emitBytes(p, OP_BUILD_CANOPY, itemCount);
}
static void subscript(Parser* p, bool canAssign) {
    expression(p);
    consume(p, TOKEN_RBRACKET, "Expect ']' after subscript.");
    if (canAssign && match(p, TOKEN_EQUAL)) {
        expression(p);
        emitByte(p, OP_SET_SUBSCRIPT);
    } else {
        emitByte(p, OP_GET_SUBSCRIPT);
    }
}

ParseRule rules[] = {
    [TOKEN_LPAREN]      = {grouping, call, PREC_CALL},
    [TOKEN_RPAREN]      = {NULL, NULL, PREC_NONE},
    [TOKEN_LBRACE]      = {canopyLiteral, NULL, PREC_NONE},
    [TOKEN_RBRACE]      = {NULL, NULL, PREC_NONE},
    [TOKEN_LBRACKET]    = {bunchLiteral, subscript, PREC_CALL},
    [TOKEN_RBRACKET]    = {NULL, NULL, PREC_NONE},
    [TOKEN_COLON]       = {NULL, NULL, PREC_NONE},
    [TOKEN_COMMA]       = {NULL, NULL, PREC_NONE},
    [TOKEN_BANG]        = {unary, NULL, PREC_NONE},
    [TOKEN_BANG_EQUAL]  = {NULL, binary, PREC_EQUALITY},
    [TOKEN_EQUAL]       = {NULL, NULL, PREC_NONE},
    [TOKEN_EQUAL_EQUAL] = {NULL, binary, PREC_EQUALITY},
    [TOKEN_GREATER]     = {NULL, binary, PREC_COMPARISON},
    [TOKEN_GREATER_EQUAL] = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS]        = {NULL, binary, PREC_COMPARISON},
    [TOKEN_LESS_EQUAL]  = {NULL, binary, PREC_COMPARISON},
    [TOKEN_ID]          = {variable, NULL, PREC_NONE},
    [TOKEN_STRING]      = {string, NULL, PREC_NONE},
    [TOKEN_NUM]         = {number, NULL, PREC_NONE},
    [TOKEN_ASK]         = {ask, NULL, PREC_NONE},
    [TOKEN_FALSE]       = {literal, NULL, PREC_NONE},
    [TOKEN_TRUE]        = {literal, NULL, PREC_NONE},
    [TOKEN_NIL]         = {literal, NULL, PREC_NONE},
    [TOKEN_PLUS]        = {NULL, binary, PREC_TERM},
    [TOKEN_MINUS]       = {NULL, binary, PREC_TERM},
    [TOKEN_MUL]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_DIV]         = {NULL, binary, PREC_FACTOR},
    [TOKEN_SUMMON]      = {NULL, NULL, PREC_NONE},
    [TOKEN_TUMBLE]      = {NULL, NULL, PREC_NONE},
    [TOKEN_BANANA]      = {NULL, NULL, PREC_NONE},
    [TOKEN_RIPE]        = {NULL, ripe_, PREC_RIPE},     
    [TOKEN_YELLOW]      = {NULL, yellow_, PREC_YELLOW},
    [TOKEN_CATCH]       = {NULL, NULL, PREC_NONE},
    [TOKEN_ERROR]       = {NULL, NULL, PREC_NONE},
};

static ParseRule* getRule(TokenType type) { return &rules[type]; }
static void parsePrecedence(Parser* p, Precedence precedence) {
  advance(p);
  ParseFn prefixRule = getRule(p->previous.type)->prefix;
  if (prefixRule == NULL) {
    error(p, "Expect expression.");
    return;
  }
  bool canAssign = precedence <= PREC_ASSIGNMENT;
  prefixRule(p, canAssign);
  while (precedence <= getRule(p->current.type)->precedence) {
    advance(p);
    ParseFn infixRule = getRule(p->previous.type)->infix;
    infixRule(p, canAssign);
  }
}
static void expression(Parser* p) { parsePrecedence(p, PREC_ASSIGNMENT); }
static void block(Parser* p) {
  while (!check(p, TOKEN_RBRACE) && !check(p, TOKEN_EOF)) {
    declaration(p);
  }
  consume(p, TOKEN_RBRACE, "Expect '}' after block.");
}
static void printStatement(Parser* p) {
  expression(p);
  emitByte(p, OP_PRINT);
}

static void expressionStatement(Parser* p) {
  expression(p);
  // In REPL mode, print the result of an expression statement.
  // Otherwise, pop it off the stack.
  emitByte(p, p->isRepl ? OP_PRINT : OP_POP);
}

static void ifStatement(Parser* p) {
  consume(p, TOKEN_LPAREN, "Expect '(' after 'if'.");
  expression(p);
  consume(p, TOKEN_RPAREN, "Expect ')' after if condition.");
  consume(p, TOKEN_LBRACE, "Expect '{' after condition.");
  long thenJump = emitJump(p, OP_JUMP_IF_FALSE);
  emitByte(p, OP_POP);
  block(p);
  long elseJump = emitJump(p, OP_JUMP);
  patchJump(p, thenJump);
  emitByte(p, OP_POP);
  if (match(p, TOKEN_ELSE)) {
    consume(p, TOKEN_LBRACE, "Expect '{' after 'else'.");
    block(p);
  }
  patchJump(p, elseJump);
}

static void swingStatement(Parser* p) {
  expression(p);
  emitByte(p, OP_LOOP_START);
  uint32_t loopStart = ftell(p->outFile);
  consume(p, TOKEN_LBRACE, "Expect '{' before swing block.");
  block(p);
  emitByte(p, OP_JUMP_BACK);
  emitAddress(p, loopStart);
}
static void giveStatement(Parser* p) {
  if (check(p, TOKEN_RBRACE)) {
    emitReturn(p);
  } else {
    expression(p);
    emitByte(p, OP_RETURN);
  }
}

static void tumbleStatement(Parser* p) {
    consume(p, TOKEN_LBRACE, "Expect '{' after 'tumble'.");
    long catchJump = emitJump(p, OP_TUMBLE_SETUP);
    block(p);
    emitByte(p, OP_TUMBLE_END);
    long exitJump = emitJump(p, OP_JUMP);
    patchJump(p, catchJump);
    consume(p, TOKEN_CATCH, "Expect 'catch' after 'tumble' block.");
    consume(p, TOKEN_LPAREN, "Expect '(' after 'catch'.");
    beginScope(p);
    consume(p, TOKEN_ID, "Expect error variable name.");
    declareVariable(p);
    consume(p, TOKEN_RPAREN, "Expect ')' after error variable.");
    consume(p, TOKEN_LBRACE, "Expect '{' after catch clause.");
    block(p);
    endScope(p);
    patchJump(p, exitJump);
}

static void summonStatement(Parser* p) {
    consume(p, TOKEN_STRING, "Expect file path string after 'summon'.");
    string(p, false);
    emitByte(p, OP_SUMMON);
}

static void statement(Parser* p) {
  if (match(p, TOKEN_TREE)) {
    printStatement(p);
  } else if (match(p, TOKEN_GIVE)) {
    giveStatement(p);
  } else if (match(p, TOKEN_IF)) {
    ifStatement(p);
  } else if (match(p, TOKEN_TUMBLE)) {
    tumbleStatement(p);
  } else if (match(p, TOKEN_SUMMON)) {
    summonStatement(p);
  } else if (match(p, TOKEN_SWING)) {
    swingStatement(p);
  }
  else if (match(p, TOKEN_BANANA)) {
    bananaStatement(p);
  }
   else if (match(p, TOKEN_LBRACE)) {
    beginScope(p);
    block(p);
    endScope(p);
  } else {
    expressionStatement(p);
  }
}

static void initCompiler(Compiler* compiler, Compiler* enclosing) {
  compiler->enclosing = enclosing;
  compiler->localCount = 0;
  compiler->scopeDepth = 0;
  Local* local = &compiler->locals[compiler->localCount++];
  local->depth = 0;
  local->name.start = "";
  local->name.length = 0;
}

static void beginScope(Parser* p) { p->compiler->scopeDepth++; }

static void endScope(Parser* p) {
  p->compiler->scopeDepth--;
  while (p->compiler->localCount > 0 &&
         p->compiler->locals[p->compiler->localCount - 1].depth > p->compiler->scopeDepth) {
    emitByte(p, OP_POP);
    p->compiler->localCount--;
  }
}

static int resolveLocal(Compiler* compiler, Token* name) {
  for (int i = compiler->localCount - 1; i >= 0; i--) {
    Local* local = &compiler->locals[i];
    if (name->length == local->name.length &&
        memcmp(name->start, local->name.start, name->length) == 0) {
      return i;
    }
  }
  return -1;
}

static void addLocal(Parser* p, Token name) {
  if (p->compiler->localCount == 256) {
    error(p, "Too many local variables in function.");
    return;
  }
  Local* local = &p->compiler->locals[p->compiler->localCount++];
  local->name = name;
  local->depth = p->compiler->scopeDepth;
}

static void declareVariable(Parser* p) {
  if (p->compiler->scopeDepth == 0) return;
  Token* name = &p->previous;
  for (int i = p->compiler->localCount - 1; i >= 0; i--) {
    Local* local = &p->compiler->locals[i];
    if (local->depth != -1 && local->depth < p->compiler->scopeDepth) break;
    if (name->length == local->name.length &&
        memcmp(name->start, local->name.start, name->length) == 0) {
      error(p, "Already a variable with this name in this scope.");
    }
  }
  addLocal(p, *name);
}

static void varDeclaration(Parser* p) {
  consume(p, TOKEN_ID, "Expect variable name.");
  Token name = p->previous;
  declareVariable(p);
  if (match(p, TOKEN_EQUAL)) {
    expression(p);
  } else {
    emitByte(p, OP_NIL);
  }
  if (p->compiler->scopeDepth == 0) {
    emitByte(p, OP_SET_GLOBAL);
    uint8_t len = (uint8_t)name.length;
    emitByte(p, len);
    fwrite(name.start, sizeof(char), len, p->outFile);
    emitByte(p, OP_POP);
  }
}

static void funDeclaration(Parser* p) {
  consume(p, TOKEN_ID, "Expect function name.");
  Token name = p->previous;
  declareVariable(p);
  long bodyJump = emitJump(p, OP_JUMP);
  long bodyStart = ftell(p->outFile);
  Compiler compiler;
  initCompiler(&compiler, p->compiler);
  p->compiler = &compiler;
  beginScope(p);
  int arity = 0;
  consume(p, TOKEN_LPAREN, "Expect '(' after function name.");
  if (!check(p, TOKEN_RPAREN)) {
    do {
      arity++;
      if (arity > 255) error(p, "Can't have more than 255 parameters.");
      consume(p, TOKEN_ID, "Expect parameter name.");
      declareVariable(p);
    } while (match(p, TOKEN_COMMA));
  }
  consume(p, TOKEN_RPAREN, "Expect ')' after parameters.");
  consume(p, TOKEN_LBRACE, "Expect '{' before function body.");
  block(p);
  emitReturn(p);
  p->compiler = p->compiler->enclosing;
  patchJump(p, bodyJump);
  emitByte(p, OP_PUSH);
  emitByte(p, VAL_OBJ);
  emitByte(p, OBJ_FUNCTION);
  emitByte(p, (uint8_t)arity);
  emitAddress(p, bodyStart);
  uint8_t nameLen = (uint8_t)name.length;
  emitByte(p, nameLen);
  fwrite(name.start, sizeof(char), nameLen, p->outFile);
  if (p->compiler->scopeDepth == 0) {
    emitByte(p, OP_SET_GLOBAL);
    uint8_t len = (uint8_t)name.length;
    emitByte(p, len);
    fwrite(name.start, sizeof(char), len, p->outFile);
  }
}

static void variable(Parser* p, bool canAssign) {
  Token name = p->previous;
  int arg = resolveLocal(p->compiler, &name);
  if (arg != -1) {
    if (canAssign && match(p, TOKEN_EQUAL)) {
      expression(p);
      emitBytes(p, OP_SET_LOCAL, (uint8_t)arg);
    } else {
      emitBytes(p, OP_GET_LOCAL, (uint8_t)arg);
    }
  } else {
    if (canAssign && match(p, TOKEN_EQUAL)) {
      expression(p);
      emitByte(p, OP_SET_GLOBAL);
    } else {
      emitByte(p, OP_GET_GLOBAL);
    }
    uint8_t len = (uint8_t)name.length;
    emitByte(p, len);
    fwrite(name.start, sizeof(char), len, p->outFile);
  }
}

static void declaration(Parser* p) {
  if (p->hadError) return;
  if (match(p, TOKEN_TRIBE)) {
    funDeclaration(p);
  } else if (match(p, TOKEN_APE)) {
    varDeclaration(p);
  } else {
    statement(p);
  }
}

int compile(const char* source, FILE* outFile, bool isRepl) {
  Parser p;
  p.hadError = false;
  initLexer(&p.lexer, source);
  p.outFile = outFile;
  p.isRepl = isRepl; // Set the REPL flag in the parser
  if (p.outFile == NULL) return 0;

  Compiler compiler;
  initCompiler(&compiler, NULL);
  p.compiler = &compiler;

  advance(&p);
  while (!match(&p, TOKEN_EOF)) {
    declaration(&p);
    if (p.hadError) break;
  }
  

      emitReturn(&p);
  

  return !p.hadError;
}
