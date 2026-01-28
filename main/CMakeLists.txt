set(UI_DIR ./ui)
file(GLOB_RECURSE UI_SOURCES ${UI_DIR}/*.c)

set(MODEL_DIR ./model)
file(GLOB_RECURSE MODEL_SOURCES ${MODEL_DIR}/*.c)
# Exclude indicator_transport.c, indicator_city.c, indicator_sensor.c since we use transport_data.c instead
list(REMOVE_ITEM MODEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${MODEL_DIR}/indicator_transport.c")
list(REMOVE_ITEM MODEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${MODEL_DIR}/indicator_city.c")
list(REMOVE_ITEM MODEL_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/${MODEL_DIR}/indicator_sensor.c")

set(VIEW_DIR ./view)
file(GLOB_RECURSE VIEW_SOURCES ${VIEW_DIR}/*.c)

set(CONTROLLER_DIR ./controller)
file(GLOB_RECURSE CONTROLLER_SOURCES ${CONTROLLER_DIR}/*.c)

set(UTIL_DIR ./util)
file(GLOB_RECURSE UTIL_SOURCES ${UTIL_DIR}/*.c)

idf_component_register(
    SRCS "main.c" "lv_port.c" "arimo_14.c" "arimo_16.c" "arimo_20.c" "arimo_24.c" ${UI_SOURCES} ${MODEL_SOURCES} ${VIEW_SOURCES} ${CONTROLLER_SOURCES} ${UTIL_SOURCES}
    INCLUDE_DIRS "."  ${UI_DIR} ${MODEL_DIR} ${VIEW_DIR} ${CONTROLLER_DIR} ${UTIL_DIR})
