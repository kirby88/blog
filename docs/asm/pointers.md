# Pointers vs. references in C++ from an assembly perspective

## What is this post about?

I always heard that technically pointers and references are the same things, and I wanted to see how this was true on an assembly level.

## First things first, what is a pointer/reference?

Let's start with some code. We will start with the most basic code you can imagine:

```cpp
int main()
{
   int a=1;
   int &b=a;
   b=0;
   return a;
}
```

This code means that we create a variable a, and we initialize its value to 1.
Then we create a reference to that variable and we call it b.
We then set b to 0, which means we take b, look at the variable its pointing (ie a), and set that value to 0.
Finally we return a.

So if we compile this code `gcc -Wall main.cpp -o main` and then execute it:

```
$ ./main
$ echo $?
0
```

We get 0 as the return code (`echo $?` reads the exit status of the last command executed).

# Under the hood: LLDB to the rescue

We can now attach `lldb` to our program to understand what's happening under the hood:

```
$ lldb main
(lldb) target create "main"
Current executable set to '/Users/kirby88/Developer/Blog/src/main' (x86_64).
```

Once it launches, let's set a breakpoint on the main function (`$ b main`), and run the process (`$ r`):

```
(lldb) b main
Breakpoint 1: where = main`main, address = 0x0000000100000f80
(lldb) r
Process 27473 launched: '/Users/kirby88/Developer/Blog/src/main' (x86_64)
Process 27473 stopped
* thread #1, queue = 'com.apple.main-thread', stop reason = breakpoint 1.1
    frame #0: 0x0000000100000f80 main`main
main`main:
->  0x100000f80 <+0>:  push   rbp
    0x100000f81 <+1>:  mov    rbp, rsp
    0x100000f84 <+4>:  mov    dword ptr [rbp - 0x4], 0x0
    0x100000f8b <+11>: mov    dword ptr [rbp - 0x8], 0x1
Target 0: (main) stopped.
```

Now let's disassemble:

```asm
(lldb) disas
main`main:
->  0x100000f80 <+0>:  push   rbp
    0x100000f81 <+1>:  mov    rbp, rsp
    0x100000f84 <+4>:  mov    dword ptr [rbp - 0x4], 0x0
    0x100000f8b <+11>: mov    dword ptr [rbp - 0x8], 0x1
    0x100000f92 <+18>: lea    rax, [rbp - 0x8]
    0x100000f96 <+22>: mov    qword ptr [rbp - 0x10], rax
    0x100000f9a <+26>: mov    rax, qword ptr [rbp - 0x10]
    0x100000f9e <+30>: mov    dword ptr [rax], 0x0
    0x100000fa4 <+36>: mov    eax, dword ptr [rbp - 0x8]
    0x100000fa7 <+39>: pop    rbp
    0x100000fa8 <+40>: ret
```

### Function prologue

Ok now we are talking! the two first lines are the function prologue: it pushes the stack base pointer `$RBP` on the stack and set the stack pointer `$RSP` to `$RBP`. It basically means that the base pointer and the stack pointer are pointing to the same address in memory, which is the begining of the stack.

To be sure about that, let's execute our two instructions (`$ si` will execute a single instruction), and then let's display the register (`$ register read`):

```assembly
(lldb) si -c2
(...)
(lldb) register read
General Purpose Registers:
       (...)
       rbp = 0x00007ffeefbff8f0
       rsp = 0x00007ffeefbff8f0
       (...)
```

Good, we are all set!

### Declaring a

Now, we need to declare and assign a value to `a`. this is done by the two following line:

```asm
mov    dword ptr [rbp - 0x4], 0x0
mov    dword ptr [rbp - 0x8], 0x1
```

Let's break it. `mov` takes two operands, separate by a comma, and take a value (2nd argument) to put it in somewhere (1st argument, register or memory address).
Then, we have this `dword ptr [rbp - 0x4]`. First `ptr` means pointer, and will take the value in `$rbp - 0x4`. Remember after the function prologue, rbp is pointing to the first address in the stack. Because the stack grows downards (towards 0), $rbp - 0x04 is pointing to the begining of the stack "+" 4 bytes. So $rbp-4 is just a value, but ptr[rbp -4] is the actual address in the stack. dword just means that the length we are pointing at is 4 bytes (qword = 8).

So here, we are first set the first 4 bytes to 0, and the next 4 bytes to the value int (32 bits) 1.

Let's execute these two line and check in memory that it's the case (`x` is an alias for `memory read`, `/x` means format in hexadecimal, `-s4` is the number of bytes we want to read):

```
(lldb) si -c2
(...)
(lldb) x/x -s4 $rbp-4
0x7ffeefbff8cc: 0x00000000
(lldb) x/x -s4 $rbp-8
0x7ffeefbff8cc: 0x00000001
```

What I don't know is why we need to set the first bytes to 0. Because we could have set a qword of 0x1, result would have been the same, or we could simply have used $rbp-4 which could have been the same things.
Maybe it has something to do with having value of 64bits at the end, it's faster for the processor to compute, but then why even bother settings these four first bytes as they are never referenced later!

### Declaring b

From there, we will load in `rax` the address of `rbp-8` (this is done with the `lea` opcode), and on the following line, we are saving at `rbp-0x10` that address. So we take the reference of the value in the variable `a` and we save it onto the stack (in the variable b).

From there, we want to access the value pointed by b and set to 0. So first we load in `rax` the address that is present at `rbp-0x10` (remember, it was pointing to `rbp-8`).
Then we set to that address the int value 0.

Finally we move to `eax` (aka the return register) the value of a, i.e. the value at `rbp-8`.
