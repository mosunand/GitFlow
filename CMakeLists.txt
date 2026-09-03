cmake_minimum_required(VERSION 3.21)
project(GitFlowPro VERSION 1.0.0 LANGUAGES CXX)
# Windows 资源脚本（app.rc 内嵌应用图标）需要 RC 语言（MinGW 用 windres）
enable_language(RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

list(APPEND CMAKE_PREFIX_PATH "D:/Users/qt/6.8.3/mingw_64")

# 所有构建产物严格限制在项目内 build/ 目录
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/build/lib)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network Concurrent)

set(SOURCES
    src/main.cpp
    src/paths.h
    src/i18n.h
    src/i18n.cpp
    src/theme.h
    src/theme.cpp
    src/settings.h
    src/settings.cpp
    src/crypto.h
    src/crypto.cpp
    src/proxy.h
    src/proxy.cpp
    src/models.h
    src/services/gitservice.h
    src/services/gitservice.cpp
    src/services/githubservice.h
    src/services/githubservice.cpp
    src/services/giteeservice.h
    src/services/giteeservice.cpp
    src/services/accountservice.h
    src/services/accountservice.cpp
    src/ui/icons.h
    src/ui/icons.cpp
    src/ui/titlebar.h
    src/ui/titlebar.cpp
    src/ui/highlighter.h
    src/ui/highlighter.cpp
    src/ui/codeeditor.h
    src/ui/codeeditor.cpp
    src/ui/progressdialog.h
    src/ui/progressdialog.cpp
    src/ui/aboutdialog.h
    src/ui/aboutdialog.cpp
    src/ui/settingsdialog.h
    src/ui/settingsdialog.cpp
    src/ui/addaccountdialog.h
    src/ui/addaccountdialog.cpp
    src/ui/repopaneldialog.h
    src/ui/repopaneldialog.cpp
    src/ui/manualdialog.h
    src/ui/manualdialog.cpp
    src/ui/releasedialog.h
    src/ui/releasedialog.cpp
    src/ui/terminalpanel.h
    src/ui/terminalpanel.cpp
    src/ui/mainwindow.h
    src/ui/mainwindow.cpp
)

qt_add_resources(SOURCES resources/res.qrc)

add_executable(GitFlowPro WIN32 ${SOURCES} ${CMAKE_SOURCE_DIR}/app.rc resources/res.qrc)

target_include_directories(GitFlowPro PRIVATE src)
target_link_libraries(GitFlowPro PRIVATE
    Qt6::Widgets Qt6::Gui Qt6::Core Qt6::Network Qt6::Concurrent
    bcrypt
)

# Windows: deploy Qt DLLs next to exe after build
if(WIN32)
    add_custom_command(TARGET GitFlowPro POST_BUILD
        COMMAND "D:/Users/qt/6.8.3/mingw_64/bin/windeployqt" --no-system-d3d-compiler --no-opengl-sw $<TARGET_FILE:GitFlowPro>
        COMMENT "Deploying Qt runtime"
    )
endif()
