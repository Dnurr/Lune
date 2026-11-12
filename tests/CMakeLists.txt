# Automated tests (Qt Test). Each tst_*.cpp is one executable and one CTest test.
# Run them all with test.bat, or: ctest --test-dir build --output-on-failure

find_package(Qt6 REQUIRED COMPONENTS Test)

# Qt DLLs aren't deployed next to the test executables: put Qt's bin folder on PATH.
set(LUNE_QT_BIN "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_BINS}")

function(lune_add_test name)
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE lune_editor_core Qt6::Test)
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES
        ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:${LUNE_QT_BIN}"
        TIMEOUT 60)
endfunction()

lune_add_test(tst_scene)
lune_add_test(tst_commands)
lune_add_test(tst_sceneio)
lune_add_test(tst_meshimport)
lune_add_test(tst_picking)
lune_add_test(tst_gizmo)
