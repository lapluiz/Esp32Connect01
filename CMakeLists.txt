# Define a versão mínima do CMake exigida pelo ESP-IDF
cmake_minimum_required(VERSION 3.16)

# Inclui as ferramentas padrão de build do ESP-IDF
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Nome do seu projeto (pode alterar para o que preferir)
project(esp32c3_oled_nativo)