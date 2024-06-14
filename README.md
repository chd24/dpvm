# dpvm
Original bytecode virtual machine with on-the-fly translation to x86-64 machine code.

The name is acronym of Deterministic Parallel Virtual Machine. The main principle is that output of each function depends only on its inputs and not on any side parameters. For example, such functions as `scanf()`, `time()` and so on are not possible in dpvm machine. However, function can produce side effects, i.e. launch other threads which not affects to output of the function. As a consequence of main principle, if some object is shared between two threads, then it must be unchangeable. Another consequence is that there is no need in mutexes and other synchronization primitives.

## Build and run

### Download
```
<Install cmake, make and gcc>
$ git clone https://github.com/cdy21/dpvm.git
```

### Build
```
$ cd dpvm/build
$ cmake ..
$ make
```

### Run
```
$ src/dpvm
<Wait several minutes for translating main bytecode functions>
dpvm> help
<Type other commands or expressions>
```

## Programs for dpvm machine

There are several examples of programs for dpvm machine written in C-like language. Some of them are real services actually running on author's cloud server. Sources of programs are placed in the folder `dpvm/dpvm`. Compiled bytecode programs are placed in the lmdb database `build/data.mdb` which is linked to running dpvm machine, so one can run already compiled programs.

### Compiler

Placed in `compiler` folder. This is main internal tool to compile C-like dpvm program into bytecode. Compiler itself can be used to compile single file without includes. Source files have extension `.dpvm`. For example, to compile hello-world program, type the following command via dpvm prompt. All paths are given relative to `build` folder where dpvm is running from.
```
dpvm> compile ../dpvm/examples/hello.dpvm
pertly_seabird_ibsen
```
Here `pertly_seabird_ibsen` is output of typed command. This is short hash of the compiled function which consists of three vocabulary words with two underlines between them. One can regard short hash just as name of function. To run this function, type its name with empty parameters list.
```
dpvm> pertly_seabird_ibsen()
Hello, world!
any ()
```
There text `Hello, world!` is side effect of this function which appeared in terminal because of `outputp` I/O bytecode. And `any ()` is result of the function where `any` is type of result. One can find other examples in `examples` folder.

### Interpreter

One can type at dpvm prompt any expression which will be interpreted and result will be outputed. This functionality is done by interpreter which puts expression into in-fly generated program, the compiles and executes it. For example, type.
```
dpvm> 2*2
any (4)
```

### Maker

Actually complex programs consists of several `.dpvm` files which can include other files using `#include` directive. Maker is used to compile such a program. Project must have makefile which typically has `.dpvmake` extension. Makefile contains constant and type declarations and also several sections of such a kind.
```
/*@ "filename.dpvm": description of the function */
(input parameters) -> (output parameters) function_name = some_short_hash;
/* @*/ 
```
There function_name is not short hash, but human readable function name, and some_short_hash is short hash of the function. This short hash will be automatically written into makefile by the maker. One can just omit short hash for the first version of makefile. Such a sections should be listed in strict order: function declared later can use human readable names of functions declared earlier, but not vesa versa.

Simple example of project is contained in `examples/wget` folder. This program connects to given server, sends HTTP request, reads reply and saves it into given file. To make the project, type
```
dpvm> make ../dpvm/examples/wget/wget.dpvmake
winning_disobeys_enforceable
```
To run compiled program enter, for example
```
dpvm> winning_disobeys_enforceable("info.cern.ch", "/", "cern.htm", 80)
Task finished.
```
The result will be the contents of root page `/` downloaded from server `http://info.cern.ch` with default port `80` and saved into local file `build/cern.htm`. However, output contains HTTP reply headers which can be removed as an excersize.

