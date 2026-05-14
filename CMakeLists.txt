cmake_minimum_required(VERSION 3.20)
project(reference_app LANGUAGES CXX)

add_executable(reference-app WIN32
    src/main.cpp
    src/app.rc
)

target_compile_features(reference-app PRIVATE cxx_std_17)
target_compile_definitions(reference-app PRIVATE UNICODE _UNICODE)
target_link_libraries(reference-app PRIVATE gdiplus comdlg32 user32 gdi32 ole32)

if (MSVC)
    set_property(TARGET reference-app PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()
