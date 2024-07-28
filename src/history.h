/* dpvm: history and version */
#ifndef DPVM_HISTORY
#define DPVM_HISTORY

#define DPVM_VERSION	"dpvm T15.395-T20.357" /* $DVS:time$

*** history ***

T20.357 - removed blake2 library, replaced by internal implementation of compress function

T20.329 - removed linenoise library and special input handling for stdin

T20.174 - added support for long object names in profile

T20.158 - replace lmdb storage by dpvmb, part 2: store works, mmap db file

T20.152 - replace lmdb storage by dpvmb, part 1: load works

T20.119 - fixed bug in counting ready transactions

T20.051 - malloc module self-implemented, allocate objects via special dpvm_malloc/free/calloc/realloc functions

T20.029 - added ALLOCED and MAPPED system parameters to monitor memory allocation
	  made changes to mpoopen[p] bytecode: env parameters should be listed first, then - command line parameters

T19.706 - fixed bug in hash calculation

T19.704 - added system parameters to replace default checker and translator for particular task

T19.668 - config.c rewritten to read short hashes of config objects from file .dpvm_profile.txt

T19.663 - fixed bug in memory quota system

T19.643 - fixed bugs in transactions system

T19.641 - fixed bugs in memory quota system; monitor 0.7.5

T19.637 - added memory quotas for tasks

T19.621 - added recursive setting of flags with operations =, &= and |= for task and all its descendants

T19.616 - separate operations computation of object hash and fixing the object

T19.613 - if any argument of run function has volatile or stateful modifier than transaction is temporary

T19.610 - fixed bugs with temporary transavtions

T19.605 - fixed bugs: 1) task attached to checker and translator threads; 2) with temporary transactions

T19.586 - support for new bytecode in floats translator

T19.575 - full support for UNFIX bytecode in checker, compiler and translator

T19.552 - result of temporary transaction will not be made constant; added changes to checker

T19.547 - added UNFIX bytecode and MEMORY system parameter

T19.539 - mark transactions with volatile arguments as temporary and remove them right after fetching the result

T19.530 - implemented run bytecode with volatile arguments

T19.513 - new translator with floats computation module

T19.487 - checker 0.3.0: count large int index of const object inside function

T19.050 - system parameters moved to common file sysParams.dpvmh

T18.907 - added truncating/deleting file for writes with size = 0;

T18.885 - added creating all parent directories when writing file with pos <= 0

T18.742 - implemented mpopen bytecode for modes 9, 11 and 13 - same as modes 1, 3 and 5 respectively,
          but internal thread launched instead of external system process; this internal thread
          uses the same function and arguments as mpopen read and write threads, input status for it is set to 8

T18.511 - android version works with self keyboard input

T18.510 - android version works with telnet access from PC

T18.508 - added output to buffer mechanism for android version

T18.504 - dpvm for android, first vesrion

T18.397 - implemented fexp, flog, fatan bytecodes;
	- if host system error occurs during mpopen bytecode execution,
	  error code returned in status instead of breaking the program

T18.355 - system registry introduced; monitor 0.7 integrated with registry

T18.336 - added NO_INPUT_ECHO task flag to securely inputting passwords

T18.327 - add NO_HISTORY task flag to avoid adding inputted lines to history

T18.322 - implement line editing with previous content when inputting from terminal;
	  outputted line just before input shoudl have format:
		<prompt><previous content of length N><symbols \b repeated N times>
	  when inputting, previous content will be available by pressing Arrow Up key

T18.252 - enhanced determining of prompt string for terminal input-ouptut
	- added stdin and stdout mutexes

T18.229 - dnet 0.3.6: checked with new checker
	- compiler 0.7.10: added integer constant additive expressions: a + b - c ...
	- fixed crash in io.c
	- fixed compilation issue with const variables in case

T18.051 - p2p 0.3.1: checked with new checker

T18.038 - function short hash and position added to error code in bytecode interpreter;
        - checker 0.2.8, compiler 0.7.9, maker 0.1.7, monitor 0.6.10: fixed checking errors

