#include "lexer.h"

void initLexer(Lexer* lexer, const char* source) {
  lexer->start = source;
  lexer->current = source;
}

static bool isAtEnd(Lexer* lexer) { return *lexer->current == '\0'; }
static char advance(Lexer* lexer) {
  lexer->current++;
  return lexer->current[-1];
}
static char peek(Lexer* lexer) { return *lexer->current; }
static char peekNext(Lexer* lexer) {
  if (isAtEnd(lexer)) return '\0';
  return lexer->current[1];
}
static bool match(Lexer* lexer, char expected) {
  if (isAtEnd(lexer) || *lexer->current != expected) return false;
  lexer->current++;
  return true;
}

static Token makeToken(Lexer* lexer, TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer->start;
  token.length = (int)(lexer->current - lexer->start);
  return token;
}

static Token errorToken(const char* message) {
  Token token;
  token.type = TOKEN_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  return token;
}

static void skipWhitespace(Lexer* lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
      case ' ':
      case '\r':
      case '\t':
      case '\n':
        advance(lexer);
        break;
      case '#':
        while (peek(lexer) != '\n' && !isAtEnd(lexer)) {
          advance(lexer);
        }
        break;
      default:
        return;
    }
  }
}

static TokenType checkKeyword(Lexer* lexer, int start, int length,
                              const char* rest, TokenType type) {
  if (lexer->current - lexer->start == start + length &&
      memcmp(lexer->start + start, rest, length) == 0) {
    return type;
  }
  return TOKEN_ID;
}

static TokenType identifierType(Lexer* lexer) {
  switch (lexer->start[0]) {
    case 'a':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'p':
            return checkKeyword(lexer, 2, 1, "e", TOKEN_APE);
          case 'a':
            return checkKeyword(lexer, 2, 1, "h", TOKEN_MINUS);
          case 's':
            return checkKeyword(lexer, 2, 1, "k", TOKEN_ASK);
        }
      }
      break;
    case 'b':
         if (lexer->current - lexer->start > 1) {
            switch (lexer->start[1]) {
               case 'a':
                   return checkKeyword(lexer, 2, 4, "nana", TOKEN_BANANA);
               case 'u':
                   return checkKeyword(lexer, 2, 3, "nch", TOKEN_BUNCH);
            }
         }
        break;
    case 'c':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'a':  // For words starting with "ca"
            if (lexer->current - lexer->start > 2) {
              switch (lexer->start[2]) {
                case 'n':
                  return checkKeyword(lexer, 3, 3, "opy",
                                      TOKEN_CANOPY);  // "canopy"
                case 't':
                  return checkKeyword(lexer, 3, 2, "ch",
                                      TOKEN_CATCH);  // "catch"
              }
            }
            break;
        }
      }
      break;
    case 'e':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'l':
            return checkKeyword(lexer, 2, 2, "se", TOKEN_ELSE);
          case 'e':
            return checkKeyword(lexer, 2, 1, "k", TOKEN_MUL);
        }
      }
      break;
    case 'f':
      return checkKeyword(lexer, 1, 4, "alse", TOKEN_FALSE);
    case 'g':
      return checkKeyword(lexer, 1, 3, "ive", TOKEN_GIVE);
    case 'i':
      return checkKeyword(lexer, 1, 1, "f", TOKEN_IF);
    case 'n':
      return checkKeyword(lexer, 1, 2, "il", TOKEN_NIL);
    case 'o':
      if (lexer->current - lexer->start > 2) {
        if (lexer->start[1] == 'o' && lexer->start[2] == 'h') return TOKEN_PLUS;
        if (lexer->start[1] == 'o' && lexer->start[2] == 'k') return TOKEN_DIV;
      }
      break;
    case 's':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'w':
            return checkKeyword(lexer, 2, 3, "ing", TOKEN_SWING);
          case 'u':
            return checkKeyword(lexer, 2, 4, "mmon", TOKEN_SUMMON);
        }
      }
      break;
    case 't':
      if (lexer->current - lexer->start > 1) {
        switch (lexer->start[1]) {
          case 'r':
            if (lexer->current - lexer->start > 2) {
              if (lexer->start[2] == 'e')
                return checkKeyword(lexer, 3, 1, "e", TOKEN_TREE);
              if (lexer->start[2] == 'u')
                return checkKeyword(lexer, 3, 1, "e", TOKEN_TRUE);
              if (lexer->start[2] == 'i')
                return checkKeyword(lexer, 3, 2, "be", TOKEN_TRIBE);
            }
            break;
          case 'u':
            return checkKeyword(lexer, 2, 4, "mble", TOKEN_TUMBLE);
        }
      }
      break;
      case 'r': 
        return checkKeyword(lexer, 1, 3, "ipe", TOKEN_RIPE);

      case 'y':
        return checkKeyword(lexer, 1, 5, "ellow", TOKEN_YELLOW);

  }
  return TOKEN_ID;
}

static Token identifier(Lexer* lexer) {
  while (isalnum(peek(lexer)) || peek(lexer) == '_') {
    advance(lexer);
  }
  return makeToken(lexer, identifierType(lexer));
}

static Token number(Lexer* lexer) {
  while (isdigit(peek(lexer))) advance(lexer);
  if (peek(lexer) == '.' && isdigit(peekNext(lexer))) {
    advance(lexer);
    while (isdigit(peek(lexer))) advance(lexer);
  }
  return makeToken(lexer, TOKEN_NUM);
}

static Token string(Lexer* lexer) {
  while (peek(lexer) != '"' && !isAtEnd(lexer)) {
    advance(lexer);
  }
  if (isAtEnd(lexer)) return errorToken("Unterminated string.");
  advance(lexer);
  return makeToken(lexer, TOKEN_STRING);
}

Token scanToken(Lexer* lexer) {
  skipWhitespace(lexer);
  lexer->start = lexer->current;

  if (isAtEnd(lexer)) return makeToken(lexer, TOKEN_EOF);

  char c = advance(lexer);
  if (isalpha(c) || c == '_') return identifier(lexer);
  if (isdigit(c)) return number(lexer);

  switch (c) {
    case '(':
      return makeToken(lexer, TOKEN_LPAREN);
    case ')':
      return makeToken(lexer, TOKEN_RPAREN);
    case '{':
      return makeToken(lexer, TOKEN_LBRACE);
    case '}':
      return makeToken(lexer, TOKEN_RBRACE);
    case '[':
      return makeToken(lexer, TOKEN_LBRACKET);
    case ']':
      return makeToken(lexer, TOKEN_RBRACKET);
    case ':':
      return makeToken(lexer, TOKEN_COLON);
    case ',':
      return makeToken(lexer, TOKEN_COMMA);
    case '!':
      return makeToken(lexer,
                       match(lexer, '=') ? TOKEN_BANG_EQUAL : TOKEN_BANG);
    case '=':
      return makeToken(lexer,
                       match(lexer, '=') ? TOKEN_EQUAL_EQUAL : TOKEN_EQUAL);
    case '<':
      return makeToken(lexer,
                       match(lexer, '=') ? TOKEN_LESS_EQUAL : TOKEN_LESS);
    case '>':
      return makeToken(lexer,
                       match(lexer, '=') ? TOKEN_GREATER_EQUAL : TOKEN_GREATER);
    case '"':
      return string(lexer);
  }

  static char errorMsg[60];
  sprintf(errorMsg, "Unexpected character '%c' (ASCII: %d).", c, (int)c);
  return errorToken(errorMsg);
}