include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(Catch2)

add_executable(chaselev_tests deque_test.cpp ring_test.cpp scheduler_test.cpp)
target_link_libraries(chaselev_tests PRIVATE chaselev::chaselev Catch2::Catch2WithMain)

include(Catch)
catch_discover_tests(chaselev_tests DISCOVERY_MODE PRE_TEST)
