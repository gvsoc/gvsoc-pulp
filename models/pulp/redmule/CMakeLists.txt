set(REDMULE_SRCS
    "src/redmule.cpp"
    "src/redmule_fsm.cpp"
    "src/redmule_engine.cpp"
    "src/redmule_scheduler.cpp"
    "src/redmule_streamer.cpp"
    "src/redmule_buffers.cpp"
    )

vp_model(
	NAME pulp.redmule.redmule
	SOURCES ${REDMULE_SRCS}
        )

vp_model_include_directories(
    NAME pulp.redmule.redmule
    DIRECTORY "include"
    )
