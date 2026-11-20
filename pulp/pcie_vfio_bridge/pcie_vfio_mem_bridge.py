import gvsoc.systree


class PCIeVfioMemBridge(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                socket_path: str,
                bar0_size: int,
                dma_chunk_bytes: int=4096):

        super().__init__(parent, name)

        self.add_properties({
            'socket_path': socket_path,
            'bar0_size': bar0_size,
            'dma_chunk_bytes': dma_chunk_bytes,
        })

        self.set_component('pulp.pcie_vfio_bridge.pcie_vfio_mem_bridge')

    def i_IRQ_DONE(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'done_irq', signature='wire<bool>')

    def o_MEM(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('mem', itf, signature='io')

    def o_FETCH_ENABLE(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('fetch_en', itf, signature='wire<bool>')

    def o_ENTRY_ADDR(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('entry_addr', itf, signature='wire<uint64_t>')
