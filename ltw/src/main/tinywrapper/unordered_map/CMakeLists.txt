set(CMAKE_CXX_FLAGS  "${CMAKE_CXX_FLAGS} -fPIC")
set(CMAKE_C_FLAGS  "${CMAKE_C_FLAGS} -fPIC -O3 -DNDEBUG")

add_library(unordered_map unordered_map.c int_hash.c)
