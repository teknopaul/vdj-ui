# The Xone filter as a LADSPA plugin (same Freq / Wild as the LMMS effect). Built next to vdj-ui in <build>/src/ladspa, where
# vdj-ui looks for plugins too, and installed to lib/ladspa for every other LADSPA host.
add_library(xone-ladspa MODULE xone_ladspa.cpp)
set_target_properties(xone-ladspa PROPERTIES PREFIX "" OUTPUT_NAME "xone-ladspa"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/src/ladspa")
install(TARGETS xone-ladspa LIBRARY DESTINATION lib/ladspa)
