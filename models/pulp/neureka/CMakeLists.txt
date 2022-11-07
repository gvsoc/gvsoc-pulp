set(NEUREKA_SRCS
    "src/neureka_fsm.cpp"
    "src/neureka.cpp"
    "src/neureka_clear.cpp"
    "src/neureka_debug.cpp"
    "src/neureka_index.cpp"
    "src/neureka_load.cpp"
    "src/neureka_matrixvec.cpp"
    "src/neureka_normquant.cpp"
    "src/neureka_regfile.cpp"
    "src/neureka_streamin.cpp"
    "src/neureka_streamout.cpp"
    "src/neureka_stream.cpp"
    )
vp_model(NAME pulp.neureka.neureka
    SOURCES ${NEUREKA_SRCS}
    )

vp_model_include_directories(
    NAME pulp.neureka.neureka
    DIRECTORY "include"
    )
