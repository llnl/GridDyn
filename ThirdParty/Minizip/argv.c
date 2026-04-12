#include "argv.h"

char **copy_argv(int argc, const char** argv) {
    int i;
    char **new_argv;

    new_argv = malloc((argc+1) * sizeof(const char*));
    for(i = 0; i < argc; ++i) {
        const char *item = argv[i];
        unsigned len = strlen(item);

        new_argv[i] = malloc((len+1) * sizeof(char));
        memcpy(new_argv[i], item, len);
        new_argv[i][len] = '\0';
    }
    new_argv[argc] = NULL;
    return new_argv;
}

void free_argv(int argc, char **new_argv) {
    int i;

    for(i = 0; i < argc; ++i) {
        free(new_argv[i]);
    }
    free(new_argv);
}
