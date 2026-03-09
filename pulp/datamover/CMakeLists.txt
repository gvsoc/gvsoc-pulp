set(DATAMOVER_SRCS
    "src/datamover.cpp"
    "src/datamover_regfile.cpp"
    "src/datamover_engine.cpp"
    "src/datamover_memory.cpp"
    )
vp_model(NAME pulp.datamover.datamover
    SOURCES ${DATAMOVER_SRCS}
    )

set(HWPE_INCS
    "include"
)

vp_model_include_directories(
    NAME pulp.datamover.datamover
    DIRECTORY ${HWPE_INCS}
    )

vp_model_compile_options(NAME pulp.datamover.datamover OPTIONS)
