find_package(Qt6 REQUIRED COMPONENTS Test)

# Каждый тест отдельный бинарник: упавший не тянет за собой остальные,
# а ctest показывает, какой именно
function(morph_add_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE morph_core Qt6::Test)
    target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()

morph_add_test(tst_playlist_manager tst_playlist_manager.cpp)
morph_add_test(tst_service_manager tst_service_manager.cpp support/FakeHttpServer.h support/FakeTools.h)
morph_add_test(tst_listen_tracker tst_listen_tracker.cpp)
morph_add_test(tst_yandex_service tst_yandex_service.cpp support/FakeHttpServer.h)
morph_add_test(tst_fallback tst_fallback.cpp support/FakeHttpServer.h support/FakeTools.h)
