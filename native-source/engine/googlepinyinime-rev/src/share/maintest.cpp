#include <stdio.h>
#include <iostream>

int main() {
    FILE* file = fopen("example.txt", "w");
    if (file) {
        fprintf(file, "Hello, World!\n");
        fclose(file);
    } else {
        perror("Failed to open file");
    }
    return 0;
}