Note that all input-output in dpvm is asynchronous. When I/O bytecode such as connect/input/output/read/write appeared in a function, dpvm runs separate thread to perform such an I/O operation, and calls the given function with the given data after the completion of operation (function is the first paramater, and data is the second one of bytecode). There are two forms of I/O bytecodes. First is without letter 'p' at the end of bytecode name, for example, `output`. When such a bytecode executed, original thread is terminated without any output, and execution continues in another thread after I/O operation completed. The second type is with letter 'p' at the end of bytecode name, for example `outputp`. Such an operation is executed in parallel to the original thread with the following exception: if the given function (first parameter) is empty, then the original thread continues after I/O operation completes.

### Checker, translator and monitor

These 3 dpvm programs are built-in into the dpvm machine. This means that each of them is a program for dpvm machine, its bytecode is stored the lmbd database `build/data.mdb`, and source code of dpvm machine (namely, the file `src/config.c`) contains full hash of such a function. If one changes such a program, then new program should be stored into database, its hash written into config, and dpvm machine recompiled.

Checker is used by dpvm machine to check each function before its first call. Checker requires that array of object types in the stack at each point of function's execution not depend on an execution brach which lead to this point. It also requires that types of input and output objects for each bytecode, and also types for input and output objects for the function itself matches object types which are on the top of stack. Checker is also called by compiler as the last phase of compilation of a function. 

Translator is called by dpvm machine after the checker. It translates bytecode of a function to native machine code of host system. As for now, only x86-64 architecture is supported by translator. If an architecture is not supported, then bytecode interpreter is running instead of translation. This is much slower. There are 2 methods of translation at present: slow and fast. Slow method works with any bytecode which passed the checker, but it stores top of stack in CPU register, and all previous variables in native machine stack. Fast method interprets bytecode as graph, simplifies it, and uses all CPU registers, but it works with integer programs without branching as for now.

Dpvm machine launches monitor at the beginning of its work. Monitor maintains interactive command line environment. This means that it outputs `dpvm>` prompt, inputs user text, and interprets it as command or expression. For these purposes monitor calls compiler, interpreter and maker. Monitor supports several input streams. One can connect to monitor using telnet to port 15395, and type the same set of commands there.

### Libraries

There are several folders whose contain library functions. The core all-purpose functions such as string manipulations and converters between strings and numbers are placed in `stdlib` folder. To use such a library function by its human readable name, for example, the function `strcat` which adds second string to the end of the first, one should typically put the following include directive into his program:
```
#include "../stdlib/stdlib.dpvmake"
```

The `utils` folder contains short standalone programs whose typically used directly from command line interpreper. For sush a use one should look into the makefile `utils/utils.dpvmake`, find short hash name of the function and use it in the interpreter instead of human readable name. For example, we want to find full hash of checker with purpose to write it info the file `config.c`. For this task we use `getfullhash()` function which has short hash name `mongoose_piracy_legging`. At the same time short hash of checker is `usefulness_consummately_poseidon` which can be extracted from the file `checker/checker.dpvmake`. So we can type the following and obtain full hash.
```
dpvm> mongoose_piracy_legging(usefulness_consummately_poseidon)
any (0xffd2e0693f2a92df,0xa724903e504dbb32,0x1f064d72eb2e28a7,0x9b2e69282489cfaf)
```

The `math` folder contains various mathematical functions. For example, subfolder `math/long` contains implementation of long arithmetic. It includes main RSA cryptography function namely computation of power by module. Methods of Montgomery and Karatsuba were used to optimize such a computation. The `math/crypto` subfolder contains some other crypto algorithms such as CRC32 and sha256.

### Network applications

There are several network applications written for dpvm machine whose are running on author's cloud server. Below are examples on how to run such an application from command line.

#### WWW server

HTTP server, parameters are: root folder of the site, default page, log file, host name, last parameters are: port number, server mode and local time offset.
```
dpvm> crates_fencings_shielding("/var/www/root", "index.html", "/var/log/access.log", "mysite.org", "", "", "", 80, 2, -5)
```

