#include "common.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"
#include "lexer.h" 

static char* processedFiles[1024];
static int processedCount = 0;
static char* readFile(const char* path);
char** findDependencies(const char* source, int* count);
void compileWithDependencies(const char* ape_path);

bool hasBeenProcessed(const char* path) {
    for (int i = 0; i < processedCount; i++) {
        if (strcmp(processedFiles[i], path) == 0) return true;
    }
    return false;
}

void markAsProcessed(const char* path) {
    if (processedCount < 1024) {
        processedFiles[processedCount++] = strdup(path);
    }
}

// The main recursive compilation driver.
void compileWithDependencies(const char* ape_path) {
    // 1. Avoid re-compiling or circular dependencies.
    if (hasBeenProcessed(ape_path)) {
        return;
    }

    printf("-> Processing: %s\n", ape_path);

    char* source = readFile(ape_path); // Assumes readFile is accessible
    if (source == NULL) {
        fprintf(stderr, "Error: Could not open source file '%s'.\n", ape_path);
        exit(71);
    }
    
    // 2. Find and recursively compile all dependencies first.
    int dep_count = 0;
    char** deps = findDependencies(source, &dep_count);
    free(source); // Free the source after scanning
    
    for (int i = 0; i < dep_count; i++) {
        compileWithDependencies(deps[i]);
        free(deps[i]); // Free the path string
    }
    free(deps);

    // 3. After dependencies are met, compile the current file.
    printf("=> Compiling: %s\n", ape_path);
    char path_apb[1024];
    strncpy(path_apb, ape_path, sizeof(path_apb) - 1);
    // Replace .ape with .apb
    char* dot = strrchr(path_apb, '.');
    if (dot && strcmp(dot, ".ape") == 0) {
        strcpy(dot, ".apb");
    } else {
        strcat(path_apb, ".apb");
    }

    source = readFile(ape_path); // Read the source again for compilation
    FILE* outFile = fopen(path_apb, "wb");
    if (outFile == NULL) {
        fprintf(stderr, "Error: Unable to open output file '%s'.\n", path_apb);
        exit(71);
    }

    if (compile(source, outFile, false)) { // This is your existing compile function
        printf("   Success: %s -> %s\n", ape_path, path_apb);
    } else {
        fprintf(stderr, "   Failure: Could not compile %s.\n", ape_path);
        exit(65);
    }

    fclose(outFile);
    free(source);
    
    // 4. Mark this file as done.
    markAsProcessed(ape_path);
}
char** findDependencies(const char* source, int* count) {
    *count = 0;
    Lexer lexer;
    
    // First pass: count the dependencies to know how much memory to allocate.
    initLexer(&lexer, source);
    for (;;) {
        Token token = scanToken(&lexer);
        // Look for the pattern: summon "some/path.ape"
        if (token.type == TOKEN_SUMMON) {
            Token nextToken = scanToken(&lexer);
            if (nextToken.type == TOKEN_STRING) {
                (*count)++;
            }
        }
        if (token.type == TOKEN_EOF) break;
    }

    if (*count == 0) return NULL;

    char** deps = malloc(sizeof(char*) * (*count));
    int deps_idx = 0;

    // Second pass: extract the dependency paths into the list.
    initLexer(&lexer, source);
    for (;;) {
        Token token = scanToken(&lexer);
        if (token.type == TOKEN_SUMMON) {
            Token pathToken = scanToken(&lexer);
            if (pathToken.type == TOKEN_STRING) {
                // Extract the path from the string literal (e.g., "math.ape" -> math.ape)
                int len = pathToken.length - 2;
                char* path = malloc(len + 1);
                strncpy(path, pathToken.start + 1, len);
                path[len] = '\0';
                deps[deps_idx++] = path;
            }
        }
        if (token.type == TOKEN_EOF) break;
    }
    return deps;
}



