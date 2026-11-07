find_library(CDJ_LIBRARY cdj PATHS /usr/lib /lib /usr/local/lib REQUIRED)
find_library(VDJ_LIBRARY vdj PATHS /usr/lib /lib /usr/local/lib REQUIRED)

add_library(cdj_imported UNKNOWN IMPORTED)
set_target_properties(cdj_imported PROPERTIES IMPORTED_LOCATION "${CDJ_LIBRARY}")
add_library(vdj_imported UNKNOWN IMPORTED)
set_target_properties(vdj_imported PROPERTIES IMPORTED_LOCATION "${VDJ_LIBRARY}")

# libcdj's userspace NFS v2 client (the players speak NFS v2 over UDP, which the kernel cannot mount). libcdj.so does
# not export it, so it is compiled from the libcdj source tree.
set(CDJ_SOURCE_DIR "${CMAKE_SOURCE_DIR}/references/libcdj" CACHE PATH "libcdj source tree (for src/nfs)")
set(CDJ_NFS_DIR "${CDJ_SOURCE_DIR}/src/nfs")
if(NOT EXISTS "${CDJ_NFS_DIR}/nfs_client.c")
    message(FATAL_ERROR "libcdj sources not found in ${CDJ_SOURCE_DIR}; set -DCDJ_SOURCE_DIR=...")
