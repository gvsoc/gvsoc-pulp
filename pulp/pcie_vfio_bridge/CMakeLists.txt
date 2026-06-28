vp_model(NAME pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge
    SOURCES "pcie_vfio_mem_bridge.cpp"
)

vp_model_include_directories(NAME pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge
    DIRECTORY /usr/local/include
)

vp_model_link_options(NAME pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge
    OPTIONS
        -L/usr/local/lib/x86_64-linux-gnu
        -Wl,--no-as-needed
        -Wl,--no-undefined
)

vp_model_link_libraries(NAME pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge
    LIBRARY vfio-user
)
