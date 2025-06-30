import gvsoc.systree

class FractalSync(gvsoc.systree.Component):

    def __init__(self,
                parent: gvsoc.systree.Component,
                name: str,
                level: int):

        super().__init__(parent, name)

        self.add_properties({
            'level' : level,
        })

        self.add_sources(['pulp/fractal_sync/fractal_sync.cpp'])

#we have 2 master ports
    def o_MASTER_N(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('master_n_output_port', itf, signature='wire<PortReq<uint32_t>*>')

    def i_MASTER_N(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'master_n_input_port', signature='wire<PortResp<uint32_t>*>')

    # def o_MASTER_S(self, itf: gvsoc.systree.SlaveItf):
    #     self.itf_bind('master_s_output_port', itf, signature='wire<MstPortOutput<uint32_t>*>')

    # def i_MASTER_S(self) -> gvsoc.systree.SlaveItf:
    #     return gvsoc.systree.SlaveItf(self, 'master_s_input_port', signature='wire<MstPortInput<uint32_t>*>')

#we have 4 slave ports
    # def i_SLAVE_NORD(self) -> gvsoc.systree.SlaveItf:
    #     return gvsoc.systree.SlaveItf(self, 'slave_n_input_port', signature='wire<SlvPortInput<uint32_t>*>')

    # def o_SLAVE_NORD(self, itf: gvsoc.systree.SlaveItf):
    #     self.itf_bind('slave_n_output_port', itf, signature='wire<SlvPortOutput<uint32_t>*>')

    # def i_SLAVE_SUD(self) -> gvsoc.systree.SlaveItf:
    #     return gvsoc.systree.SlaveItf(self, 'slave_s_input_port', signature='wire<SlvPortInput<uint32_t>*>')

    # def o_SLAVE_SUD(self, itf: gvsoc.systree.SlaveItf):
    #     self.itf_bind('slave_s_output_port', itf, signature='wire<SlvPortOutput<uint32_t>*>')

    def i_SLAVE_EAST(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_e_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_EAST(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_e_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    def i_SLAVE_WEST(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'slave_w_input_port', signature='wire<PortReq<uint32_t>*>')

    def o_SLAVE_WEST(self, itf: gvsoc.systree.SlaveItf):
        self.itf_bind('slave_w_output_port', itf, signature='wire<PortResp<uint32_t>*>')

    