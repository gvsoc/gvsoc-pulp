import gvsoc.systree

class XifDecoder(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str):

        super().__init__(parent, name)

        self.add_sources(['pulp/xif_decoder/xif_decoder.cpp'])

    def i_OFFLOAD_M(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_m', signature='wire<IssOffloadInsn<uint32_t>*>')

    def o_OFFLOAD_GRANT_M(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_grant_m', itf, signature='wire<IssOffloadInsnGrant<uint32_t>*>')

    def o_OFFLOAD_S1(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_s1', itf, signature='wire<IssOffloadInsn<uint32_t>*>')

    def i_OFFLOAD_GRANT_S1(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_grant_s1', signature='wire<IssOffloadInsnGrant<uint32_t>*>')
    
    def o_OFFLOAD_S2(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('offload_s2', itf, signature='wire<IssOffloadInsn<uint32_t>*>')
    
    def i_OFFLOAD_GRANT_S2(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'offload_grant_s2', signature='wire<IssOffloadInsnGrant<uint32_t>*>')
    
    def o_XIF_2_FRACTAL(self, itf: gvsoc.systree.SlaveItf):
        #master port
        self.itf_bind('fractal_input_port', itf, signature='wire<SlvPortInput<uint32_t>*>')

    def i_FRACTAL_2_XIF(self) -> gvsoc.systree.SlaveItf:
        #slave port
        return gvsoc.systree.SlaveItf(self, 'fractal_output_port', signature='wire<SlvPortOutput<uint32_t>*>')
    