T18.019 - checker 0.2.7: fixed bug with type of all types
	- compiler 0.7.8, interpreter 0.3.12: checked with new checker

T18.018 - translator 0.5.4: checked with new checker

T18.016 - added algorithms/hash class, implementation of hash table
	- checker 0.2.6: using hash table class for recursive type traversal

T18.012 - checker 0.2.5: added debugging, fixed bug

T18.001 - checker 0.2.4: added more checks for run/wait, lset, i/o bytecodes,
	  checks const/volatile/stateful modifiers for structures fields

T17.994 - bytecodes and error codes constant definitions put to dpvm/common/ *.dpvmh
	  files whose can be used both by dpvm source and dpvm functions

T17.992 - translator 0.5.3: full multiplication (64x64 -> 128 bits) and
          comparisons added to graphs translation algorithm;
	- fixed bug and improved debugging for initialization stage

T17.937 - period of checking and starting new workers shortened from 1s to 1/10s,
	  fixed bugs

T17.934 - restart transaction if previous run was terminated or out of memory;
	- added thread for periodic cleanup of obsolete transactions

T17.915 - compiler 0.7.6: implemented [ ... ] blocks for parallel computations
	  of instructions; only simply calls of functions accepted here now

T17.912 - translator 0.5.2: translation of run/wait bytecodes

