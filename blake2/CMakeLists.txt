# dpvm: blake2 cmake; T15.398-T15.398; $DVS:time$
#
# Create a library called "Hello" which includes the source file "hello.cxx".
# The extension is already found. Any number of sources could be listed here.
add_library (blake2b blake2b.c)

# Make sure the compiler can find include files for our Hello library
# when other libraries or executables link to Hello
target_include_directories (blake2b PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

