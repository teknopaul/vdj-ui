# All test binaries go to <source>/target-test so they never land in the source tree.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/target-test")

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
target_compile_definitions(vdj-ui-integration-test PRIVATE VDJ_DBSERVER_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dbserver")
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

add_executable(vdj-ui-beat-tempo-test test_beat_tempo.cpp)
target_link_libraries(vdj-ui-beat-tempo-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-beat-tempo-test COMMAND vdj-ui-beat-tempo-test)
set_tests_properties(vdj-ui-beat-tempo-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-midi-map-input-test test_midi_map_input.cpp)
target_link_libraries(vdj-ui-midi-map-input-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-midi-map-input-test COMMAND vdj-ui-midi-map-input-test)
set_tests_properties(vdj-ui-midi-map-input-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# rekordbox mode
set(VDJ_DBSERVER_FIXTURES "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/dbserver")
add_executable(vdj-ui-dbcodec-test test_db_codec.cpp)
target_compile_definitions(vdj-ui-dbcodec-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-dbcodec-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-dbcodec-test COMMAND vdj-ui-dbcodec-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-rblibrary-test test_rb_library.cpp)
target_compile_definitions(vdj-ui-rblibrary-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-rblibrary-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-rblibrary-test COMMAND vdj-ui-rblibrary-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-dbhandler-test test_db_handler.cpp)
target_compile_definitions(vdj-ui-dbhandler-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-dbhandler-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-dbhandler-test COMMAND vdj-ui-dbhandler-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-dbserver-test test_db_server.cpp)
target_compile_definitions(vdj-ui-dbserver-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-dbserver-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-dbserver-test COMMAND vdj-ui-dbserver-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-rbpackets-test test_rb_packets.cpp)
target_compile_definitions(vdj-ui-rbpackets-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-rbpackets-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-rbpackets-test COMMAND vdj-ui-rbpackets-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-rbpresence-test test_rb_presence.cpp)
target_compile_definitions(vdj-ui-rbpresence-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-rbpresence-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-rbpresence-test COMMAND vdj-ui-rbpresence-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-rbmode-test test_rb_mode.cpp)
target_compile_definitions(vdj-ui-rbmode-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-rbmode-test PRIVATE ${QT_TEST} vdj-dbserver)
add_test(NAME vdj-ui-rbmode-test COMMAND vdj-ui-rbmode-test)
# end rekordbox mode
# rekordbox mode
add_executable(vdj-ui-rbpanel-test test_rb_panel.cpp)
target_compile_definitions(vdj-ui-rbpanel-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-rbpanel-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-rbpanel-test COMMAND vdj-ui-rbpanel-test)
set_tests_properties(vdj-ui-rbpanel-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# end rekordbox mode
# LINK export
add_executable(vdj-ui-linkexport-test test_link_export.cpp)
target_compile_definitions(vdj-ui-linkexport-test PRIVATE VDJ_DBSERVER_FIXTURES="${VDJ_DBSERVER_FIXTURES}")
target_link_libraries(vdj-ui-linkexport-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-linkexport-test COMMAND vdj-ui-linkexport-test)
set_tests_properties(vdj-ui-linkexport-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# end LINK export

add_executable(vdj-ui-pendrive-toolbar-test test_pendrive_toolbar.cpp)
target_link_libraries(vdj-ui-pendrive-toolbar-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-pendrive-toolbar-test COMMAND vdj-ui-pendrive-toolbar-test)
set_tests_properties(vdj-ui-pendrive-toolbar-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-autoip-permanent-test test_autoip_permanent.cpp)
target_link_libraries(vdj-ui-autoip-permanent-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-autoip-permanent-test COMMAND vdj-ui-autoip-permanent-test)
set_tests_properties(vdj-ui-autoip-permanent-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-pendrive-registry-test test_pendrive_registry.cpp)
target_link_libraries(vdj-ui-pendrive-registry-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-pendrive-registry-test COMMAND vdj-ui-pendrive-registry-test)
set_tests_properties(vdj-ui-pendrive-registry-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-clone-dialog-test test_clone_dialog.cpp)
target_link_libraries(vdj-ui-clone-dialog-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-clone-dialog-test COMMAND vdj-ui-clone-dialog-test)
set_tests_properties(vdj-ui-clone-dialog-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-packet-logger-test test_packet_logger.cpp)
target_link_libraries(vdj-ui-packet-logger-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-packet-logger-test COMMAND vdj-ui-packet-logger-test)
set_tests_properties(vdj-ui-packet-logger-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-basics-test test_djtoolz_basics.cpp)
target_link_libraries(vdj-ui-djtoolz-basics-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-basics-test COMMAND vdj-ui-djtoolz-basics-test)
set_tests_properties(vdj-ui-djtoolz-basics-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-engine-test test_djtoolz_engine.cpp)
target_link_libraries(vdj-ui-djtoolz-engine-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-engine-test COMMAND vdj-ui-djtoolz-engine-test)
set_tests_properties(vdj-ui-djtoolz-engine-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-tools-test test_djtoolz_tools.cpp)
target_link_libraries(vdj-ui-djtoolz-tools-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-tools-test COMMAND vdj-ui-djtoolz-tools-test)
set_tests_properties(vdj-ui-djtoolz-tools-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-stretch-test test_djtoolz_stretch.cpp)
target_link_libraries(vdj-ui-djtoolz-stretch-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-stretch-test COMMAND vdj-ui-djtoolz-stretch-test)
set_tests_properties(vdj-ui-djtoolz-stretch-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-midi-matic-test test_midi_matic.cpp)
target_link_libraries(vdj-ui-midi-matic-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-midi-matic-test COMMAND vdj-ui-midi-matic-test)
set_tests_properties(vdj-ui-midi-matic-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-presets-test test_djtoolz_presets.cpp)
target_link_libraries(vdj-ui-djtoolz-presets-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-presets-test COMMAND vdj-ui-djtoolz-presets-test)
set_tests_properties(vdj-ui-djtoolz-presets-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-ui-test test_djtoolz_ui.cpp)
target_link_libraries(vdj-ui-djtoolz-ui-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-ui-test COMMAND vdj-ui-djtoolz-ui-test)
set_tests_properties(vdj-ui-djtoolz-ui-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-drum-model-test test_drum_model.cpp)
target_link_libraries(vdj-ui-drum-model-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-drum-model-test COMMAND vdj-ui-drum-model-test)
set_tests_properties(vdj-ui-drum-model-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-drum-machine-test test_drum_machine.cpp)
target_link_libraries(vdj-ui-drum-machine-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-drum-machine-test COMMAND vdj-ui-drum-machine-test)
set_tests_properties(vdj-ui-drum-machine-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-drum-ui-test test_drum_ui.cpp)
target_link_libraries(vdj-ui-drum-ui-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-drum-ui-test COMMAND vdj-ui-drum-ui-test)
set_tests_properties(vdj-ui-drum-ui-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-drum-kits-test test_drum_kits.cpp)
target_link_libraries(vdj-ui-drum-kits-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-drum-kits-test COMMAND vdj-ui-drum-kits-test)
set_tests_properties(vdj-ui-drum-kits-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-ctl-map-test test_djtoolz_ctl_map.cpp)
target_compile_definitions(vdj-ui-djtoolz-ctl-map-test PRIVATE VDJ_DOC_DIR="${CMAKE_SOURCE_DIR}/doc")
target_link_libraries(vdj-ui-djtoolz-ctl-map-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-ctl-map-test COMMAND vdj-ui-djtoolz-ctl-map-test)
set_tests_properties(vdj-ui-djtoolz-ctl-map-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-midi-control-test test_djtoolz_midi_control.cpp)
target_link_libraries(vdj-ui-djtoolz-midi-control-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-midi-control-test COMMAND vdj-ui-djtoolz-midi-control-test)
set_tests_properties(vdj-ui-djtoolz-midi-control-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-djtoolz-controller-test test_djtoolz_controller.cpp)
target_link_libraries(vdj-ui-djtoolz-controller-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-djtoolz-controller-test COMMAND vdj-ui-djtoolz-controller-test)
set_tests_properties(vdj-ui-djtoolz-controller-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-controller-dialog-test test_controller_dialog.cpp)
target_link_libraries(vdj-ui-controller-dialog-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-controller-dialog-test COMMAND vdj-ui-controller-dialog-test)
set_tests_properties(vdj-ui-controller-dialog-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-beatclock-test test_beat_clock.cpp)
target_link_libraries(vdj-ui-beatclock-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-beatclock-test COMMAND vdj-ui-beatclock-test)

add_executable(vdj-ui-playerquantize-test test_player_quantize.cpp)
target_link_libraries(vdj-ui-playerquantize-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-playerquantize-test COMMAND vdj-ui-playerquantize-test)
set_tests_properties(vdj-ui-playerquantize-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-seqbar-test test_seq_bar.cpp)
target_link_libraries(vdj-ui-seqbar-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-seqbar-test COMMAND vdj-ui-seqbar-test)

add_executable(vdj-ui-deckphase-test test_deck_phase.cpp)
target_link_libraries(vdj-ui-deckphase-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-deckphase-test COMMAND vdj-ui-deckphase-test)

add_executable(vdj-ui-joystickmap-test test_joystick_map.cpp)
target_link_libraries(vdj-ui-joystickmap-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-joystickmap-test COMMAND vdj-ui-joystickmap-test)

# LADSPA host, FX tools and the Xone filter plugin (the test loads the plugin built next to vdj-ui)
add_executable(vdj-ui-fx-test test_fx.cpp)
target_compile_definitions(vdj-ui-fx-test PRIVATE VDJ_LADSPA_DIR="${CMAKE_BINARY_DIR}/src/ladspa")
target_link_libraries(vdj-ui-fx-test PRIVATE ${QT_TEST} vdj-cdj)
add_dependencies(vdj-ui-fx-test xone-ladspa)
add_test(NAME vdj-ui-fx-test COMMAND vdj-ui-fx-test)
set_tests_properties(vdj-ui-fx-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# trakcrate master database (plain C++, no Qt types; QtTest only as the runner)
add_executable(vdj-ui-trakcrate-library-test test_trakcrate_library.cpp)
target_link_libraries(vdj-ui-trakcrate-library-test PRIVATE ${QT_TEST} vdj-trakcrate)
add_test(NAME vdj-ui-trakcrate-library-test COMMAND vdj-ui-trakcrate-library-test)

add_executable(vdj-ui-trakcrate-settings-test test_trakcrate_settings.cpp)
target_link_libraries(vdj-ui-trakcrate-settings-test PRIVATE ${QT_TEST} vdj-trakcrate)
add_test(NAME vdj-ui-trakcrate-settings-test COMMAND vdj-ui-trakcrate-settings-test)

add_executable(vdj-ui-trakcrate-import-test test_trakcrate_import.cpp)
target_compile_definitions(vdj-ui-trakcrate-import-test PRIVATE TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate")
target_link_libraries(vdj-ui-trakcrate-import-test PRIVATE ${QT_TEST} vdj-trakcrate)
add_test(NAME vdj-ui-trakcrate-import-test COMMAND vdj-ui-trakcrate-import-test)

add_executable(vdj-ui-trakcrate-panel-test test_trakcrate_panel.cpp)
target_link_libraries(vdj-ui-trakcrate-panel-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-trakcrate-panel-test COMMAND vdj-ui-trakcrate-panel-test)
set_tests_properties(vdj-ui-trakcrate-panel-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-analysis-test test_trakcrate_analysis.cpp)
target_link_libraries(vdj-ui-trakcrate-analysis-test PRIVATE ${QT_TEST} vdj-trakcrate)
add_test(NAME vdj-ui-trakcrate-analysis-test COMMAND vdj-ui-trakcrate-analysis-test)

add_executable(vdj-ui-trakcrate-pdb-roundtrip-test test_trakcrate_pdb_roundtrip.cpp)
target_compile_definitions(vdj-ui-trakcrate-pdb-roundtrip-test PRIVATE
    TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate"
    TRAKCRATE_REFERENCES="${CMAKE_SOURCE_DIR}/references/libcdj")
target_link_libraries(vdj-ui-trakcrate-pdb-roundtrip-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-pdb-roundtrip-test COMMAND vdj-ui-trakcrate-pdb-roundtrip-test)

add_executable(vdj-ui-trakcrate-pdb-build-test test_trakcrate_pdb_build.cpp)
target_compile_definitions(vdj-ui-trakcrate-pdb-build-test PRIVATE TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate")
target_link_libraries(vdj-ui-trakcrate-pdb-build-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-pdb-build-test COMMAND vdj-ui-trakcrate-pdb-build-test)

add_executable(vdj-ui-trakcrate-anlz-roundtrip-test test_trakcrate_anlz_roundtrip.cpp)
target_compile_definitions(vdj-ui-trakcrate-anlz-roundtrip-test PRIVATE
    TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    TRAKCRATE_REFERENCES="${CMAKE_SOURCE_DIR}/references/libcdj")
target_link_libraries(vdj-ui-trakcrate-anlz-roundtrip-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-anlz-roundtrip-test COMMAND vdj-ui-trakcrate-anlz-roundtrip-test)

add_executable(vdj-ui-trakcrate-artwork-test test_trakcrate_artwork.cpp)
target_link_libraries(vdj-ui-trakcrate-artwork-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-trakcrate-artwork-test COMMAND vdj-ui-trakcrate-artwork-test)
set_tests_properties(vdj-ui-trakcrate-artwork-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-editor-test test_trakcrate_editor.cpp)
target_link_libraries(vdj-ui-trakcrate-editor-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-editor-test COMMAND vdj-ui-trakcrate-editor-test)
set_tests_properties(vdj-ui-trakcrate-editor-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-nudge-test test_trakcrate_nudge.cpp)
target_link_libraries(vdj-ui-trakcrate-nudge-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-nudge-test COMMAND vdj-ui-trakcrate-nudge-test)
set_tests_properties(vdj-ui-trakcrate-nudge-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-artwork-edit-test test_trakcrate_artwork_edit.cpp)
target_link_libraries(vdj-ui-trakcrate-artwork-edit-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-artwork-edit-test COMMAND vdj-ui-trakcrate-artwork-edit-test)
set_tests_properties(vdj-ui-trakcrate-artwork-edit-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-scripts-test test_trakcrate_scripts.cpp)
target_link_libraries(vdj-ui-trakcrate-scripts-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-scripts-test COMMAND vdj-ui-trakcrate-scripts-test)
set_tests_properties(vdj-ui-trakcrate-scripts-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(vdj-ui-trakcrate-rbimport-test test_trakcrate_rbimport.cpp)
target_compile_definitions(vdj-ui-trakcrate-rbimport-test PRIVATE TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate")
target_link_libraries(vdj-ui-trakcrate-rbimport-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-rbimport-test COMMAND vdj-ui-trakcrate-rbimport-test)

add_executable(vdj-ui-trakcrate-cuemarks-test test_trakcrate_cuemarks.cpp)
target_compile_definitions(vdj-ui-trakcrate-cuemarks-test PRIVATE TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate")
target_link_libraries(vdj-ui-trakcrate-cuemarks-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-cuemarks-test COMMAND vdj-ui-trakcrate-cuemarks-test)

add_executable(vdj-ui-trakcrate-export-test test_trakcrate_export.cpp)
target_compile_definitions(vdj-ui-trakcrate-export-test PRIVATE TRAKCRATE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/trakcrate")
target_link_libraries(vdj-ui-trakcrate-export-test PRIVATE ${QT_TEST} vdj-trakcrate vdj-cdj)
add_test(NAME vdj-ui-trakcrate-export-test COMMAND vdj-ui-trakcrate-export-test)

add_executable(vdj-ui-level-meter-test test_level_meter.cpp)
target_link_libraries(vdj-ui-level-meter-test PRIVATE ${QT_TEST} vdj-cdj)
add_test(NAME vdj-ui-level-meter-test COMMAND vdj-ui-level-meter-test)
set_tests_properties(vdj-ui-level-meter-test PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
