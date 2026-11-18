#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp> 
#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
/** 
 *@brief:   Mailbox device model for GVSOC.
 *@details: This device allows multiple "mailboxes" to be defined, each of which can hold up to two 32-bit "letters".
 *   The device provides an interface for reading/writing these letters, as well as control registers to enable/disable sending and receiving interupts when "subscribed" to it.
 *   When a letter is sent or received, the device can trigger an interrupt to notify the component connected to it.
 *@auth: Leonardo Domenicali leonardo.domenicali@studio.unibo.it | leonardo.domenicali@gmail.com
*/

/**
 * @brief:  offsets for the internal rappresentation of the mailbox status and control registers.
 * @details: The control registers are rappresented as bits in a 8-bit u-integer, so that they can be easily manipulated with bitwise operations while keeping the footprint of the model small
 */
enum internalRegisterOffsets {
    SND_STAT =  0,
    SND_SET =   1,
    SND_CLR =   2,
    SND_EN =    3,
    RCV_STAT =  4,
    RCV_SET =   5,
    RCV_CLR =   6,
    RCV_EN =    7
};

/**
 * @brief  offsets for the memory-mapped registers of the device.
 * @details These offsets are used to decode the incoming requests and to identify which register is being accessed.
 */
enum registerOffsets {
    INT_SND_STAT =  0x0,
    INT_SND_SET =   0x4,
    INT_SND_CLR =   0x8,
    INT_SND_EN =    0xC,
    INT_RCV_STAT =  0x40,
    INT_RCV_SET =   0x44,
    INT_RCV_CLR =   0x48,
    INT_RCV_EN =    0x4C,
    LETTER_0 =      0x80, 
    LETTER_1 =      0x8C
};

/**
 * @brief  helper function to check the status of a register bit.
 * @param  reg the register value to check.
 * @param  offset the offset of the bit to check, as defined in internalRegister
 * @return true if the bit at the given offset is set, false otherwise.
 */
bool check_register(uint8_t reg, internalRegisterOffsets offset) {
    return (reg & (1u << offset)) != 0;
}

/**
 * @brief  helper function to drive the interrupt wire to the given level.
 * @param  irq the wire to drive.
 * @param  level the level to drive the wire to.
 */
static void drive_irq(vp::WireMaster<bool> &irq, bool level)
{
    if (irq.is_bound())
        irq.sync(level);
}

class Mailbox : public vp::Component
{
public:
    Mailbox(vp::ComponentConf &conf);
    ~Mailbox(); 
    void reset(bool active) override;

private:
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    
    vp::Trace trace;
    vp::IoSlave input_itf;

    size_t mailboxSize;

    vp::WireMaster<bool> *irq_rcvs;
    vp::WireMaster<bool> *irq_snds;
    
    std::vector<uint8_t> registers;
    std::vector<std::pair<uint32_t, uint32_t>> letters;
    
    vp::ClockEvent fsm_event;
    static void fsm_handler(vp::Block *__this, vp::ClockEvent *event);
};

Mailbox::Mailbox(vp::ComponentConf &conf)
    : vp::Component(conf), fsm_event(this, Mailbox::fsm_handler)
{
    traces.new_trace("trace", &trace, vp::DEBUG);

    this->input_itf.set_req_meth(&Mailbox::req);
    this->new_slave_port("input", &this->input_itf);

    this->mailboxSize = this->get_js_config()->get("size")->get_int();

    irq_snds = new vp::WireMaster<bool>[this->mailboxSize];
    irq_rcvs = new vp::WireMaster<bool>[this->mailboxSize];

    for(int i = 0; i < this->mailboxSize; i++) {
        this->new_master_port("irq_snd_"+std::to_string(i), &this->irq_snds[i]);
        this->new_master_port("irq_rcv_"+std::to_string(i), &this->irq_rcvs[i]);
        this->registers.push_back(0);
        this->letters.push_back({0, 0});
    }
}

