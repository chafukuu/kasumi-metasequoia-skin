cmake_minimum_required(VERSION 3.20)
project(KasumiTypedStarsTests LANGUAGES CXX)

if(NOT WIN32)
  message(FATAL_ERROR "These standalone tests require Windows and the Windows SDK.")
endif()

enable_testing()
find_package(Threads REQUIRED)
get_filename_component(KASUMI_ROOT "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

foreach(test_name IN ITEMS TypingStarEvents TypedStarsSmoke AnimationSchedulerSmoke)
  if(test_name STREQUAL "TypingStarEvents")
    set(test_source typing_star_events_test.cpp)
  else()
    set(test_source "${test_name}.cpp")
  endif()
  add_executable(${test_name} "${test_source}")
  target_compile_features(${test_name} PRIVATE cxx_std_17)
  target_include_directories(${test_name} PRIVATE "${KASUMI_ROOT}/native-source/server/src")
  target_compile_definitions(${test_name} PRIVATE NOMINMAX)
  target_link_libraries(${test_name} PRIVATE Threads::Threads)
  if(MSVC)
    target_compile_options(${test_name} PRIVATE /EHsc /W4 /utf-8)
  endif()
  add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

target_link_libraries(AnimationSchedulerSmoke PRIVATE user32)

find_package(Python3 3.9 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME TypingStarHooks
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_typing_star_hooks.py")
  add_test(NAME TypingStarDedup
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_typing_star_dedup.py")
  execute_process(COMMAND "${Python3_EXECUTABLE}" -c "import PIL"
    RESULT_VARIABLE pillow_status OUTPUT_QUIET ERROR_QUIET)
  if(pillow_status EQUAL 0)
    add_test(NAME BlueStarPixels
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_LIST_DIR}/verify_blue_star.py"
        "${KASUMI_ROOT}/skin/kasumi-adaptive")
  else()
    message(STATUS "Pillow unavailable: BlueStarPixels is not registered.")
  endif()
else()
  message(STATUS "Python 3.9+ unavailable: Python source checks are not registered.")
endif()
