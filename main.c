#include "common.h"
#include "compiler.h"
#include "vm.h"
#include "debug.h"

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

  if (strcmp(command, "compile") == 0 && argc == 3) {
    compileCommand(argv[2]);
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
