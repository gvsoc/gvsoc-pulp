vp_model(NAME "pulp.udma.udma_v3_pulp_impl"
    SOURCES "udma_v3_impl.cpp"
    "uart/udma_uart_v1.cpp"
    "hyper/udma_hyper_v3.cpp"
    "spim/udma_spim_v3.cpp"
    "i2s/udma_i2s_v2.cpp"
    "cpi/udma_cpi_v1.cpp"
)

vp_model_compile_definitions(
    NAME "pulp.udma.udma_v3_pulp_impl"
    DEFINITIONS
    -DUDMA_VERSION=3
    -DHYPER_VERSION=3
    -DI2S_VERSION=2
    -DHAS_UART
    -DHAS_HYPER
    -DHAS_SPIM
    -DHAS_I2S
    -DHAS_CPI
)