Mailbox::~Mailbox() {
    delete[] irq_snds;
    delete[] irq_rcvs;
}

void Mailbox::fsm_handler(vp::Block *__this, vp::ClockEvent *event)
{
    // Unused for now
}

void Mailbox::reset(bool active)
{
    if (!active)
        return;

    for (size_t i = 0; i < this->mailboxSize; ++i) {
        const uint8_t previous = this->registers[i];
        this->registers[i] = 0;
        this->letters[i] = {0, 0};
        // The wires start low. Notify receivers only when reset changes a line.
        if (check_register(previous, SND_SET) && check_register(previous, SND_EN))
            drive_irq(this->irq_snds[i], false);
        if (check_register(previous, RCV_SET) && check_register(previous, RCV_EN))
            drive_irq(this->irq_rcvs[i], false);
    }
}

/**
 * @brief  Method called when a request is received on the input interface. It decodes the request and updates the internal state of the device accordingly, 
 *          as well as triggering interrupts if needed.
 * @param  __this Pointer to the block instance receiving the request, which is casted to a Mailbox instance to access its internal state.
 * @param  req Pointer to the request object containing the details of the request, such as the address, data, and whether it is a read or write operation.
 * @return The status of the request, which can be IO_REQ_OK if the request was successfully handled, or IO_REQ_INVALID if the request was invalid
 */
