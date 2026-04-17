#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

void init_repo() {
    // Check if directory exists, if not, create it
    if (mkdir(".myvcs", 0777) == -1) {
        printf("Error: Repository already initialized or permission denied.\n");
    } else {
        mkdir(".myvcs/objects", 0777);
        printf("Initialized empty VCS repository in .myvcs/\n");
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: ./vcs <command> [args]\n");
        return 1;
    }

    if (strcmp(argv[1], "init") == 0) {
        init_repo();
    } else if (strcmp(argv[1], "add") == 0) {
        // We will implement this next
        printf("Add command called for: %s\n", argv[2]);
    } else {
        printf("Unknown command: %s\n", argv[1]);
    }

    return 0;
}