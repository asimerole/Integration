cmake_minimum_required(VERSION 3.21)
project(Recon-Integration)

# Установка политики CMP0167 для использования BoostConfig.cmake
if(POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

# Установка политики CMP0144 для использования переменной BOOST_ROOT
if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

set(CMAKE_CXX_STANDARD 17)

# Установка конфигурации для сборки Release по умолчанию
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Пути к библиотекам (замените на ваш путь к библиотекам ODBC)
if (MSVC)
    set(ODBC_LIB_PATH "C:/Program Files (x86)/Windows Kits/10/Lib/10.0.22621.0/um/x64")
    link_directories(${ODBC_LIB_PATH})
    add_compile_options("/wd4819")
endif()

# Добавляем определение макроса UNICODE, если необходимо
add_definitions(-DUNICODE)


# Указываем путь к установленной Boost
set(BOOST_ROOT "C:/DEV/boost_1_85_0")
set(BOOST_LIBRARYDIR "C:/DEV/boost_1_85_0/stage/lib")
set(Boost_NO_SYSTEM_PATHS ON)
set(Boost_DIR "C:/DEV/boost_1_85_0/stage/lib/cmake/Boost-1.85.0")

# Ищем необходимые компоненты Boost (файлы и библиотеки)
set(Boost_USE_STATIC_LIBS ON)  # Использовать статические библиотеки Boost
find_package(Boost REQUIRED COMPONENTS filesystem system)
include_directories(${Boost_INCLUDE_DIRS})

# Установка путей для iconv
set(ICONV_INCLUDE_DIR "C:/DEV/libiconv-for-Windows/build/include")
set(ICONV_LIB_DIR "C:/DEV/libiconv-for-Windows/build/lib")

include_directories(${ICONV_INCLUDE_DIR})
link_directories(${ICONV_LIB_DIR})

# Источники проекта
file(GLOB SOURCES "src/*.cpp")
file(GLOB HEADERS "include/*.h")

add_executable(${PROJECT_NAME} main.cpp)

# Линковка библиотек Boost
target_link_libraries(${PROJECT_NAME} PRIVATE Boost::filesystem Boost::system)

# Линковка библиотеки odbc32
if (MSVC)
    target_link_libraries(${PROJECT_NAME} PRIVATE odbc32.lib ${ICONV_LIB_DIR}/libiconv.dll.a)
endif()

# Настройки компиляции для режима Release
set_target_properties(${PROJECT_NAME} PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib
)
