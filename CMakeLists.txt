cmake_minimum_required(VERSION 3.10)
project(kairo_hello)

add_library(kairo_hello SHARED main.cpp)

# Standard Android shared library settings for mcpelauncher mods
set_target_properties(kairo_hello PROPERTIES
    OUTPUT_NAME "kairo_hello"
    PREFIX "lib"
)
