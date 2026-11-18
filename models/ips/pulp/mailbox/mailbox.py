import gvsoc.systree

'''
Mailbox device model for GVSOC.
This device allows multiple "mailboxes" to be defined, each of which can hold up to two 32-bit "letters".
The device provides an interface for reading/writing these letters, as well as control registers to enable/disable sending and receiving interupts when "subscribed" to it.
When a letter is sent or received, the device can trigger an interrupt to notify the component connected to it.
Autorh: Leonardo Domenicali leonardo.domenicali@studio.unibo.it | leonardo.domenicali@gmail.com
'''
class Mailbox(gvsoc.systree.Component):
    def __init__(self, parent: gvsoc.systree.Component, name: str, size: int):

        super().__init__(parent, name)

        self.add_sources(['ips/pulp/mailbox/mailbox.cpp'])

        self.add_properties({
            "size": size
        })

    def i_INPUT(self) -> gvsoc.systree.SlaveItf:
        return gvsoc.systree.SlaveItf(self, 'input', signature='io')

    # The IRQ wire output
    def o_SND_IRQ(self,id:int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'irq_snd_{id}', itf, signature='wire<bool>')
    
    def o_RCV_IRQ(self,id:int, itf: gvsoc.systree.SlaveItf):
        self.itf_bind(f'irq_rcv_{id}', itf, signature='wire<bool>')
