vp_model(NAME pulp.chips.siracusa.apb_soc_impl
    SOURCES "apb_soc_impl.cpp"
    )

vp_files(FILES "rom.bin" "debug_rom.bin"
    PREFIX "pulp/chips/siracusa"
    )
