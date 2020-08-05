# Implementing `cp` for sparse files (aKa files with holes)

I'm currently reading the book [Avanced Programming in the UNIX Environment](https://www.amazon.com/Advanced-Programming-UNIX-Environment-3rd/dp/0321637739/). Not only it's amazing and well written, but the authors added a lot of fun exercises to do.
One of them (exercise 4.3) is to write the `cp` command utility, but only for sparse files, or as the author calls them file with holes.

## What are sparse files?

What we call sparse files are regular files, which content is not contiguous, hence having "holes" in the middle.

### Create a file with a hole

How these files can be created ? well that's easy, first you create a file, then you write some content, then you increment the offset, and then you write data again. See the following listing:

```
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
```

First, we create a file in `tmp/file.hole`. Then we will write `abcdefghi`.
Then we advance the cursor for 1GiB-10, and we end up writing 10 more bytes `ABCDEFGHIJ`.

Let's compile and see what's happening on the disk:

```
$ gcc sparseFileCreator.c
$ ./a.out
$ ls -als tmp/
total 16
0 drwxr-xr-x  3 jul  staff          96 Aug  5 06:44 .
0 drwxr-xr-x  7 jul  staff         224 Aug  5 06:44 ..
16 -rw-r--r--  1 jul  staff  1073741824 Aug  5 06:44 file.hole
```

So we only wrote 20 bytes of data, but as you can see in the listing, the overall size is 1073741824 bytes (1 GiB), which makes sense because we extended the pointer to that size.
The interesting part is in the first column. The option `-s` of the `ls` command gives you the number of 512 bytes block allocated for the files (the value of [`stat.st_blocks`](https://pubs.opengroup.org/onlinepubs/009695399/basedefs/sys/stat.h.html)). Which means that in our case, the filesytem only allocated 16*512 = 8192 bytes (8KiB) of storage.

So yeah we have a 1GiB file in size stored on 8KiB, and that's why it's called sparse file, not all the file content is stored on disk, but the filesystem stores it in a smart way. If you try to access for reading anything between the two parts, the filesystem is smart enough and will return 0, just like if actual data where written on disk.

### What happen if you copy that file?

Well, let's find out!

```
$ time cp tmp/file.hole tmp/file_cp.hole
cp tmp/file.hole tmp/file_cp.hole  0.00s user 0.57s system 43% cpu 1.314 total
$ ls -als tmp
total 2105360
      0 drwxr-xr-x  4 jul  staff         128 Aug  5 06:53 .
      0 drwxr-xr-x  7 jul  staff         224 Aug  5 06:53 ..
     16 -rw-r--r--  1 jul  staff  1073741824 Aug  5 06:44 file.hole
2105344 -rw-r--r--  1 jul  staff  1073741824 Aug  5 06:53 file_cp.hole
```

I used the `time` command to measure how much time `cp` is taking. And as we can see, it took `cp` 1.314s to copy the file.
We can verify that both size have the same contents:

```
$ od -c tmp/file.hole
0000000    a   b   c   d   e   f   g   h   i   j  \0  \0  \0  \0  \0  \0
0000020   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
*
7777777760   \0  \0  \0  \0  \0  \0   A   B   C   D   E   F   G   H   I   J
10000000000
$ od -c tmp/file_cp.hole
0000000    a   b   c   d   e   f   g   h   i   j  \0  \0  \0  \0  \0  \0
0000020   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
*
7777777760   \0  \0  \0  \0  \0  \0   A   B   C   D   E   F   G   H   I   J
10000000000
```
The [`od`](https://pubs.opengroup.org/onlinepubs/9699919799/utilities/od.html) command reads a file and display its characters thanks to the `-c` option.

So we do have the same content for both `file.hole` and `file_cp.hole`, they are both the same `stat.st_blocks` size, but the size allocated differs, as the result of the `cp`command was copy byte to byte, it takes at least 1GiB on disk. (it actually takes more 2105344*512=1077936128 which is 1GiB and 4MiB. My guess is that the system stores extra data that ends up being 4MiB for a 1GiB).

*Note* These tests are run on macOS, and the `cp` utility doesn't have the option to copy sparse files (`--sparse`) as [the one on linux](https://man7.org/linux/man-pages/man1/cp.1.html) does.

## Implementing `cp`

Let's try to implement our own implementation of `cp` that correct that behavior.

```
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
```

First we make we have two arguments, that the first one exists and can be read and we open the second argument for writing.
Then we create a buffer of the size of the most adequate block for reading the file (`stat.st_blksize`), and read chunk of the file to copy into that buffer. If that buffer is filled with 0, that means that we can skip writing bytes for that block and just use a `lseek` that will move forward the current file offset (without provoking any I/O as this information is stored in the file table entry in the memory of the OS and not on the file system). Of course if there is anything else than 0, we write that buffer to the output file.

## Performances

```
$ gcc main.c
$ time ./a.out tmp/file.hole tmp/file_cp2.hole
./a.out tmp/file.hole tmp/file_cp2.hole  2.35s user 0.29s system 99% cpu 2.652 total
$ ls -als tmp
total 2105376
      0 drwxr-xr-x  5 jul  staff         160 Aug  5 07:13 .
      0 drwxr-xr-x  7 jul  staff         224 Aug  5 07:12 ..
     16 -rw-r--r--  1 jul  staff  1073741824 Aug  5 06:44 file.hole
2105344 -rw-r--r--  1 jul  staff  1073741824 Aug  5 06:53 file_cp.hole
     16 -rw-r--r--  1 jul  staff  1073741824 Aug  5 07:13 file_cp2.hole
```

So as you can see, the file copied by our program is the same size, but have only 16 blocks of 512 bytes allocated on disk, just likeb the original. Let's verify the content:

```
$ od -c tmp/file.hole
0000000    a   b   c   d   e   f   g   h   i   j  \0  \0  \0  \0  \0  \0
0000020   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
*
7777777760   \0  \0  \0  \0  \0  \0   A   B   C   D   E   F   G   H   I   J
10000000000
$ od -c tmp/file_cp2.hole
0000000    a   b   c   d   e   f   g   h   i   j  \0  \0  \0  \0  \0  \0
0000020   \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0  \0
*
7777777760   \0  \0  \0  \0  \0  \0   A   B   C   D   E   F   G   H   I   J
10000000000
```

Our own copy took longer to execute (2.652s vs 1.314s), but most of the time comes for the user time (what we are doing in the program), 0s for `cp` vs. 2.35s for our verison of copy. That comes from the fact that we have to go through each bytes to check if they are equals to 0. I'm sure we can be way more clever here and do lots of improvements to that version, but we can't escape the fact that we have to check that each and every bytes is equal to 0.
But the system time was divided by almost 2, as we spend way less time in kernel space to write data.

## References

* If you are curious about the C implementation of `cp` in Linux: https://github.com/coreutils/coreutils/blob/master/src/cp.c