HTTPS server, new parameters are: public key of the host (`crt` file) and banch of private and public keys (`pem` file). 
```
dpvm> crates_fencings_shielding("/var/www/root", "index.html", "/var/log/access.log", "mysite.org", "/var/www/mysite.crt", "/root/mysite.pem", "", 443, 3, -5)
```
Note that for HTTPS node program 'sslproxy` from `utils` repository should be compiled and put into `cgi-bin` subfolder of the site root.

#### News log

Collects news from several RRS feeds and put them into log file. Parameters are: text file with feeds list (see example in the file `dpvm/newslog/feeds/.txt`), log file for output, local time offset.
```
dpvm> stockcar_fitness_murderous("/opt/dpvm/dpvm/newslog/feeds.txt", "/var/log/news.log", -5)
```

#### p2p

As for now, this program can maintain network connections between dpvm machines and broadcast any dpvm objects created on one machine (for example, results of function compilation) to other. So we do not need transfer bytecode from one dataabse to another manually. Parameters are: file which contains list or known hosts in format host:port, log file, name of local host, incoming port for p2p connections, flags, local time offset and logging level.
```
dpvm> inhabit_hue_octopus("p2p_hosts.txt", "/var/log/p2p.log", "mysite.org", 16949, 1, -5, 7)
```

## Internals

### Object

Any entity inside dpvm machine is regarded as object. Object is unity of 4 separate stacks each of which contains fields of corresponding basic types: 1) links to other objects or to the object itself; 2) 8-byte signed integers; 3) 8-byte double-precision float numbers; 4) 1-byte characters. Link to an object is its hash. Length of a hash is 32 bytes. These four basic types are fixed in current implementation, but not fixed in theory. There is allowed an extension in which, for example, integers will be 16-byte and so on.

### Type

Each object has a type. The type defines minimum and maximum size of each of four stacks, and types of links. Type of object is an object too. This object contains 16 integers and not less than 4 links to types of linked objects. First 4 integers are minimum values of length in bytes of basic types for 4 stacks, next 4 numbers ar maximum values for them. Next 4 numbers are minimum values for sizes of corresponding stacks, and the last 4 numbers are maximum values for these sizes.

Each link from a type outside first 4 is the type of corresponing link of object. For example, 4-th link of the type corresponds to 0-th link of the object etc. 2-nd link of type defines a type for all linked objects at even positions, for which type was not defined earlier by direct type of link, 3-rd link of type corresponds to linked objects at odd positions. Link 0 is the type of input, if the object is function, link 1 in the type of its output.

For example, type of all types is the object this contains the following 4 links (`this` means link to itself) and 16 integers:
```
this this this this 32 8 8 1 32 8 8 1 4 16 0 0 LONG_MAX 16 0 0
```
Type of this object is link to itself (`this`). This object is predefined.

Another predefined object is `any` - most wide type. Object of any type can be casted to the type `any`. The `any` type as an object consists of the following fields:
```
this this this this 1 1 1 1 LONG_MAX LONG_MAX LONG_MAX LONG_MAX 0 0 0 0 LONG_MAX LONG_MAX LONG_MAX LONG_MAX
```
Type of object `any` is type of all types defined earlier.

### Hash

Each object has a hash. Hash is unique index of the object. Hash is calculated from serialized representation of the object using Blake 2b algorithm. Length of hash is 32 bytes. Serialized representaion of object has the following format: lenghts in bytes of all 4 basic types (each length is 8-byte integer), lenghts of 4 stacks (8-byte integers), hash of object type, hashes of links, integer numbers, float numbers, characters. If link is to the object itself, then hash is filled by all zeroes.

Short hash is 47 lower bits of hash. Short hash has string representation consisting of 3 words lowercase from fixed dictionary and 2 underline characters between words. At present, objects are stored in the database in serialized format using short hash as a key, but this may be changed to use full hash as key.

### Function

