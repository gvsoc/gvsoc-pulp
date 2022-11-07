set(PULP_ISS_WRAPPER "pulp_iss_wrapper.cpp")

generate_isa(NAME pulp.cpu.iss.iss_pulp_fc
    THINGY "--core=Pulp_fc_core@pulp.cpu.iss.pulp_cores"
    SOURCES ${PULP_ISS_WRAPPER}
    )
vp_model_compile_definitions(
    NAME pulp.cpu.iss.iss_pulp_fc
    DEFINITIONS
    -DEXTERNAL_ISS_WRAPPER=1
    -DPIPELINE_STAGES=2
    -DISS_SINGLE_REGFILE
    -DPCER_VERSION_2
    -DPRIV_1_10
    )

generate_isa(NAME pulp.cpu.iss.iss_pulp_cluster
    THINGY "--core=Pulp_cluster_core@pulp.cpu.iss.pulp_cores"
    SOURCES ${PULP_ISS_WRAPPER}
    )
vp_model_compile_definitions(
    NAME pulp.cpu.iss.iss_pulp_cluster
    DEFINITIONS
    -DEXTERNAL_ISS_WRAPPER=1
    -DPIPELINE_STAGES=2
    -DISS_SINGLE_REGFILE
    -DPCER_VERSION_2
    -DPRIV_1_10
    )
