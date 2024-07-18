# dpvm: src cmake; T15.398-T20.329; $DVS:time$
#
# Add executable called "helloDemo" that is built from the source files
# "demo.cxx" and "demo_b.cxx". The extensions are automatically found.
add_executable (dpvm 
	bytecode.c
	cache.c
	config.c
	hash.c
	init.c
	io.c
	launch.c
	mailbox.c
	main.c
	malloc.c
	name.c
	object.c
	queue.c
	run.c
	store.c
	task.c
	thread.c
	transaction.c
)

# Link the executable to the Hello library. Since the Hello library has
# public include directories we will use those link directories when building
# helloDemo
find_package (Threads)
target_link_libraries (dpvm LINK_PUBLIC blake2b m ${CMAKE_THREAD_LIBS_INIT})
target_compile_options (dpvm PRIVATE -g)