vp::IoReqStatus Mailbox::req(vp::Block *__this, vp::IoReq *req)
{
    Mailbox *_this = (Mailbox *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint8_t *data = req->get_data(); 

    if (req->get_size() != sizeof(uint32_t) || data == nullptr ||
        (req->get_opcode() != vp::READ && req->get_opcode() != vp::WRITE))
        return vp::IO_REQ_INVALID;

    uint32_t value = 0;
    if (is_write)
        std::memcpy(&value, data, sizeof(value));
    
    _this->trace.msg("Received request: addr=0x%lx, is_write=%d\n", offset, is_write);
    
    uint64_t mailbox_index = offset >> 8;
    if(mailbox_index >= _this->mailboxSize) {
        _this->trace.msg("Invalid mailbox_index: 0x%lx\n", mailbox_index);
        return vp::IO_REQ_INVALID;
    }
    
    uint64_t reg_mailbox_index = offset & 0xFF;
    
    switch(reg_mailbox_index) {
        case LETTER_0:
            if(is_write) {
                _this->letters[mailbox_index].first = value;
                _this->trace.msg("Written letter 0: 0x%x\n", _this->letters[mailbox_index].first);
            } else {
                value = _this->letters[mailbox_index].first;
                _this->trace.msg("Read letter 0: 0x%x\n", _this->letters[mailbox_index].first);
            }
            break;
            
        case LETTER_1:
            if(is_write) {
                _this->letters[mailbox_index].second = value;
                _this->trace.msg("Written letter 1: 0x%x\n", _this->letters[mailbox_index].second);
            } else {
                value = _this->letters[mailbox_index].second;
                _this->trace.msg("Read letter 1: 0x%x\n", _this->letters[mailbox_index].second);
            }
            break;

        case INT_SND_STAT:
            if(!is_write) {
                _this->trace.msg("Read mailbox %d snd interrupt status\n", mailbox_index);
                value = check_register(_this->registers[mailbox_index], SND_SET);
            }    
            break;
            
        case INT_SND_SET:
            if(is_write) {
                _this->trace.msg("Set status of mailbox %d\n", mailbox_index);
                _this->registers[mailbox_index] |= (1u << SND_SET);
                if(check_register(_this->registers[mailbox_index], SND_EN)) {
                    drive_irq(_this->irq_snds[mailbox_index], true);
                    _this->trace.msg("Send interrupt asserted of mailbox %d\n", mailbox_index);
                }
            }
            break;
            
        case INT_SND_CLR:
            if(is_write) {
                _this->registers[mailbox_index] &= ~(1u << SND_SET);              
              
                drive_irq(_this->irq_snds[mailbox_index], false);
                _this->trace.msg("Send interrupt deasserted of mailbox %d\n", mailbox_index);   
            }
            break;
            
        case INT_SND_EN:
            if(is_write) {
                if (value == 0) {
                    _this->registers[mailbox_index] &= ~(1u << SND_EN);
                    _this->trace.msg("Disable send interrupt of mailbox %d\n", mailbox_index);
                }
                else if (value == 1) {
                    _this->registers[mailbox_index] |= (1u << SND_EN);
                    _this->trace.msg("Enable send interrupt of mailbox %d\n", mailbox_index);
                }
                else{
                    _this->trace.msg("Invalid value for send interrupt enable of mailbox %d: 0x%x\n", mailbox_index, value);
                    return vp::IO_REQ_INVALID;
                }
                // Set the interrupt line according to the new value and the current status of the mailbox, if the interrupt is enabled and the status is set, the interrupt line should be asserted, otherwise it should be deasserted.
                drive_irq(_this->irq_snds[mailbox_index], value != 0 && check_register(_this->registers[mailbox_index], SND_SET));
            }
            else{
                value = check_register(_this->registers[mailbox_index], SND_EN);
                _this->trace.msg("Read send interrupt enable of mailbox %d\n", mailbox_index);
            }
            break;

        case INT_RCV_STAT:
            if(!is_write) {
                _this->trace.msg("Read mailbox %d rcv interrupt status\n", mailbox_index);
                value = check_register(_this->registers[mailbox_index], RCV_SET);
            }    
            break;
            
        case INT_RCV_SET:
            if (is_write) {
                _this->trace.msg("Set status of mailbox %d\n", mailbox_index);
                _this->registers[mailbox_index] |= (1u << RCV_SET);
                if (check_register(_this->registers[mailbox_index], RCV_EN)) {
                    drive_irq(_this->irq_rcvs[mailbox_index], true);
                    _this->trace.msg("Receive interrupt asserted of mailbox %d\n", mailbox_index);
                }
            }
            break;
            
        case INT_RCV_CLR:
            if(is_write) {
                _this->registers[mailbox_index] &= ~(1u << RCV_SET);

                drive_irq(_this->irq_rcvs[mailbox_index], false);
                _this->trace.msg("Receive interrupt deasserted of mailbox %d\n", mailbox_index);
            }
            break;
            
        case INT_RCV_EN:
            if(is_write) {
                if (value == 0) {
                    _this->registers[mailbox_index] &= ~(1u << RCV_EN);
                    _this->trace.msg("Disable receive interrupt of mailbox %d\n", mailbox_index);
                }
                else if (value == 1) {
                    _this->registers[mailbox_index] |= (1u << RCV_EN);
                    _this->trace.msg("Enable receive interrupt of mailbox %d\n", mailbox_index);
                }
                else{
                    _this->trace.msg("Invalid value for receive interrupt enable of mailbox %d: 0x%x\n", mailbox_index, value);
                    return vp::IO_REQ_INVALID;
                }
                // Set the interrupt line according to the new value and the current status of the mailbox, if the interrupt is enabled and the status is set, the interrupt line should be asserted, otherwise it should be deasserted.
                drive_irq(_this->irq_rcvs[mailbox_index], value != 0 && check_register(_this->registers[mailbox_index], RCV_SET));
            }
            else{
                value = check_register(_this->registers[mailbox_index], RCV_EN);
                _this->trace.msg("Read receive interrupt enable of mailbox %d\n", mailbox_index);
            }
            break;
            
        default :
            _this->trace.msg("Invalid register mailbox_index: 0x%lx\n", reg_mailbox_index);
            return vp::IO_REQ_INVALID;
        }
    
    if (!is_write)
        std::memcpy(data, &value, sizeof(value));
    return vp::IO_REQ_OK;
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Mailbox(config);
}
