# Тесты, не зависящие от сети (EpisodeParser, …). Запуск: `ctest` из корня
# сборки (или ./tests/test_episode_parser напрямую).

set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Test)

add_executable(test_episode_parser
    test_episode_parser.cpp
    ../src/core/EpisodeParser.cpp
)
target_include_directories(test_episode_parser PRIVATE ../src/core)
target_link_libraries(test_episode_parser PRIVATE Qt6::Core Qt6::Test)

add_test(NAME episode_parser COMMAND test_episode_parser)
