cmake_minimum_required (VERSION 3.11)

# Define an executable target
project(texture_conversion)
add_executable(texture_conversion)
target_compile_definitions(texture_conversion
	PUBLIC _CRT_SECURE_NO_WARNINGS)

# Specify the required C standard
set_target_properties(texture_conversion PROPERTIES C_STANDARD 99)
set_target_properties(texture_conversion PROPERTIES CMAKE_C_STANDARD_REQUIRED True)

# Add source code
target_sources(texture_conversion PRIVATE
	main.c
	stb_dxt.h
	stb_image.h
)

if (UNIX)
# Link math.h
target_link_libraries(texture_conversion PRIVATE m)
endif (UNIX)
