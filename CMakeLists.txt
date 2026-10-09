cmake_minimum_required(VERSION 3.16)
project(sintetizador_wav VERSION 1.0.0 LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  # -ffp-contract=off: sem FMA, o resultado é igual bit a bit em qualquer máquina.
  add_compile_options(-Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

add_library(sintcore STATIC src/dsp.c src/partitura.c src/instrumentos.c src/render.c src/wav.c)
target_include_directories(sintcore PUBLIC src)
find_library(MATH_LIBRARY m)
if(MATH_LIBRARY)
  target_link_libraries(sintcore PUBLIC ${MATH_LIBRARY})
endif()

add_executable(sintetizador src/main.c)
target_link_libraries(sintetizador PRIVATE sintcore)

enable_testing()
foreach(nome dsp partitura render)
  add_executable(test_${nome} tests/test_${nome}.c)
  target_link_libraries(test_${nome} PRIVATE sintcore)
  add_test(NAME ${nome} COMMAND test_${nome})
endforeach()
add_test(NAME wav COMMAND sh ${CMAKE_SOURCE_DIR}/tests/wav.sh ${CMAKE_BINARY_DIR})