static char* readFile(const char* path) {
  FILE* file = fopen(path, "rb");
  if (file == NULL) {
    fprintf(stderr, "Could not open file \"%s\".\n", path);
    exit(74);
  }
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

static void runRepl() {
    VM vm;
    initVM(&vm); // Initialize the VM once for the whole session
    char line[1024];
    printf("Apeslang Interactive REPL. Type 'exit' to quit.\n");
    for (;;) {
        printf(">> ");
        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }
        if (strcmp(line, "exit\n") == 0) {
            break;
        }

        // Interpret the line of code within the persistent VM
        interpret(&vm, line);
    }
    freeVM(&vm); // Free the VM when the session ends
}

static void compileCommand(const char* sourcePath) {
  char outputPath[256];
  strncpy(outputPath, sourcePath, sizeof(outputPath) - 1);
  char* dot = strrchr(outputPath, '.');
  if (dot == NULL || strcmp(dot, ".ape") != 0) {
    fprintf(stderr, "Error: Source file for compilation must have a .ape extension.\n");
    exit(64);
  }
  strcpy(dot, ".apb");

  printf("Compiling %s to %s...\n", sourcePath, outputPath);
  char* source = readFile(sourcePath);
  
  FILE* outFile = fopen(outputPath, "wb");
  if (outFile == NULL) {
      fprintf(stderr, "Could not open output file \"%s\".\n", outputPath);
      exit(74);
  }

  // Pass 'false' for isRepl when compiling a file
  int success = compile(source, outFile, false);
  free(source);
  fclose(outFile);

  if (success) {
    printf("Compilation successful. 🦍🍌\n");
  } else {
    fprintf(stderr, "Compilation failed.\n");
    exit(65);
  }
}

static void runCommand(const char* bytecodePath) {
  if (strrchr(bytecodePath, '.') == NULL || strcmp(strrchr(bytecodePath, '.'), ".apb") != 0) {
    fprintf(stderr, "Error: File for execution must have a .apb extension.\n");
    exit(64);
  }

  VMResult result = runBytecode(bytecodePath);
  if (result != VM_RESULT_OK) {
    fprintf(stderr, "\nExecution failed.\n");
    exit(70);
  }
}

static void disassembleCommand(const char* path) {
    if (strrchr(path, '.') == NULL || strcmp(strrchr(path, '.'), ".apb") != 0) {
        fprintf(stderr, "Error: File for disassembly must have a .apb extension.\n");
        exit(64);
    }

    FILE* file = fopen(path, "rb");
    if (!file) {
        fprintf(stderr, "Could not open file \"%s\".\n", path);
        exit(74);
    }

    fseek(file, 0L, SEEK_END);
    long fileSize = ftell(file);
    rewind(file);

    uint8_t* bytecode = (uint8_t*)malloc(fileSize);
    if(bytecode == NULL) {
        fprintf(stderr, "Not enough memory to read \"%s\".\n", path);
        exit(74);
    }

    fread(bytecode, sizeof(uint8_t), fileSize, file);
    fclose(file);

    disassembleBytecode(path, bytecode, fileSize);
    free(bytecode);
}

int main(int argc, const char* argv[]) {
  if (argc < 2) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  apeslang compile <file.ape>\n");
    fprintf(stderr, "  apeslang run <file.apb>\n");
    fprintf(stderr, "  apeslang repl\n");
    fprintf(stderr, "  apeslang disassemble <file.apb>\n");
    return 64;
  }

  const char* command = argv[1];

   if (argc == 3 && strcmp(argv[1], "compile") == 0) {
        compileWithDependencies(argv[2]);
        // Clean up memory from the processed files list
        for (int i = 0; i < processedCount; i++) {
            free(processedFiles[i]);
        }
    } else if (strcmp(command, "run") == 0 && argc == 3) {
    runCommand(argv[2]);
  } else if (strcmp(command, "repl") == 0 && argc == 2) {
    runRepl();
  } else if (strcmp(command, "disassemble") == 0 && argc == 3) {
    disassembleCommand(argv[2]);
  } else {
    fprintf(stderr, "Unknown command or incorrect number of arguments.\n");
    return 64;
  }

  return 0;
}
