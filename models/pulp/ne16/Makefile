ifdef XTENSOR_INCLUDE_DIR
IMPLEMENTATIONS += pulp/ne16/ne16
pulp/ne16/ne16_SRCS = pulp/ne16/src/ne16.cpp \
                      pulp/ne16/src/ne16_fsm.cpp \
                      pulp/ne16/src/ne16_stream.cpp \
                      pulp/ne16/src/ne16_regfile.cpp \
                      pulp/ne16/src/ne16_clear.cpp \
                      pulp/ne16/src/ne16_load.cpp \
                      pulp/ne16/src/ne16_streamin.cpp \
                      pulp/ne16/src/ne16_matrixvec.cpp \
                      pulp/ne16/src/ne16_streamout.cpp \
                      pulp/ne16/src/ne16_normquant.cpp \
                      pulp/ne16/src/ne16_index.cpp \
                      pulp/ne16/src/ne16_debug.cpp
pulp/ne16/ne16_CFLAGS = -Ipulp/ne16/include -DXTENSOR_USE_XSIMD -isystem $(XTENSOR_INCLUDE_DIR) -march=native -std=c++14
else
IMPLEMENTATIONS += pulp/ne16/ne16
pulp/ne16/ne16_SRCS = pulp/ne16/src/ne16_empty.cpp
endif
