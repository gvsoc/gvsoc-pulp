set(NE16_SRCS
    "src/ne16_fsm.cpp"
    "src/ne16.cpp"
    "src/ne16_clear.cpp"
    "src/ne16_debug.cpp"
    "src/ne16_index.cpp"
    "src/ne16_load.cpp"
    "src/ne16_matrixvec.cpp"
    "src/ne16_normquant.cpp"
    "src/ne16_regfile.cpp"
    "src/ne16_streamin.cpp"
    "src/ne16_streamout.cpp"
    "src/ne16_stream.cpp"
    )
vp_model(NAME pulp.ne16.ne16
    SOURCES ${NE16_SRCS}
    )

vp_model_link_options(NAME pulp.ne16.ne16 OPTIONS -fopenmp)

vp_model_include_directories(
    NAME pulp.ne16.ne16
    DIRECTORY "include"
    )

vp_model_compile_options(NAME pulp.ne16.ne16 OPTIONS -fopenmp)
