#include "common.h"
#include "compiler.h"
#include "vm.h"

// Reads a file into a dynamically allocated string.
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

  int success = compile(source, outputPath);
  free(source);

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
  if (result == VM_RESULT_OK) {
  } else {
    fprintf(stderr, "Execution failed due to a runtime error.\n");
    exit(70);
  }
}

int main(int argc, const char* argv[]) {
  if (argc < 3) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  apeslang compile <file.ape>\n");
    fprintf(stderr, "  apeslang run <file.apb>\n");
    return 64;
  }

  const char* command = argv[1];
  const char* path = argv[2];

  if (strcmp(command, "compile") == 0) {
    compileCommand(path);
  } else if (strcmp(command, "run") == 0) {
    runCommand(path);
  } else {
    fprintf(stderr, "Unknown command '%s'.\n", command);
    return 64;
  }

  return 0;
}