T17.907 - added 4 new parameters for I/O bytecode getsys:
		qsize (threads queue size),
		qtime (queue stall time in ms),
		ntransactions (number of stored transactions), 
		nresults (number of transactions with computed results
	- monitor 0.6.7: new parameters added to output of stats command

T17.904 - written transaction module and integrated with threads module

T17.903 - written queue module and integrated with threads module

T17.901 - fixed more bugs in run/wait implementation

T17.900 - fixed bugs in run/wait implementation, written run_wait tests

T17.895 - fixed bug in wait implementation;
	  translator 0.5.1: fixed bug, no translation for run/wait as for now

T17.892 - checker 0.2.3, compiler 0.7.5: fixed bugs

T17.890 - bytecode run/wait implementation, checker 0.2.2 with run/wait support

T17.863 - changed run/wait bytecodes

T17.684 - const/stateful/volatile keywords controlled by checker:
	  checker 0.2, compiler 0.7.3, interpreter 0.3.11, maker 0.1.4,
	  monitor 0.6.6, dnet 0.3.3, www server 0.6.11, news log 0.4.2, p2p 0.2.2
	- optimizations in translator: translator 0.5 

T17.622 - monitor 0.6.3: send messages works only for addresses previously used by recv

T17.583 - translator 0.4.11: fixed bug with memory leak

T17.575 - added naddrs and nmess sys params for io bytecode getsys

T17.542 - fixed bug in io bytecode write with pos < 0

T17.539 - dpvm_thread_prepare_result() function introduced, type matching problem fixed

T17.535 - improved type matching, now hashes of two types compare 

T17.493 - multi mutexes with conditions used in mailbox instead of one mutex and time wait

T17.490 - wait interval for mailbox receive decreased from 1s to 0.1s

T17.468	- fixed error in io.c, bytecode connect

T17.449 - compiler 0.7.2: added operations op=

T17.353 - compiler 0.7.1: added operations && and ||

T17.350 - compiler 0.7: rewritten getlexem(), now lexems encoded using build-in hash,
	  lexems do not compare since now using last 7 bytes

T17.195 - fixed bug in mailbox/mrecv

T17.194 - monitor 0.6: added broadcasting saved objects via p2p

T17.189 - added status parameter to mrecv callback
	- if subsequent call to mrecv with the same address is performed from
	  another task, then it failed and status show a error NOT_CHECKED
	- if timeout == 1 << 63 in mrecv io bytecode, then dependance of mailbox
	  address from particular task is removed after this call
	- dependency between mailbox address and task is also removed after task
	  removal
	- written msave_p2p function which sends objects to p2p if p2p is active
	  or perform msave in contrary

T17.178 - added one more parameter (timeout) to input IO bytecode

T17.156 - error handler now called after io error

T17.061 - fixed separation of args and env variables in mpopen
	- src, monitor 0.5.3: task name length expanded to 32 qwords
	- newslog 0.4.1: added handler, fixed reading from feeds file

T17.048 - maximum number of threads became lower in 4 times
	- www_server 0.6.3: code rewritten, keep-alive mode, flags

T16.987 - fixed build for MacOS

T16.985 - translator 0.4.10: implemented fsplit and fmerge bytecodes

T16.976 - p2p 0.1.3 first working version; 
	- if func.csize == 0 in call of io bytecode, then the code executed inline, 
	  without creation new thread; 
	- implemented fsplit and fmerge bytecodes in bytecode interpretator

T16.947 fixed bug in tasks engine; xdag miner 0.3.2: reconnecting to pool, worker name,
	support for multithread mining and different RandomX flags

T16.939 xdag miner 0.3: added support for RandomX algo (method 2)

T16.922 www_server 0.5.1: supports https://

T16.919 added mpopen with mode 5 which allow to run sslproxy for https://

T16.914 fixes in io system, added poll() to write io bytecode

T16.675 fixes in input-output system

T16.673 compiler 0.6.5: fixed several bugs; only used links copied now to function

T16.642 monitor 0.5.1 rewritten using mrecv/msend, any expression is interpreted in
	separate task; translator 0.4.9: add possibility to break dead cycle if
	task is killed; also add some breakpoints to cycles/waitings in dpvm code

T16.632 mrecv/msend bytecodes implemented

T16.624 monitor 0.5: supports creating and killing tasks, new run/kill commands

T16.621 tasks subsystem implemented

T16.612 small fixes in compiler, number of threads increased in 4 times

T16.609 changes in monitor, compiler, translator, checker, maker: added *p io bytecodes,
	functionality of existing io bytecode changed not to make arguments constant
	and to break thread after an io bytecode performed; functilnality of new *p
	io bytecodes is similar to old functionality of io bytecodes without -p suffix;
	hash bytecode now not make object constant; introduced fix bytecode to make
	object constant

T16.604 maker 0.1 (+compiler 0.6.2, monitor 0.4.6): first implementation of maker;
	now batch build is enabled with substitution of previously builded object
	names; files *.dpvmake amd monitor command 'make' introduced

T16.599 compiler 0.6.1, monitor 0.4.5: implemented #include "filename" instruction,
	several source files can be provided to the compiler

T16.598 translator 0.4.7: added new graph-based translation algorithm for integer
	computations; xdag 0.1.2: increased sha256 speed with new translator

T16.584 interpreter 0.3.5, monitor 0.4.4: new compiler integrated with interpreter;
	xdag 0.1.1: sha256 speed increased in 3 times

T16.569 compiler 0.6 (+monitor 0.4.3, checker 0.1.6): rewritten expression compilation,
	implemented usage of structures members, constants; the language modified:
	usage of array indexes without type specifying allowed for arrays only

T16.544 compiler 0.5.5, monitor 0.4.2: rewritten declarations, added type modifiers,
	complex type, structures, global declarations, declaration initializers, 
	constant expressions, aggregate initializers

T16.522 compiler 0.5.4: top functions rewritten on C-like language instead of asm,
	added if-else and while instructions; monitor 0.4.1: added save command;
	fixed error when creating new object with links of same types

T16.516 xdag 0.1: first version of miner; 
	implemented crypto algorithms sha256, dfs, crc32, base64

T16.506 monitor 0.4, checker 0.1.5, compiler 0.5.3, interpreter 0.3.4, debugger 0.2.1,
	translator 0.4.6: implemented 'connect' io bytecode, written example/wget

T16.503 debugger 0.2.0: first working version with support for all bytecodes;
	monitor 0.3.6: included new debugger; 
	translator 0.4.5: implemented full versions of mul and div bytecodes;
	thread with io bytecode 'bind' not recognised as active, so dpvm program
	terminated now after exiting from all monitor terminals

T16.500 Initial version of debugger 0.1.0, also interpreter 0.3.3, monitor 0.3.5

T16.492 checker 0.1.4, translator 0.4.4: transfer internal data from checker
	to translator, optimize bootstrap time

T16.491 translator 0.4.3: fsincos bytecode implemented, fixed bug in run.c

T16.483 monitor 0.3.4, translator 0.4.2: in the case of fail right error code returned,
	fixed bug in text2hashes()

T16.481 monitor 0.3.3: each console restarted with error message if program failed;
	implemented mechanism to set error hadler vi getsys io bytecode

T16.479 monitor 0.3.2: output of physical threads number in stats command;
	number of physical threads is not fixed now, it may increase if all threads
	in given hash group are busy

T16.470 monitor 0.3: command 'compile' added; interactive mode now is via monitor;
	monitor 0.3.1: fixed bug, print error message if thread fails

T16.467 monitor 0.2.4: command 'object' added

T16.465 monitor 0.2.3: command 'type' added; implemented 'msave' bytecode

T16.459 checker 0.1.3, translator 0.4.1, compiler 0.5.2, interpreter 0.3.2,
	monitor 0.2.2: implemented mload bytecode; in monitor, computing expressions
	which contain names of objects; separate io module from threads

T16.453 translator 0.4: +complex recursive functions; all functions translated

T16.448 translator 0.3.7, checker 0.1.2: translate simple recursive functions

T16.246 translator 0.3.6: adding i/o bytecodes, part 2; newslog 0.3.3: fixed bugs

T16.218 translator 0.3.5: adding i/o bytecodes, part 1

T16.197 translator 0.3.4: added all math bytecodes

T16.191 translator 0.3.3: added fneg,fadd,fsub,fmul,fdiv bytecodes

T16.188 translator 0.3.2: added feq,flt,fgt bytecodes

T16.183 translator 0.3.1: starting floating point translation, added itof, ftoi

T16.182 translator 0.3: added *pop bytecodes, all core bytecodes implemented

T16.175 translator 0.2.11: added *set bytecodes

T16.161 translator 0.2.10: added hash bytecode, fixed bug in push implementation

T16.158 translator 0.2.9: added new bytecode, implement info at full

T16.153 translator 0.2.8: added *push bytecodes, fixed bug in dpvm

T16.139 translator 0.2.7: implement *get, code and info bytecodes; strcmp() translated

T16.123 translator 0.2.6: can call translated function from translated function

T16.117 translator 0.2.5: array of addresses of build-in functions provided to 
	translator, added function "free" (dpvm_free_object) to this array;
	written full translation of "ret" bytecode

T16.106 translator 0.2.4: added more optimizations

T16.103 translator 0.2.3: added more optimizations

T16.101 translator 0.2.2: rewritten without moving stack boundary

T16.082 translator 0.2.1: added optimization

T16.082 translator 0.2: full integer-only version

T16.079 added stack boundaries checking to translator and dpvm

T16.076 translator 0.1: first working version, instructions 0-127 and ret implemented

T16.068 fixed critical bug in dpvm code; www_server 0.4.2: fixed '&' symbol in queries

T16.062 monitor, newslog, www_server: compliance with new checker, version +0.0.1

T16.061 checker 0.1.1: more strict rules for types in call and ret,
	any can't be converted to concrete type;
	compiler 0.5.1, interpreter 0.3.1 - compatibility with new checker

T16.054 newslog 0.3: added watchdog to kill wget if it hungs for 5 minutes

T16.051 checker built in dpvm and call befare call of any function

T16.046 compiler 0.5: type of function depends now only on input and output types;
	introduced arbitrary types in parameter list of function and local declarations;
	arbitrary type can be given by name of its hash; 
	checker called after compilation

T16.043 interpreter 0.3: output chars as string, support for checker

T16.041 checker 0.1: working version, checks stack consistency

T16.013 checker: start of project, check for jumps matching

T16.010 www_server 0.4: support for HTTP Range header in files and blog attachments

T15.967 www_server: support for If-Modified-Since: HTTP header in blog

T15.941 stdlib: fixed bug in function parse_time()

T15.941 www_server: support for If-Modified-Since: HTTP header

T15.922 fix compilation warnings with gcc 7.4.0

T15.598 newslog 0.2: text format, win1251 recode, hide wget user-agent, read history

T15.596 newslog 0.1: news logger; monitor 0.2: lunch multiple start commands

T15.587 interpreter 0.2: supported expressions and float output

T15.584 compiler 0.4: added expressions, float support

T15.571 written monitor program which allows remote connect to daemon and run commands

T15.568 www_server: +log.cgi; I/O threads rewritten, pthread_detach performed inside

T15.565 www_server 0.3: cgi programs work

T15.563 www_server: POST method works in blog

T15.560 mpopen: add arguments and environment variables

T15.558 mpopen I/O bytecode implemented for GET cgi requests

T15.554 www_server 0.2: show directories, %XX in filenames

T15.552 compiler 0.3: added integer numbers, strings, chars, term.*pop(number);

T15.550 www_server version 0.1: HEAD request, 501 reply, Date:, Server:, cookies

T15.547 getsys I/O bytecode; implemented proper access logging for www server

T15.546 read, write and bind I/O bytecodes; first working version of www server

T15.541 output bytecode, 'reverse' example program; fixed memory leaks

T15.539 added threads, I/O bytecodes; implemented output bytecode, hello program

T15.527 implemented interpreter version 0.1 which is used to calculate expressions
	in dpvm interactive program

T15.514 compiler: object name interpreted as term

T15.502 compiler 0.2: added call, if, do-while instructions; obtained fully-functional
	compiler without assembly; written simple math program examples

T15.489 compiler: added instructions: term[term] = term; and term.push(term); 

T15.483 compiler: added declarations of array types

T15.475 compiler: load instruction works, added type conversion to assign instruction

T15.470 compiler: added load instruction: var = term[term]; getlexem() rewritten

T15.467 compiler: added binary operation instruction: var = term oper term;

T15.454 compiler: added simple assignment instruction, fix bug in bytecode.c

T15.445 compiler: added declaration instruction for simple types

T15.442 compiler: added 'return' instruction

T15.440 compiler: added names to input and output parameters of function

T15.439 compiler 0.1: compilation the whole text uting getlexem(), comments skipped,
	body of function constists of instructions: asm { }, use ...;, block: { ... }

T15.435 getlexem function do not require lexems to be seperated by spaces now

T15.432 getlexem function skips comments now

T15.431 added compile command which calls 'compiler' function written in dpvm

T15.429 written compiler0 accepted source in the form: type -> type { asm { ... } }

T15.428 implemented 'call' bytecode, written function that constructs a type

T15.426 added ctrl-c handler

T15.424 info instruction removed because it's not deterministic, some bugs fixed

T15.422 added linonoise library for line editing

T15.420 first self-compiled assembler

T15.418 implemented more bytecodes, first version of assembler written in dpvm

T15.417 second program written ann works (bubble sort of ints), error codes introduced

T15.415 implemented some bytecodes, fixed bug in memory allocation,
	working version of first program words2ints

T15.413 written set of bytecodes and first sufficient program

T15.412 saving and loading objects from database works, correct nrefs for links

T15.410 added 'call' command, RET and ADDI instructions, run first program

T15.409 added 'object' command

T15.407 added 'type' command with full functionality

T15.406 added name module, object name is 3 dictionary words concatenated by _ symbol

T15.404 added interactive command environment and objects cache

T15.398 first working build, added blake2 and lmdb

T15.395	project starts

*/

#endif
