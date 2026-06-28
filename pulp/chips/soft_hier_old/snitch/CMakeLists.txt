vp_files(FILES "bootrom.bin"
    PREFIX "pulp/chips/soft_hier_old/snitch"
    )

vp_model(NAME pulp.chips.soft_hier_old.snitch.sequencer
    SOURCES "sequencer.cpp"
)
