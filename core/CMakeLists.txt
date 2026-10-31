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
)

target_include_directories(arranger_core PUBLIC include)