Function is main unit of execution in dpvm machine. Function is an object too. Function consists of bytecode which is stored in characters stack, and constants (linked objects, integers, floats) whose are stored in 3 other stacks. Type of function contains type of input object (as link 0) and type of output object (as link 1) of this function. Input object of function is all input parameters of the funcfion placed into the single object, output object is the same thing for output parameters.

For example, let a function has one object of type `any` and 2 integers as input parameters, and 1 integer and 1 float as output parameters. The type of such a function can be written in a program as
```
(any obj, int param1, int param2) -> (int output_param, float output_float)
```
We can place input parameters into single object which consists of one link and 2 integers. Type of this input object is
```
this this this this any 32 8 8 1 32 8 8 1 1 2 0 0 1 2 0 0
```
This type is 0-th link of type of the function. The same thing is for output object which corresponds to 1-st link of type of the function.

### Execution

Dpvm machine execution environment consists of threads. Each thread has stack of execution stacks. Each execution stack corresponds to a function which is executed in this thread. Function can call another function. When this occurs, new execution stack is created for newly called function, and this execution stack is pushed to the stack of stacks. When a function returnes execution to previous function, execution stack is popped from the stack of stacks. Threads can create another threads by using I/O bytecodes. Threads are groupped into tasks. Newly created thread generally belongs to the same task as the creator. The exception from this rule is `setsys` I/O bytecode called with one of parameter ids equal to 0x201. Such a call will run thread in a new task. New task is regarged as child task of current task. 

Execution stack is an object too, it consists of 4 separate stacks. Top values of execution stack mean top values of each of 4 stacks. When function is called, top values (their quantities correspond to input type of this function) are extracted from execution stack of previous function and pushed to empty execution stack of new function. Initial state of execution stack of the function is its input object. Then, bytecodes are executed step by step from position 0. Each bytecode replaces predefined number of values from the top of execution stack by another values. Execution stack of function is transformed step by step until bytecode `ret` is read. This bytecode extracts top values (their quantities correspond to output type of function) from execution stack and creates output object of the function. Execution process is returned to the point where the function was called from, and values from the output object of function are pushed to execution stack of previous function.

### Bytecodes

There are 256 bytecodes. Bytecodes from 0 to 127 just put corresponding numbers to integer stack. The following table consists all implemented bytecodes from 128 to 255 in hexadecimal representation

\ | 0/8 | 1/9 | 2/A | 3/B | 4/C | 5/D | 6/E | 7/F
---|---|---|---|---|---|---|---|---
80|ill|nop|code|-|info|new|hash|fix
88|ret|-|call|-|-|-|-|-
90|lb|jb|jzb|jnzb|lf|jf|jzf|jnzf
98|eq|lt|gt|neg|feq|flt|fgt|fneg
A0|lload|iload|fload|cload|lstore|istore|fstore|cstore
A8|lpops|ipops|fpops|cpops|lpopn|ipopn|fpopn|cpopn
B0|lget|iget|fget|cget|lset|iset|fset|cset
B8|lpush|ipush|fpush|cpush|lpop|ipop|fpop|cpop
C0|add|sub|mul|div|fadd|fsub|fmul|fdiv
C8|shl|shr|-|-|and|or|xor|not
D0|itof|itoc|ftoi|ctoi|fsplit|fmerge|ffloor|fceil
D8|-|-|fsqrt|-|-|fsincos|-|-
E0|input|output|read|write|bind|connect|getsys|setsys
E8|mload|msave|mstat|-|mrecv|msend|mpopen|-
F0|inputp|outputp|readp|writep|bindp|connectp|getsysp|setsysp
F8|mloadp|msavep|mstatp|-|mrecvp|msendp|mpopenp|-

Prefix `l`, `i`, `f`, `c` mean that bytecode deal with specific of 4 stacks: links, integers, floats or characters.

#### Unilities

