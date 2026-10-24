find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Test)

if(Qt6_FOUND)
    find_package(Qt6 REQUIRED COMPONENTS Test)
    set(QT_TEST Qt6::Test)
else()
    find_package(Qt5 REQUIRED COMPONENTS Test)
    set(QT_TEST Qt5::Test)
endif()

add_executable(vdj-ui-test test_main.cpp)
target_link_libraries(vdj-ui-test PRIVATE ${QT_TEST})
add_test(NAME vdj-ui-test COMMAND vdj-ui-test)

add_executable(vdj-ui-wrapper-test test_wrappers.cpp)
target_link_libraries(vdj-ui-wrapper-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-wrapper-test COMMAND vdj-ui-wrapper-test)

add_executable(vdj-ui-beatgrid-test test_beat_grid.cpp)
target_link_libraries(vdj-ui-beatgrid-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-beatgrid-test COMMAND vdj-ui-beatgrid-test)

add_executable(vdj-ui-controls-test test_controls.cpp)
target_link_libraries(vdj-ui-controls-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-controls-test COMMAND vdj-ui-controls-test)

add_executable(vdj-ui-config-test test_config.cpp)
target_link_libraries(vdj-ui-config-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-config-test COMMAND vdj-ui-config-test)

add_executable(vdj-ui-tracks-test test_tracks.cpp)
target_link_libraries(vdj-ui-tracks-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-tracks-test COMMAND vdj-ui-tracks-test)

add_executable(vdj-ui-integration-test test_integration.cpp)
target_link_libraries(vdj-ui-integration-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-integration-test COMMAND vdj-ui-integration-test)
set_tests_properties(vdj-ui-integration-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-nfsmount-test test_nfs_mount.cpp)
target_link_libraries(vdj-ui-nfsmount-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-nfsmount-test COMMAND vdj-ui-nfsmount-test)
set_tests_properties(vdj-ui-nfsmount-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# Real rekordbox export used by vdj-ui-rekordbox-test. Override at configure time with
#   cmake -DVDJ_TEST_REKORDBOX_DIR=/path ..   or at run time with the env var of the same name.
set(VDJ_TEST_REKORDBOX_DIR "/var/tmp/rekordbox-example" CACHE PATH
    "Folder with Contents/ and PIONEER/rekordbox/export.pdb for rekordbox tests")
add_executable(vdj-ui-rekordbox-test test_rekordbox.cpp)
target_compile_definitions(vdj-ui-rekordbox-test PRIVATE VDJ_TEST_REKORDBOX_DIR="${VDJ_TEST_REKORDBOX_DIR}")
target_link_libraries(vdj-ui-rekordbox-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-rekordbox-test COMMAND vdj-ui-rekordbox-test)
set_tests_properties(vdj-ui-rekordbox-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-audio-test test_audio.cpp)
target_link_libraries(vdj-ui-audio-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-audio-test COMMAND vdj-ui-audio-test)
set_tests_properties(vdj-ui-audio-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-difflock-test test_diff_lock.cpp)
target_link_libraries(vdj-ui-difflock-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-difflock-test COMMAND vdj-ui-difflock-test)
set_tests_properties(vdj-ui-difflock-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