endif()
add_library(xdj-nfs STATIC
    ${CDJ_NFS_DIR}/xdr.c
    ${CDJ_NFS_DIR}/rpc.c
    ${CDJ_NFS_DIR}/utf16.c
    ${CDJ_NFS_DIR}/portmap_client.c
    ${CDJ_NFS_DIR}/mount_client.c
    ${CDJ_NFS_DIR}/nfs_client.c
    nfs/xdj_nfs.cpp
)
set_target_properties(xdj-nfs PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(xdj-nfs PRIVATE ${CDJ_NFS_DIR})
target_include_directories(xdj-nfs PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/nfs)
if(Qt6_FOUND)
    target_link_libraries(xdj-nfs PUBLIC Qt6::Core)
else()
    target_link_libraries(xdj-nfs PUBLIC Qt5::Core)
endif()

add_library(vdj-cdj STATIC
    cdj/cdj_wrapper.cpp
    vdj/vdj_wrapper.cpp
    vdj/backline_model.cpp
    config/config_manager.cpp
    debug/packet_logger.cpp
    audio/audio_decoder.cpp
    audio/ladspa_host.cpp
    audio/fx_rack.cpp
    audio/audio_output.cpp
    audio/audio_player.cpp
    audio/beat_clock.cpp
    audio/quantize_settings.cpp
    audio/deck_phase.cpp
    audio/beat_source.cpp
    audio/sample_cache.cpp
    audio/wav_info.cpp
    audio/voice_mixer.cpp
    audio/djtoolz_engine.cpp
    audio/djtoolz_tool.cpp
    audio/djtoolz_scheduler.cpp
    audio/sample_stretch.cpp
    audio/djtoolz_presets.cpp
    audio/swing.cpp
    audio/drum_pattern.cpp
    audio/drum_kits.cpp
    audio/time_stretcher.cpp
    midi/alsa_clock.cpp
    midi/seq_bar.cpp
    midi/joystick_map.cpp
    midi/joystick_seq_bridge.cpp
    midi/diff_lock.cpp
    midi/sync_telemetry.cpp
    midi/seq_phase_model.cpp
    midi/sync_controller.cpp
    midi/sync_aligner.cpp
    midi/midi_map_input.cpp
    midi/djtoolz_ctl_map.cpp
    midi/djtoolz_midi_control.cpp
    midi/midi_router.cpp
    midi/midi_out.cpp
    midi/instrument_manager.cpp
    audio/audio_recorder.cpp
    nfs/id3_tags.cpp
    nfs/remote_deck.cpp
    nfs/nfs_mount_manager.cpp
    ui/dark_theme.cpp
    ui/deck_widget.cpp
    ui/deck_controls.cpp
    ui/diff_lock_bar.cpp
    ui/pitch_slider.cpp
    ui/keyboard_shortcuts.cpp
    ui/beat_grid_widget.cpp
    ui/deck_area.cpp
    ui/usb_config_dialog.cpp
    ui/joystick_config_dialog.cpp
    ui/track_browser.cpp
    ui/sample_tree_model.cpp
    ui/djtoolz_icons.cpp
    ui/drum_voice_selector.cpp
    ui/step_chooser.cpp
    ui/drum_editor.cpp
    ui/swing_slider.cpp
    ui/beat_chooser.cpp
    ui/sample_chooser.cpp
    ui/djtoolz_tool_widget.cpp
    ui/djtoolz_panel.cpp
    ui/djtoolz_controller.cpp
    ${CMAKE_SOURCE_DIR}/resources.qrc
    ui/track_details.cpp
    ui/track_analysis.cpp
    ui/waveform_delegate.cpp
    ui/waveform_widget.cpp
    ui/waveform_window.cpp
    ui/player_panel.cpp
    ui/quantize_button.cpp
    export/playlist_export.cpp
    ui/debug_panel.cpp
    ui/debug_overlay.cpp
    ui/autoip_status_widget.cpp
    ui/bpm_ramp_widget.cpp
    ui/hud_window.cpp
    ui/pendrive_clone_dialog.cpp
    ui/virtual_pendrives_dialog.cpp
    ui/pendrive_toolbar.cpp
    ui/recording_panel.cpp
    ui/rekordbox_mode_panel.cpp
    ui/trakcrate_job.cpp
    ui/trakcrate_jobs_strip.cpp
    ui/mounted_sticks.cpp
    ui/trakcrate_track_model.cpp
    ui/trakcrate_import_dialog.cpp
    ui/trakcrate_bpm_dialog.cpp
    ui/trakcrate_pendrive_dialog.cpp
    ui/trakcrate_settings_dialogs.cpp
    ui/trakcrate_settings_viewer.cpp
    ui/trakcrate_analysis.cpp
    ui/star_rating_widget.cpp
    ui/level_meter_widget.cpp
    ui/trakcrate_track_editor.cpp
    ui/artwork_view.cpp
    ui/trakcrate_scripts.cpp
    ui/trakcrate_preview_player.cpp
    ui/artwork_launcher.cpp
    ui/trakcrate_rbimport_dialog.cpp
    ui/trakcrate_panel.cpp
    ui/trakcrate_export_dialog.cpp
    ui/trakcrate_search_dialog.cpp
    ui/dir_usage.cpp
    ui/trakcrate_artwork.cpp
    ui/instrument_config_dialog.cpp
    ui/midi_monitor_dialog.cpp
    ui/controller_dialog.cpp
    ui/main_window.cpp
    ui/trakcrate_window.cpp
    network/beat_tempo.cpp
    network/beat_phase.cpp
    network/network_manager.cpp
    network/link_export.cpp
    export/pendrive_registry.cpp
    network/network_config_dialog.cpp
    network/auto_ip_manager.cpp
)

target_include_directories(vdj-cdj PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

# Audio preview: ffmpeg decodes, PulseAudio (or PipeWire's pulse server) plays
find_package(PkgConfig REQUIRED)
find_package(Threads REQUIRED)
pkg_check_modules(AUDIO REQUIRED IMPORTED_TARGET libavformat libavcodec libswresample libavutil libpulse libpulse-simple)
target_link_libraries(vdj-cdj PUBLIC PkgConfig::AUDIO Threads::Threads ${CMAKE_DL_LIBS})
target_link_libraries(vdj-cdj PUBLIC xdj-nfs cdj_imported vdj_imported)

# rekordbox mode: dbserver protocol (see .planning/plans/rekordbox-pc-mode_PLAN.md)
# NFS file service (portmap + mountd + NFS v2, read-only) from the libcdj tree; its xdr/rpc/utf16/mount code is in xdj-nfs
add_library(vdj-nfsd STATIC
    ${CDJ_NFS_DIR}/nfs_server.c
    ${CDJ_NFS_DIR}/mount_server.c
    ${CDJ_NFS_DIR}/nfs_dirfs.c
    ${CDJ_NFS_DIR}/portmap_server.c
    ${CDJ_NFS_DIR}/nfsd.c
)
set_target_properties(vdj-nfsd PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vdj-nfsd PUBLIC ${CDJ_NFS_DIR})
find_package(Threads REQUIRED)
target_link_libraries(vdj-nfsd PUBLIC xdj-nfs Threads::Threads)

add_library(vdj-dbserver STATIC
    dbserver/db_codec.cpp
    dbserver/rb_library.cpp
    dbserver/db_handler.cpp
    dbserver/db_blobs.cpp
    dbserver/db_server.cpp
    dbserver/rb_packets.cpp
    dbserver/rb_presence.cpp
    dbserver/rb_mode_options.cpp
    dbserver/rb_netif.cpp
    dbserver/nfs_observer.cpp
    dbserver/rb_mode.cpp
)
set_target_properties(vdj-dbserver PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(vdj-dbserver PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
# pdb/anlz parsers come from the installed libcdj (<cdj/pdb.h>, <cdj/anlz.h>), as for the track browser
target_link_libraries(vdj-dbserver PUBLIC cdj_imported vdj-nfsd)
if(Qt6_FOUND)
    target_link_libraries(vdj-dbserver PUBLIC Qt6::Network)
else()
    target_link_libraries(vdj-dbserver PUBLIC Qt5::Network)
endif()
target_link_libraries(vdj-cdj PUBLIC vdj-dbserver)
# end rekordbox mode

# trakcrate: the master database and rekordbox pendrive export (doc/trakcrate/). Plain C++17, no Qt, so it can move to the command line tools.
add_library(vdj-trakcrate STATIC
    trakcrate/fsutil.cpp
    trakcrate/natsort.cpp
    trakcrate/sha256.cpp
    trakcrate/track.cpp
    trakcrate/library.cpp
    trakcrate/audio_probe.cpp
    trakcrate/importer.cpp
    trakcrate/audio_decode.cpp
    trakcrate/analysis_beats.cpp
    trakcrate/analysis_wave.cpp
    trakcrate/cuemark_render.cpp
    trakcrate/cuemark_edit.cpp
    trakcrate/grid_edit.cpp
    trakcrate/artwork_edit.cpp
    trakcrate/scripts.cpp
    trakcrate/bpm_detect.cpp
    trakcrate/analysis.cpp
    trakcrate/pdb_writer.cpp
    trakcrate/ext_pdb.cpp
    trakcrate/pdb_build.cpp
    trakcrate/pdb_append.cpp
    trakcrate/anlz_writer.cpp
    trakcrate/mp3_index.cpp
    trakcrate/settings_files.cpp
    trakcrate/settings_defaults.cpp
    trakcrate/settings_sets.cpp
    trakcrate/settings_view.cpp
    trakcrate/default_files.cpp
    trakcrate/artwork_paths.cpp
    trakcrate/exporter.cpp
    trakcrate/rb_import.cpp
    trakcrate/usb_speed.cpp
)
set_target_properties(vdj-trakcrate PROPERTIES POSITION_INDEPENDENT_CODE ON)
# the player's support files, compiled in with .incbin (default_files.cpp); a change to them rebuilds it
set(TC_USER_SETTINGS_DIR ${CMAKE_SOURCE_DIR}/data/trakcrate/user-settings)
set_source_files_properties(trakcrate/default_files.cpp PROPERTIES
    COMPILE_DEFINITIONS "TC_USER_SETTINGS_DIR=\"${TC_USER_SETTINGS_DIR}\""
    OBJECT_DEPENDS "${TC_USER_SETTINGS_DIR}/djprofile.nxs;${TC_USER_SETTINGS_DIR}/RBFLTR.DAT;${TC_USER_SETTINGS_DIR}/exportExt.pdb")
target_include_directories(vdj-trakcrate PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
pkg_check_modules(TC_FFMPEG REQUIRED IMPORTED_TARGET libavformat libavcodec libavutil libswresample)
pkg_check_modules(TC_AUBIO IMPORTED_TARGET aubio)       # libaubio-dev: BPM detection for the "Detect BPM" action
if(TC_AUBIO_FOUND)
    target_link_libraries(vdj-trakcrate PUBLIC PkgConfig::TC_AUBIO)
    target_compile_definitions(vdj-trakcrate PUBLIC TC_HAVE_AUBIO=1)
else()
    message(WARNING "libaubio not found: trakcrate BPM detection disabled (apt install libaubio-dev)")
endif()
target_link_libraries(vdj-trakcrate PUBLIC PkgConfig::TC_FFMPEG cdj_imported)     # cdj: the pdb readers verifyPdb checks against
target_link_libraries(vdj-cdj PUBLIC vdj-trakcrate)

# vdj-usb-watch: prints CONNECT / DISCONNECT lines with the USB version of plugged devices (see ai-output/usb-detection.md)
add_executable(vdj-usb-watch tools/usb_watch.cpp)
target_link_libraries(vdj-usb-watch PRIVATE vdj-trakcrate)
# end trakcrate

find_library(ASOUND_LIBRARY asound PATHS /usr/lib /lib /usr/lib/x86_64-linux-gnu REQUIRED)
if(ASOUND_LIBRARY)
    target_link_libraries(vdj-cdj PUBLIC ${ASOUND_LIBRARY})
endif()

if(Qt6_FOUND)
    target_link_libraries(vdj-cdj PUBLIC Qt6::Widgets Qt6::Network)
else()
    target_link_libraries(vdj-cdj PUBLIC Qt5::Widgets Qt5::Network)
endif()

add_executable(vdj-ui main.cpp)
target_link_libraries(vdj-ui PRIVATE vdj-cdj)

# A rebuilt binary loses its file capability, and UDP port 111 (LINK export) needs it: give it back after every link, as root.
# sudo asks for the password in the terminal when it has to; a failure only prints a warning. Turn off with cmake -DVDJ_SETCAP=OFF .
option(VDJ_SETCAP "setcap cap_net_bind_service on vdj-ui after building (uses sudo)" ON)
if(VDJ_SETCAP)
    find_program(SETCAP_PROGRAM setcap PATHS /usr/sbin /sbin)
    if(SETCAP_PROGRAM)
        add_custom_command(TARGET vdj-ui POST_BUILD
            COMMAND sudo ${SETCAP_PROGRAM} cap_net_bind_service=+ep $<TARGET_FILE:vdj-ui>
                    || ${CMAKE_COMMAND} -E echo "WARNING: setcap failed, UDP port 111 (LINK export) needs root. See doc/privileged-port-111.md"
            COMMENT "setcap cap_net_bind_service on vdj-ui (sudo)"
            VERBATIM)
    endif()
endif()

install(TARGETS vdj-ui RUNTIME DESTINATION bin)
