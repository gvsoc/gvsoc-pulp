set(NEUREKA_SRCS
"src/neureka_debug.cpp"
"src/neureka_fsm.cpp"
"src/neureka_streamin.cpp"
"src/neureka_infeatload.cpp"
"src/neureka_outfeatstore.cpp"
"src/neureka_woffs.cpp"
"src/neureka_matrixvec.cpp"
"src/neureka_normquant.cpp"
"src/neureka.cpp"
    )
vp_model(NAME pulp.neureka.neureka
    SOURCES ${NEUREKA_SRCS}
    )
set(NEUREKA_INCS
"inc/utils"
"inc/norm"
"inc/binconv"
"inc/buffer"
"inc/control"
"inc/stream"
"inc/input_buffer"
"inc/pe"
"inc/"
)

vp_model_include_directories(
    NAME pulp.neureka.neureka
    DIRECTORY ${NEUREKA_INCS}
    )

vp_model_compile_definitions(
    NAME pulp.neureka.neureka
    DEFINITIONS
    -DXTENSOR_USE_XSIMD
    )
    

vp_model_compile_options(NAME pulp.neureka.neureka OPTIONS)
