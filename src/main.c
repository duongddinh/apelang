#include "common.h"
#include "compiler/compiler.h"
#include "vm/vm.h"
#include "debug/debug.h"
#include "lexer/lexer.h"

static char* paths[1024];
static int states[1024], pathCount;

static char* readFile(const char* path, size_t* size) {
    FILE* f = fopen(path, "rb");
    if (!f || fseek(f, 0, SEEK_END)) { fprintf(stderr, "Cannot read %s\n", path); exit(74); }
    long n = ftell(f);
    if (n < 0 || n > 1024 * 1024) { fprintf(stderr, "File too large: %s\n", path); exit(74); }
    rewind(f);
    char* data = malloc((size_t)n + 1);
    if (!data || fread(data, 1, n, f) != (size_t)n) exit(74);
    fclose(f); data[n] = 0; *size = n; return data;
}

static void compileFile(const char* path) {
    char* canonical = realpath(path, NULL);
    if (!canonical) { fprintf(stderr, "Cannot resolve source: %s\n", path); exit(74); }
    int slot;
    for (slot = 0; slot < pathCount; slot++) if (!strcmp(paths[slot], canonical)) break;
    if (slot < pathCount) {
        free(canonical);
        if (states[slot] == 1) { fprintf(stderr, "Circular summon dependency: %s\n", path); exit(65); }
        return;
    }
    if (pathCount == 1024) { fprintf(stderr, "Too many dependencies.\n"); exit(65); }
    paths[pathCount++] = canonical; states[slot] = 1;
    size_t len = strlen(canonical), size;
    if (len < 4 || strcmp(canonical + len - 4, ".ape")) { fprintf(stderr, "Expected .ape source.\n"); exit(64); }
    char* source = readFile(canonical, &size);
    Lexer lexer; initLexer(&lexer, source);
    for (;;) {
        Token token = scanToken(&lexer);
        if (token.type == TOKEN_EOF || token.type == TOKEN_ERROR) break;
        if (token.type == TOKEN_SUMMON) {
            Token next = scanToken(&lexer);
            if (next.type == TOKEN_STRING) {
                size_t prefix = next.start[1] == '/' ? 0 : (size_t)(strrchr(canonical, '/') - canonical + 1);
                char* dep = malloc(prefix + next.length);
                if (!dep) exit(74);
                memcpy(dep, canonical, prefix);
                memcpy(dep + prefix, next.start + 1, next.length - 2);
                dep[prefix + next.length - 2] = 0;
                compileFile(dep); free(dep);
            }
        }
    }
    char* bytecode = NULL; size_t bytes = 0;
    FILE* memory = open_memstream(&bytecode, &bytes);
    if (!memory) exit(74);
    bool ok = compile(source, memory, false);
    int closed = fclose(memory);
    free(source);
    if (!ok || closed) { free(bytecode); exit(65); }
    if (!validateBytecode((uint8_t*)bytecode, bytes)) { fprintf(stderr, "Compiler produced invalid bytecode.\n"); exit(65); }
    char* output = strdup(canonical);
    memcpy(output + len - 4, ".apb", 5);
    FILE* f = fopen(output, "wb");
    if (!f || fwrite(bytecode, 1, bytes, f) != bytes || fclose(f)) exit(74);
    printf("Compiled: %s\n", output);
    free(output); free(bytecode); states[slot] = 2;
}

static void stats(VM* vm) {
    printf("\n-- Apelang Stats --\nMemory: %zu bytes\nStack Depth: %d\nGC Cycles: %d\n", vm->bytesAllocated, vm->maxFrameCount, vm->gcCycles);
}

int main(int argc, const char* argv[]) {
    if (argc == 3 && !strcmp(argv[1], "compile")) {
        compileFile(argv[2]);
        for (int i = 0; i < pathCount; i++) free(paths[i]);
        return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "disassemble")) {
        size_t size; uint8_t* bytes = (uint8_t*)readFile(argv[2], &size);
        if (!validateBytecode(bytes, size)) { free(bytes); fprintf(stderr, "Invalid bytecode.\n"); return 65; }
        disassembleBytecode(argv[2], bytes, (int)size); free(bytes); return 0;
    }
    if (argc == 3 && !strcmp(argv[1], "run")) {
        VM vm; initVM(&vm);
        VMResult result = runBytecode(&vm, argv[2]);
        stats(&vm); freeVM(&vm); return result == VM_RESULT_OK ? 0 : 70;
    }
    if (argc == 2 && !strcmp(argv[1], "repl")) {
        VM vm; initVM(&vm); char line[4096];
        puts("Apelang REPL. Type 'exit' to quit.");
        while (printf(">> "), fflush(stdout), fgets(line, sizeof(line), stdin)) {
            if (!strcmp(line, "exit\n")) break;
            interpret(&vm, line);
        }
        stats(&vm); freeVM(&vm); return 0;
    }
    fprintf(stderr, "Usage: apeslang compile <file.ape> | run <file.apb> | disassemble <file.apb> | repl\n");
    return 64;
}
