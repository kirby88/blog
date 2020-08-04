#include <fcntl.h> // Fo open
#include <sys/stat.h> // For stat
#include <unistd.h> // For read/write

#define FILE_MODE (S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH)

int main(int argc, char **argv) {
    if(argc != 3) {
        write(2, "Error, command need two arguments. Usage: cp from to\n", 53);
        return -1;
    }
    int fdFrom;
    int fdTo;
    struct stat statBuf;
    // Opening sparse file in read mode only
    if((fdFrom = open(argv[1], O_RDONLY)) < 0) {
        write(2, "open error\n", 12);
    }
    // Stat buf to get optimal blksize
    if(stat(argv[1], &statBuf) < 0) {
        write(2, "stat error\n", 12);
    }
    // Opening the copy in write only mode, create if doesn't exists.
    if((fdTo = open(argv[2], O_WRONLY | O_CREAT, FILE_MODE)) < 0) {
        write(2, "write error\n", 13);
    }
    char buf[statBuf.st_blksize]; // Buffer is allocated on the heap

    // We are going to read every block
    int offset = 0;
    while(read(fdFrom, buf, statBuf.st_blksize) > 0) {
        int i=0;
        while(i < statBuf.st_blksize && !buf[i++]) {}

        // If i != stat.st_blksize, that means that the buffer contains
        // something else than 0, so we copy it.
        // Else, we will create a hole in the copy.
        if(i != statBuf.st_blksize) {
            write(fdTo, buf, statBuf.st_blksize);
        } else {
            lseek(fdTo, offset + statBuf.st_blksize, SEEK_SET);
        }
        offset += statBuf.st_blksize;
    }
   return 1;
}
