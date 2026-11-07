cmake_minimum_required(VERSION 3.16)
project(morph VERSION 0.2.4 LANGUAGES CXX)

option(MORPH_BUILD_TESTS "Build unit tests" ON)

if(UNIX AND NOT APPLE AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "/usr" CACHE PATH "..." FORCE)
endif()

# Номер сборки из git. Раньше configure дописывал его в build_number.txt,
# и после любой сборки рабочее дерево было грязным. Файл остаётся для
# сборки из архива без .git
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
    execute_process(
        COMMAND ${GIT_EXECUTABLE} rev-list --count HEAD
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        OUTPUT_VARIABLE BUILD_NUMBER
        OUTPUT_STRIP_TRAILING_WHITESPACE
        ERROR_QUIET
    )
endif()
if(NOT BUILD_NUMBER AND EXISTS "${CMAKE_SOURCE_DIR}/build_number.txt")
    file(READ "${CMAKE_SOURCE_DIR}/build_number.txt" BUILD_NUMBER)
    string(STRIP "${BUILD_NUMBER}" BUILD_NUMBER)
endif()
if(NOT BUILD_NUMBER)
    set(BUILD_NUMBER "1")
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# на маке MPRIS не нужен, а QtDBus в brew-сборке Qt может и не оказаться
if(APPLE)
    enable_language(OBJCXX)
    find_package(Qt6 COMPONENTS Core Quick Qml Network Multimedia Widgets Svg Core5Compat Sql Concurrent REQUIRED)
else()
    find_package(Qt6 COMPONENTS Core Quick Qml Network Multimedia DBus Widgets Svg Core5Compat Sql Concurrent REQUIRED)
endif()
find_package(OpenSSL REQUIRED)

# Всё, кроме main, живёт в статической библиотеке: её же линкуют тесты
add_library(morph_core STATIC
    src/core/Application.h src/core/Application.cpp
    src/core/MorphCore.h src/core/MorphCore.cpp
    src/utils/PathProvider.h src/utils/PathProvider.cpp
    src/utils/QmlHighlighter.h src/utils/QmlHighlighter.cpp
    src/utils/CacheManager.h src/utils/CacheManager.cpp
    src/utils/BackgroundWriter.h src/utils/BackgroundWriter.cpp
    src/utils/FileWatcher.h src/utils/FileWatcher.cpp
    src/utils/CryptoUtils.h src/utils/CryptoUtils.cpp
    src/network/NetworkManager.h src/network/NetworkManager.cpp
    src/services/BaseService.h
    src/services/YandexService.h src/services/YandexService.cpp
    src/services/SoundCloudService.h src/services/SoundCloudService.cpp
    src/services/YouTubeService.h src/services/YouTubeService.cpp
    src/services/LyricsService.h src/services/LyricsService.cpp
    src/services/PinterestCovers.h src/services/PinterestCovers.cpp
    src/audio/AudioEngine.h src/audio/AudioEngine.cpp
    src/core/ServiceManager.h src/core/ServiceManager.cpp
    src/core/ListenTracker.h src/core/ListenTracker.cpp
    src/core/WaveMixer.h src/core/WaveMixer.cpp
    src/core/CandidateFilter.h src/core/CandidateFilter.cpp
    src/core/TrackMatch.h src/core/TrackMatch.cpp
    src/core/FallbackFinder.h src/core/FallbackFinder.cpp
    src/core/PlaybackQueue.h src/core/PlaybackQueue.cpp
    src/core/Library.h src/core/Library.cpp
    src/core/ListeningHistory.h src/core/ListeningHistory.cpp
    src/core/TasteProfile.h src/core/TasteProfile.cpp
    src/core/Ranker.h src/core/Ranker.cpp
    src/core/Recommender.h src/core/Recommender.cpp
    src/core/MorphMixes.h src/core/MorphMixes.cpp
    src/core/PlaylistSync.h src/core/PlaylistSync.cpp
    src/settings/SettingsManager.h src/settings/SettingsManager.cpp
    src/settings/PlaylistManager.h src/settings/PlaylistManager.cpp
    src/settings/TokenManager.h src/settings/TokenManager.cpp
    src/settings/HistoryManager.h src/settings/HistoryManager.cpp
    src/settings/StyleManager.h src/settings/StyleManager.cpp
    src/core/MprisManager.h src/core/MprisManager.cpp
    src/utils/DiscordManager.h src/utils/DiscordManager.cpp
    src/services/LastFmApi.h src/services/LastFmApi.cpp
    src/core/KnowledgeBase.h src/core/KnowledgeBase.cpp
    src/core/MusicSpace.h src/core/MusicSpace.cpp
    src/core/TasteVectors.h src/core/TasteVectors.cpp
    src/core/TasteRadio.h src/core/TasteRadio.cpp
    src/core/AudioFeatures.h src/core/AudioFeatures.cpp
    src/core/SoundLibrary.h src/core/SoundLibrary.cpp
    src/utils/StallWatch.h src/utils/StallWatch.cpp
    src/server/HttpServer.h src/server/HttpServer.cpp
    src/server/SubsonicBackend.h
    src/server/SubsonicCatalog.h src/server/SubsonicCatalog.cpp
    src/server/SubsonicApi.h src/server/SubsonicApi.cpp
    src/server/ServerAudio.h src/server/ServerAudio.cpp
    src/server/PhoneMixes.h src/server/PhoneMixes.cpp
    src/server/MorphBackend.h src/server/MorphBackend.cpp
    src/server/PhoneServer.h src/server/PhoneServer.cpp
)