`ill` - illegal bytecode, `nop` - empty operation, `code` pushes current function into links stack, `info` pops object from links stack, pushes its type to links stack and pushes 8 numbers to integers stack: 4 sizes of basic types in bytes and 4 sizes of stacks for this object, `new` is reverse operation: pops type from links stack, 8 sizes from integers stack, construct new object for given parameters and pushes it into links stack, `hash` pops object from link stack and pushes full hash of the object into integers stack as 4 integers, `fix` pop object from link stack and make it constant (unchangeable).

#### Functions

`ret` pops output values from 4 stacks, finishes the function, returns to previous execution stack and pushes output values to corresponding stacks, `call` pops function from links stack, pops its input values from 4 stacks, creates new empty execution stack, pushes input values into it and starts execution of new function from zero position.

#### Branches

`lb/lf` is label for back/forward branching, `jb/jf` goes to corresponding label, `jzb/jzf/jnzb/jnzf` pops integer from integers stack and goes to corresponding label is this number is zero (z) or is not zero (nz). Labels and branch instructions form a correct parenthesis structure, so corresponding label is unequivocally determined for given branch instruction. For example, in the following function.
```
... lb ... jzf ... lf ... lb ... jnzb ... jb ...
```
`jnzb` bytecode corresponds to second label `lb` and `jb` bytecode corresponds to the first label `lb`. If labels or branch instructions does not form a correct parenthesis structure, then the checker fails on verification of this function, and the function is not executed.

#### Stack manipulations

`*load` pops number N from integer stack and pushes value from N-th position in correspongind stack (position of top is 0) to the same stack, `*store` pops N from integer stack, pops value from corresponding stack and writes the value to the position N of the same stack, `*pops` pops single value from corresponding stack, `*popn` pops N from integers stack and then pops N values from corresponding stack.

#### Object manipulations

`*get` pops N from integers stack, pops object from links stack and pushes N-th object (position 0 here is bottom of stack) from corresponding stack of the object into the execution stack, `*set` pops N from integers stack, pops object from links stack, pops value from corresponding stack and writes this value into N-th position of  corresponding stack of the object, `*push` pops object from links stack, then pops value from corresponding stack and pushes it into the same stack of the obejct, `*pop` pops object from links stack, pops number N from integers stack and pops N values from corresponding stack of the object.

#### Integer math

`eq/lt/gt/add/sub/and/or/xor/shl/shr` pops 2 integers from stack, computes the operation and pushes result to integers stack, result of comparison is 0 or 1, bit shifts are unsigned, `mul/div` pops 2 integers and pushes 2 integers, for `mul` the are low and high parts of the product where numbers regarded as unsigned, for `div` they are remainder and quotient where numbers are regarded as signed, `neg/not` pops one integer, computes the operation and pushes the result. Note that division by 0 does not produce an error. In this case remainder is equal to dividend, and quotient is zero.

#### Float math

`feq/flt/fgt` pops two floats and pushes result of comparison into integers stack, `fneg/ffloor/fceil/fsqrt` pops one float, computes the operation and pushes one float result, `fadd/fsub/fmul/fdiv` pops 2 float, computes the operation and pushes float result, `fsincos` pops 1 float and pushes 2 floats: its sin and cos, `fmerge` pop 1 float and pushes 2 integers: mantissa and exponent (in radix 2), `fsplit` is reverse operation: pops 2 integers: mantissa and exponent, forms the float number and pops it, `itoc/itof/ctoi/ctof` pops 1 value from `c/i/f` stack, casts it to another basic type and pushes to corresponding stack. Some of float bytecodes are still unimplemented, for example, `fexp`, `flog` and `fatan`.

#### I/O bytecodes

Difference between bytecodes with and without suffix `p` was described earlier. First parameter of each I/O bytecode (which popped from the stack last) is link to a function which will be called in another thread after I/O operation completion, the second parameter is arbitrary object that passed as first parameter to that function. Below we define only version of bytecodes without `p` suffix. There input parameters (or inputs) mean input parameters with exception for first two links, and output parameters (outputs) mean input parameters of a function which is called after operation completion, with exception for the first link. Note that I/O bytecode pops his parameters from execution stack, but pushes back nothing. 

