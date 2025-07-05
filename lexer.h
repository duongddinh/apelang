#ifndef APE_LEXER_H
#define APE_LEXER_H

#include "common.h"

typedef enum {
  // Single character tokens
  TOKEN_LPAREN,
  TOKEN_RPAREN,
  TOKEN_LBRACE,
  TOKEN_RBRACE,
  TOKEN_COMMA, 
  // One or two character tokens
  TOKEN_EQUAL,
  TOKEN_EQUAL_EQUAL,
  TOKEN_BANG,
  TOKEN_BANG_EQUAL,
  TOKEN_LESS,
  TOKEN_LESS_EQUAL,
  TOKEN_GREATER,
  TOKEN_GREATER_EQUAL,
  // Literals
  TOKEN_ID,
  TOKEN_STRING,
  TOKEN_NUM,
  // Keywords
  TOKEN_APE,
  TOKEN_ELSE,
  TOKEN_FALSE,
  TOKEN_IF,
  TOKEN_NIL,
  TOKEN_SWING,
  TOKEN_TREE,
  TOKEN_TRUE,
  TOKEN_ASK,
  TOKEN_TRIBE,  
  TOKEN_GIVE,  
  // Apelang specific math operators
  TOKEN_PLUS,
  TOKEN_MINUS,
  TOKEN_MUL,
  TOKEN_DIV,
  // Misc
  TOKEN_ERROR,
  TOKEN_EOF
} TokenType;

typedef struct {
  TokenType type;
  const char* start;
  int length;
} Token;

typedef struct {
  const char* start;
  const char* current;
} Lexer;

void initLexer(Lexer* lexer, const char* source);
Token scanToken(Lexer* lexer);

#endif  