# Разбор звука это циклы по миллиону отсчётов: без оптимизации он в сорок раз
# медленнее и в отладочной сборке грел бы мак в фоне. Этот файл всегда с -O2
set_source_files_properties(src/core/AudioFeatures.cpp PROPERTIES
    COMPILE_OPTIONS "$<$<CXX_COMPILER_ID:GNU,Clang,AppleClang>:-O2>")

if(APPLE)
    target_sources(morph_core PRIVATE src/core/MacNowPlaying.h src/core/MacNowPlaying.mm)
    set_source_files_properties(src/core/MacNowPlaying.mm PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
    target_link_libraries(morph_core PUBLIC "-framework AppKit" "-framework Foundation" "-framework MediaPlayer")
else()
    target_sources(morph_core PRIVATE
        src/core/MprisRootAdaptor.h src/core/MprisPlayerAdaptor.h src/core/MprisPlayerAdaptor.cpp
    )
    target_link_libraries(morph_core PUBLIC Qt6::DBus)
endif()

target_include_directories(morph_core PUBLIC src)
target_compile_definitions(morph_core PUBLIC
    MORPH_BUILD_NUMBER="${BUILD_NUMBER}"
    MORPH_VERSION="${PROJECT_VERSION}"
)

target_link_libraries(morph_core PUBLIC
    Qt6::Core
    Qt6::Sql
    Qt6::Concurrent
    Qt6::Quick
    Qt6::Qml
    Qt6::Network
    Qt6::Multimedia
    Qt6::Widgets
    Qt6::Svg
    Qt6::Core5Compat
    OpenSSL::Crypto
)

if(WIN32)
    target_link_libraries(morph_core PUBLIC dwmapi)
endif()

add_executable(morph
    resources.qrc
    src/main.cpp
)
target_link_libraries(morph PRIVATE morph_core)

if(WIN32)
    set_target_properties(morph PROPERTIES WIN32_EXECUTABLE TRUE)
    target_sources(morph PRIVATE morph.rc)
endif()

if(APPLE)
    target_sources(morph PRIVATE macos/morph.icns)
    set_source_files_properties(macos/morph.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(morph PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_SOURCE_DIR}/macos/Info.plist.in"
        MACOSX_BUNDLE_BUNDLE_NAME "morph"
        MACOSX_BUNDLE_ICON_FILE "morph.icns"
        MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.don-qu1xote.morph"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${BUILD_NUMBER}"
    )
endif()

if(MORPH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(Packaging.cmake)