Array of characters (array of bytes) means an object in which only characters stack is not empty, ints array is the same concept for integers. Each thread has its input and output streams. By default they are standard input and output of dpvm machine, but after some I/O operations input and output streams redirected for the thread which is started after I/O operation completion and all threads produced by that thread. Redirections are described below in definitions of corresponding bytecodes.

`input` inputs characters from input stream, input parameters: integer timeout in nanoseconds, output parameters: array of inputted characters, integer status (number of characters or -1 for error); `output` outputs characters to output stream, input parameters: array of outputtted characters, output parameters: integer status (number of outputtted characters or -1 for error); `read` reads characters from a file, inputs: file name (array of characters), start position in file, numbner of bytes to read, outputs: array of characters, status; `write` writes characters to a file, inputs: file name, array of bytes to write, start position to write or -1 to write at the end of file, outputs: integer status;

`bind` listens for incoming connections to specified local port, function will be called for each connection in separate thread, inputs: integer which compines port to listen in lower 16 bits, and protocol in the next 16 bits (as for now, only protocol 0x406 is allowed which means TCP over IPv4), ip address in 2 next integers (IPv4 address is in lower 32 bits of first integer), outputs: port and 2 integers of IP address of remote host which connected to local host; `connect` connects to remote host, inputs: host name to connect, port and protocol as single integer, outputs are the same as for `bind` bytecode. `bind` and `connect` bytecodes redirect input and output of created thread and all its succestor threads to the established connection.

`getsys` reads system parameter, inputs: array of ints (parameters ids), outputs: array of ints (parameners values); `setsys` writes system parameters, inputs: array of parameter ids, array of parameter values, outputs: array of final parameters values. System parameter id consists of 3 parts: bit 16 and above is task id for which parameter is concerned to or 0 if it concerned to global scope, bits from 8 to 15 is group of parameters, bits from 0 to 7 is number of parameter. Here is description of implemented parameters. In each group parameters are listed with step 1 starting from parameter 0. Note: parameters can be accessed only for current task or any child task in several generations.
* Group 0, general parameters: current time (in nanoseconds since January 1, 1970), number of objects, number of thread, number of active threads, number of physical threads occupied by dpvm machine, number of local mailbox addresses which contain messages, number of messages in queues. Note: set time means wait until this time. 
* Group 1, error handling: error code (see below), bytecode which triggers error, position in function in which error occured, short hash of function in which error is occured. Note: if on of parameters read in `getsys` is from group 1, then this call is regarded as error handler for current task. Function will be called each time when thread is crashed with error.
* Group 2, task parameters: task flags (see below), task id, task id for first child task, task id for next sibling task, total number of child tasks in any generations. Note: if `setsys` is called for the parameter `task id`, then this mean creation of new child task, other parameters in this call interpreted as parameters of the new task.
* Group 3, task name. There are 32 integer parameters in this group. They are intended for storing 128 bytes (8 bytes in each integer) which describe this task in human readable format.

Error codes. For group of errors, `*` corresponds to LINKS, INTS, FLOATS and CODES for 4 error codes in the group respectively. 
Code | Mnemonic | Meaning
--- | --- | ---
0 | SUCCESS | no error
1 | FINISHED | thread is finished
2 | TERMINATED | thread was manually terminated
3 | END_OF_CODE | end of function reached without ret bytecode
4 | NOT_IMPLEMENTED | bytecode not implemented
5 | NOT_FINISHED | thread is not finished
6 | NOT_CHECKED | checker failed on this function
7 | NOT_TRANSLATED | translator failed on this function
8 | NO_MEMORY | no memory for object creation
9 | CREATE_OBJECT | other error during object creation
10 | STORE_ERROR | error during working with lmdb database
11 | MAP | error during memory mapping on host system
12 | CONST | attempt to alter const object
13 | OBJECT_PARAMS | when creating new object, desired sizes of basic types unsupported
14 | TYPE_MISMATCH | can't cast type of object to desired type
15 | IO | error during I/O operation
16..19 | `*`_INDEX | illegal index in array during get/set operation with object
20..23 | `*`_OVERFLOW | push of pop failed for object due to limitations in object's type

