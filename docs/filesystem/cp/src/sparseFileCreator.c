
#include <fcntl.h> // Fo creat
#include <sys/stat.h> // For file mode
#include <unistd.h> // For write

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

char buf1[] = "abcdefghij";
char buf2[] = "ABCDEFGHIJ";
int totalSize = 1 << 30; // 1 GiB

int main (void) {
    int fd;

    if ((fd = creat("tmp/file.hole", FILE_MODE)) < 0) {
        write(2, "creat error\n", 12);
    }
    if(write(fd, buf1, 10) != 10) {
        write(2, "write buf1 error\n", 12);
    }
    // Offset is now at 10
    if(lseek(fd, totalSize-10, SEEK_SET) == -1) {
        write(2, "seek error\n", 12);
    }
    // Offset now 1GiB-10
    if(write(fd, buf2, 10) != 10) {
        write(2, "write buf2 error\n", 12);
    }
    // Offset now 1GiB
    return 1; // Will close all open file descriptors
}
