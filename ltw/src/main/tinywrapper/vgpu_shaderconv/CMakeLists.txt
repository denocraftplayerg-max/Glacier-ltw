add_library(vgpu_shaderconv shaderconv.c)
target_compile_options(vgpu_shaderconv PRIVATE -O3 -DNDEBUG)