Task flags:
Bit number | Meaning if this bit = 1
--- | ---
0 | task is finished. Set this flag to 1 to kill the task
1 | task is stopped
4 | task can't modify flags of tasks (self and for childs)
5 | task can't modify names of tasks (Group 3 of parameters)
6 | task can't modify error handlers of tasks
7 | task can't create child tasks
32..63 | task can't execute corresponding I/O bytecode 0xE0..0xFF 

`mload` loads array of objects from lmdb database, inputs: array of integers of the size `4*N`, where each 4 positions contain full hash of desired object, or short hash if only 47 lower bits of hash are not zero, outputs: array of loaded objects where `any` type as object mean that object is not found; `msave` stores array of objects into database, inputs: array of objects to save, outputs: array of copies of these objects which are found is cache or were written to cache; `mstat` reads parameters of given file or several files from given catalogue, inputs: filename of file or catalogue (characters array), start position in the catalogue, maximum number of files to return starting from the given position, outputs: array of objects, each corresponds to one file, each object contain file name inside the catalogue in its characters stack, and the following parameters in integers stack: size of the file, modification time, access time, creation time, file mode, inode number, user id, group id. All time parameters are given in nanoseconds since January 1, 1970. Last 4 parameters are in Unix style.

`mrecv` waits while input mailbox queue on given address is empty or before timeout, and return all incoming messages, inputs: mailbox address (any object), timeout in nanoseconds, outputs: array of all input messages, status (< 0 means error); `msend` sends mailbox message to given address, inputs: address (any object), message (any object), outputs: status. Note: this mailbox system is internal for dpvm machine. `mpopen` runs program in external environment of dpvm machine, inputs: path to program executable (array of characters), parameters of executable - array of arrays of chars, where each characters array of the form name=value means environment variable for the program, and each characters array not of this form means command line option for the program, mode, outputs: status. Note: only modes 1, 3, 5 are supported. Mode 1 mean that one thread will be started and output of program will be redirected to input of this thread; 3 mean that 2 threads will be started, input of first will be redirected to output of program, and output of second will be redirected to input of the program; 5 mean that one thread will be started, and both input and output of the thread will be redirected to corresponding output and input of the program. In status bit 0 raised if input is redirected, bit 1 raised if output is redirected.

### Constant and volatile objects

Each object at each moment is in one of two states: constant i.e. nonchangeable, or volatile i.e. changeable. When object is created by `new` bytecode, it is in the volatile state. Constant object can't switch to volatile state. Volatile object can switch to constant state only in following cases: 
* Bytecode `fix` is called for this object;
* Object is become a type of new created object;
* Object is a function and this function is called for execution;
* Object is passed as parameter to one of I/O bytecodes with the following exception: parameter number 2 (arbitrary data which is passed to function called after operation completion) have not transit to constant state in the following cases:
  * if bytecode have no `p` suffix and not equal to `bind`, `getsys` or `mpopen`;
  * if bytecode is `getsys` and no one of parameter ids belongs to group 1;
  * if bytecode is `mpopen` and mode is 1 or 5.

There are 3 key words connected to constant or volatile objects in the C-like language used to compile into dpvm bytecode. They are the following: const, volatile and stateful. These key words may be applied to input, output parameters of function or to fields of a structure. Const mean that object should not be altered. Actually object may be const or volatile. Volatile mean that object must be volatile. Stateful mean that state of object should not be altered (i.e. transited from volatile to const state). Note that dpvm machine does not deal with these keywords. They are used by compiler, can be encoded inside a type of a function, and it is planned that these keywords will be analysed by checker in the future. However, if dpvm machine detects attempt to alter const object, a thread is crashed with error. 
