add_executable(arranger_tests
    TestMain.cpp
    ChordTests.cpp
    TransposerTests.cpp
    YamahaReaderTests.cpp
    EngineTests.cpp
)
target_link_libraries(arranger_tests PRIVATE arranger_core)
add_test(NAME arranger_tests COMMAND arranger_tests)
