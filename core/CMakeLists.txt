add_library(arranger_core STATIC
    src/Chord.cpp
    src/ChordRecognizer.cpp
    src/Transposer.cpp
    src/Style.cpp
    src/YamahaStyleReader.cpp
    src/KorgStyleReader.cpp
    src/Oc31.cpp
    src/StyleLoader.cpp
    src/StyleEngine.cpp
    src/SmfWriter.cpp
    src/Smf.cpp
    src/Pad.cpp
    src/KorgPads.cpp
)

target_include_directories(arranger_core PUBLIC include)
target_link_libraries(arranger_core PRIVATE arranger_options)
