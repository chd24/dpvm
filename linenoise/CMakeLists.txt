# dpvm: linenoise cmake; T15.422-T15.422; $DVS:time$
#
# Create a library called "Hello" which includes the source file "hello.cxx".
# The extension is already found. Any number of sources could be listed here.
add_library (linenoise linenoise.c)

# Make sure the compiler can find include files for our Hello library
# when other libraries or executables link to Hello
target_include_directories (linenoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
