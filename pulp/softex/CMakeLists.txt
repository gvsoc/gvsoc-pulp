set(SOFTEX_FLEXFLOAT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../core/models/cpu/iss/flexfloat")

set(SOFTEX_SRCS
    "src/softex.cpp"
    "src/softex_fsm.cpp"
    "src/softex_stream.cpp"
    "src/softex_datapath.cpp"
    )

vp_model(
    NAME pulp.softex.softex
    SOURCES ${SOFTEX_SRCS}
    )

vp_model_include_directories(
    NAME pulp.softex.softex
    DIRECTORY "include"
    )
