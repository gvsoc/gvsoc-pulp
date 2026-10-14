// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)
//
// GAP9 cluster DMA (mchan v7), io_v2 port of the legacy
// `pulp.mchan.mchan_v7_impl` io-protocol model.
//
// One register channel per cluster PE plus one for the FC (the last channel).
// Reading CMD allocates a transfer id (TID), writing CMD pushes the command
// word sequence (see archi/mchan_v7.h). Commands move from the per-channel
// queues to a global read (ext->loc) or write (loc->ext) queue, then execute:
//
//   * ext side: big-packet io_v2 bursts of up to `max_burst_length` bytes on
//     the `ext_itf` master (-> cluster soc_ico -> SoC AXI path). DONE /
//     GRANTED / DENIED are all handled; a denied burst is held and re-issued
//     synchronously from the retry() callback.
//   * loc side: `nb_loc_ports` word-wide masters bound to dedicated TCDM
//     crossbar inputs (one beat per port per cycle). The first half of the
//     ports drains ext read data into the TCDM, the second half fetches TCDM
//     data for ext writes. A beat denied by the crossbar (bank conflict with
//     a PE) is held and re-issued synchronously from retry(), so the DMA
//     competes for banks like any other master.
//
// Completion: when all bytes of a TID have been transferred, the initiating
// channel's event line (and irq line if requested) is pulsed -- bound to the
// cluster event unit DMA events -- or broadcast to all channels. The FC
// channel pulses `ext_irq_itf` instead (-> FC ITC). Software frees the TID by
// writing its bit to STATUS.

#include <vp/vp.hpp>
#include <vp/itf/io_v2.hpp>
#include <vp/itf/wire.hpp>
#include <stdio.h>
#include <string.h>
#include <queue>
#include <vector>

#include <vp/power/power_table_convert.hpp>
#include <ips/pulp/mchan/mchan/mchan_config.hpp>
#include "archi/mchan_v7.h"

// Number of CMD writes that fully describe a 2D command (1D commands take 3)
#define MAX_CMD_WORDS 5

#define MCHAN_NB_COUNTERS 16

class Mchan;
class MchanChannel;

// A DMA command, built from the sequence of CMD register writes
class MchanCmd
{
public:
    void init() { this->step = 0; }

    // Step in the CMD write sequence (a command is complete after 3 writes,
    // or 5 for an ext 2D transfer)
    int step;

    // Raw content of the command (the sequence of written words)
    uint32_t content[MAX_CMD_WORDS];

    // Unpacked command fields
    int is_2d;
    uint32_t size;              // total transfer size in bytes
    uint32_t size_to_read;      // bytes not yet carved into source bursts/beats
    uint32_t size_to_write;     // bytes not yet landed at the destination
    uint32_t line_size_to_read; // 2D: bytes left in the current line
    uint64_t source;            // current source address
    uint64_t source_chunk;      // 2D: start of the current source line
    uint64_t dest;              // current destination address
    uint64_t dest_chunk;        // 2D: start of the current destination line
    uint32_t stride;            // 2D: line stride on the ext side
    uint32_t length;            // 2D: line length on the ext side
    int loc2ext;                // 1: TCDM -> ext, 0: ext -> TCDM
    int counter_id;             // TID accounting this transfer
    int raise_irq;
    int raise_event;
    int broadcast;

    MchanChannel *channel;      // channel the command arrived on

    void set_next(MchanCmd *next) { this->next = next; }
    MchanCmd *get_next() { return next; }

private:
    MchanCmd *next;
};

// An ext-side burst in flight: the io_v2 request plus the TCDM-side streaming
// state. For an ext read (ext->loc) the data buffer is drained into the TCDM
// by the loc ports once the response arrived; for an ext write (loc->ext) the
// buffer is first filled from the TCDM, then sent.
class MchanExtReq
{
public:
    vp::IoReq req;
    MchanCmd *cmd;
    uint32_t loc_addr;          // TCDM address of the burst
    uint32_t carved;            // bytes carved into loc beats so far
    uint32_t completed;         // bytes whose loc beat completed
    // Cycle at which the burst is really finished. The SoC path may answer
    // DONE inline with a latency annotation (io_v2 big-packet model): the
    // transaction -- including data validity for reads -- only completes
    // that many cycles later.
    int64_t ready_cycle;

    void set_next(MchanExtReq *next) { this->next = next; }
    MchanExtReq *get_next() { return next; }

private:
    MchanExtReq *next;
};

// Linked-list queue for commands / ext requests (same as the v1 model)
template<class T>
class MchanQueue
{
public:
    MchanQueue(int size) : size(size) {}
    void init() { first = NULL; last = NULL; nb_cmd = 0; }
    T *pop();
    T *pop(bool loc2ext);
    void push(T *cmd);
    bool is_full() { return nb_cmd >= size; }
    bool is_empty() { return nb_cmd == 0; }
    T *get_first() { return first; }

private:
    T *first;
    T *last;
    int nb_cmd;
    int size;
};

// One register channel (one per PE, last one for the FC)
class MchanChannel
{
    friend class Mchan;

public:
    MchanChannel(int id, Mchan *top);
    vp::IoReqStatus req(vp::IoReq *req);
    void reset();

protected:
    MchanCmd *pop_cmd(bool read_queue);
    void trigger_event(MchanCmd *cmd);

    int current_counter;

private:
    vp::IoReqStatus handle_queue_req(vp::IoReq *req, bool is_write, uint32_t *value);
    vp::IoReqStatus handle_status_req(vp::IoReq *req, bool is_write, uint32_t *value);
    vp::IoReqStatus handle_queue_write(vp::IoReq *req, uint32_t *value);
    bool check_command(MchanCmd *cmd);
    int unpack_command(MchanCmd *cmd);
    void handle_req(vp::IoReq *req, uint32_t *value);

    int id;
    Mchan *top;
    vp::IoSlave in;
    // Command-queue write parked while the queue is full (GRANTED, resp()'d
    // once a command is popped). At most one: the core is stalled meanwhile.
    vp::IoReq *pending_req;
    int pending_cmd;
    MchanCmd *current_cmd;
    MchanQueue<MchanCmd> *pending_cmds;

    vp::WireMaster<bool> event_itf;
    vp::WireMaster<bool> irq_itf;
};

// One word-wide TCDM port and its in-flight beat
class MchanLocPort
{
public:
    MchanLocPort(Mchan *top, int id);

    vp::IoMaster itf;
    vp::IoReq req;
    // Burst entry the in-flight beat serves; NULL when no beat is in flight
    MchanExtReq *entry;
    int beat_size;
    // Beat denied by the crossbar, held for synchronous re-issue on retry()
    bool denied;
    int64_t ready_cycle;
};

class Mchan : public vp::Component
{
    friend class MchanChannel;
    friend class MchanLocPort;

public:
    Mchan(vp::ComponentConf &config);

    void reset(bool active);
    void start();

protected:
    static vp::IoReqStatus channel_req(vp::Block *__this, vp::IoReq *req, int id);
    // Raise the irq/event GUI pulse traces (1-cycle wide) on transfer end
    void raise_irq_event(bool irq, bool event);
    uint32_t get_status();
    void free_counters(uint32_t counter_mask);
    int alloc_counter(vp::IoReq *req, MchanChannel *channel);
    int do_alloc_counter(MchanChannel *channel);
    MchanCmd *get_command();
    void free_command(MchanCmd *cmd);
    void check_queue();

    int pending_bytes[MCHAN_NB_COUNTERS];
    int nb_core_read_cmd;
    int nb_core_write_cmd;

private:
    static void check_queue_handler(vp::Block *__this, vp::ClockEvent *event);
    static void check_ext_read_handler(vp::Block *__this, vp::ClockEvent *event);
    static void check_ext_write_handler(vp::Block *__this, vp::ClockEvent *event);
    static void check_loc_transfer_handler(vp::Block *__this, vp::ClockEvent *event);
    static void ext_retry(vp::Block *__this, vp::IoRetryChannel channel);
    static vp::IoRespAck ext_response(vp::Block *__this, vp::IoReq *req);
    static void loc_retry(vp::Block *__this, int id, vp::IoRetryChannel channel);
    static vp::IoRespAck loc_resp(vp::Block *__this, vp::IoReq *req, int id);

    void move_to_global_queue(bool read_queue);
    void send_ext_read_burst();
    void prepare_loc_read_burst();
    void send_ext_write_burst(MchanExtReq *entry);
    void handle_ext_read_response(MchanExtReq *entry);
    void handle_ext_write_end(MchanExtReq *entry);
    void do_handle_ext_write_end(MchanExtReq *entry);
    void ext_port_freed(int64_t latency);
    bool ext_can_issue();
    static void ext_write_completion_handler(vp::Block *__this, vp::ClockEvent *event);
    void handle_loc_beat_done(MchanLocPort *port);
    void handle_cmd_termination(MchanCmd *cmd);
    void account_transfered_bytes(MchanCmd *cmd, int bytes);

    // GUI / power signal helpers (back-ported from the legacy model)
    void busy_set(int count);
    void power_check();
    void power_handle_cmd_start(MchanCmd *cmd);
    void power_handle_cmd_end(MchanCmd *cmd);
    static void irq_clear_handler(vp::Block *__this, vp::ClockEvent *event);

    vp::Trace trace;

    int nb_channels;
    int core_queue_depth;
    int global_queue_depth;
    int max_nb_ext_read_req;
    int max_nb_ext_write_req;
    int max_burst_length;
    int nb_loc_ports;
    int tcdm_addr_width;

    int nb_pending_ext_read_req;
    int nb_pending_ext_write_req;
    uint32_t free_counter_mask;
    std::vector<MchanChannel *> channels;
    std::vector<MchanLocPort *> loc_ports;
    // TID-allocation reads parked while all counters are busy
    std::queue<std::pair<vp::IoReq *, MchanChannel *>> alloc_pending_reqs;

    vp::ClockEvent *check_queue_event;
    vp::ClockEvent *check_ext_read_event;
    vp::ClockEvent *check_ext_write_event;
    vp::ClockEvent *check_loc_transfer_event;
    vp::ClockEvent *ext_write_completion_event;

    int sched_core_queue;
    MchanQueue<MchanCmd> *pending_read_cmds;
    MchanQueue<MchanCmd> *pending_write_cmds;
    // Completed ext read bursts whose data is being drained into the TCDM
    MchanQueue<MchanExtReq> *pending_loc_write_reqs;
    // Ext write bursts whose response carried a latency annotation: their
    // completion (byte accounting + end-of-transfer event) only happens once
    // the annotated cycle is reached
    MchanQueue<MchanExtReq> *pending_ext_write_completions;
    MchanCmd *current_ext_read_cmd;
    MchanCmd *current_ext_write_cmd;
    // Free burst pools
    MchanExtReq *first_ext_read_entry;
    MchanExtReq *first_ext_write_entry;
    // Ext write burst being filled from the TCDM (one at a time)
    MchanExtReq *pending_loc_read_req;
    // Ext burst denied by the SoC path, held for synchronous re-issue
    MchanExtReq *denied_ext_entry;
    bool ext_is_stalled;
    // The ext port carries one burst at a time: the next burst only issues
    // once the previous one's annotated completion cycle is reached, so the
    // big-packet latency annotations stay accurate (they assume a master
    // that does not pipeline).
    bool ext_in_flight;
    int64_t ext_next_cycle;
    // TCDM-filled write bursts waiting for the ext port
    MchanQueue<MchanExtReq> *ready_ext_write_bursts;

    MchanCmd *first_command;

    vp::IoMaster ext_itf{&Mchan::ext_retry, &Mchan::ext_response};

    vp::Trace cmd_events[MCHAN_NB_COUNTERS];
    vp::WireMaster<bool> ext_irq_itf;

    // --- GUI signals back-ported from the legacy mchan_v7_impl model ---
    // Registers (vp::Register auto-registers a GUI/VCD event); declaration
    // order must match the constructor init list (-Wreorder).
    vp::Register<uint32_t> busy;
    // Busy wire to the cluster composite (clock-gating tracking)
    vp::WireMaster<bool> busy_itf;
    // Compiled config struct; only carries the power tables, the
    // functional parameters still come from the JSON properties.
    MchanConfig cfg;
    // Last command readied on a channel queue
    vp::Register<uint32_t> source, dest, size, stride, length;
    // Command currently processed on the ext side, split per direction
    vp::Register<uint32_t> ext2loc_source, ext2loc_dest, ext2loc_size, ext2loc_stride, ext2loc_length;
    vp::Register<uint32_t> loc2ext_source, loc2ext_dest, loc2ext_size, loc2ext_stride, loc2ext_length;
    // End-of-transfer irq/event pulses
    vp::Register<uint32_t> irq_trace, event_trace;
    // In-flight ext burst
    vp::Register<uint32_t> ext_burst_addr, ext_burst_size, ext_burst_write;

    // Not in the init list:
    int busy_count;
    // In-flight per-port loc beat
    std::vector<vp::Register<uint32_t>> loc_burst_addr, loc_burst_size, loc_burst_write;
    bool power_pending_ext2loc, power_pending_loc2ext;
    int64_t irq_raise_cyclestamp, event_raise_cyclestamp;
    vp::ClockEvent *irq_clear_event;
    vp::PowerSource background_power, ext2loc_power, loc2ext_power, both_power;
};


template<class T>
T *MchanQueue<T>::pop()
{
    if (!first) return NULL;
    T *cmd = first;
    first = cmd->get_next();
    nb_cmd--;
    return cmd;
}

template<class T>
T *MchanQueue<T>::pop(bool loc2ext)
{
    T *current = first, *prev = NULL;
    while (current && current->loc2ext != loc2ext)
    {
        prev = current;
        current = current->get_next();
    }

    if (!current) return NULL;

    if (prev)
        prev->set_next(current->get_next());
    else
        first = current->get_next();

    if (last == current)
        last = prev;

    nb_cmd--;
    return current;
}

template<class T>
void MchanQueue<T>::push(T *cmd)
{
    if (first)
        last->set_next(cmd);
    else
        first = cmd;
    cmd->set_next(NULL);
    last = cmd;
    nb_cmd++;
}


// ---------------------------------------------------------------------------
// Register channels
// ---------------------------------------------------------------------------

MchanChannel::MchanChannel(int id, Mchan *top)
    : id(id), top(top), in(id, &Mchan::channel_req)
{
    top->new_slave_port("in_" + std::to_string(id), &in);
    pending_cmds = new MchanQueue<MchanCmd>(top->core_queue_depth);

    top->new_master_port("event_itf_" + std::to_string(id), &event_itf);
    top->new_master_port("irq_itf_" + std::to_string(id), &irq_itf);
}

void MchanChannel::reset()
{
    pending_req = NULL;
    pending_cmd = 0;
    current_cmd = NULL;
    pending_cmds->init();
}

// Check if a raw command is complete and unpack it
int MchanChannel::unpack_command(MchanCmd *cmd)
{
    if (cmd->step == 1)
    {
        cmd->size = MCHAN_CMD_CMD_LEN_GET(cmd->content[0]);
        cmd->size_to_read = cmd->size;
        cmd->size_to_write = cmd->size;
        cmd->loc2ext = !MCHAN_CMD_CMD_TYPE_GET(cmd->content[0]);
        cmd->is_2d = MCHAN_CMD_CMD__2D_EXT_GET(cmd->content[0]);
        cmd->raise_irq = MCHAN_CMD_CMD_ILE_GET(cmd->content[0]);
        cmd->raise_event = MCHAN_CMD_CMD_ELE_GET(cmd->content[0]);
        cmd->broadcast = MCHAN_CMD_CMD_BLE_GET(cmd->content[0]);
        cmd->counter_id = current_counter;
    }
    else if (cmd->step == 3)
    {
        if (cmd->loc2ext)
        {
            cmd->source = cmd->content[1];
            cmd->dest = cmd->content[2];
        }
        else
        {
            cmd->dest = cmd->content[1];
            cmd->source = cmd->content[2];
        }

        if (!cmd->is_2d)
        {
            top->source.set(cmd->source);
            top->dest.set(cmd->dest);
            top->size.set(cmd->size);
            top->stride.set(0);
            top->length.set(0);
            top->trace.msg("New 1D command ready (channel: %d, source: 0x%lx, dest: 0x%lx, size: 0x%x, loc2ext: %d, counter: %d)\n",
                id, cmd->source, cmd->dest, cmd->size, cmd->loc2ext, cmd->counter_id);
            goto unpackDone;
        }
    }
    else if (cmd->step == 5)
    {
        cmd->length = cmd->content[3];
        cmd->stride = cmd->content[4];
        cmd->line_size_to_read = cmd->length;
        cmd->source_chunk = cmd->source;
        cmd->dest_chunk = cmd->dest;

        top->source.set(cmd->source);
        top->dest.set(cmd->dest);
        top->size.set(cmd->size);
        top->stride.set(cmd->stride);
        top->length.set(cmd->length);

        top->trace.msg("New 2D command ready (channel: %d, source: 0x%lx, dest: 0x%lx, size: 0x%x, loc2ext: %d, stride: 0x%x, len: 0x%x, counter: %d)\n",
            id, cmd->source, cmd->dest, cmd->size, cmd->loc2ext, cmd->stride, cmd->length, cmd->counter_id);
        goto unpackDone;
    }
    return 0;

unpackDone:
    pending_cmd++;
    cmd->step = 0;
    return 1;
}

MchanCmd *MchanChannel::pop_cmd(bool read_queue)
{
    MchanCmd *cmd = pending_cmds->pop(!read_queue);
    if (cmd == NULL) return NULL;
    pending_cmd--;

    if (cmd->loc2ext)
        top->nb_core_write_cmd--;
    else
        top->nb_core_read_cmd--;

    if (pending_req)
    {
        // Queue has room again: complete the parked CMD write and wake the core
        vp::IoReq *req = pending_req;
        pending_req = NULL;
        handle_req(req, (uint32_t *)req->get_data());
        req->set_resp_status(vp::IO_RESP_OK);
        in.resp(req);
    }

    return cmd;
}

// Called when the command queue has been written: enqueue the command if it
// is now complete
bool MchanChannel::check_command(MchanCmd *cmd)
{
    cmd->channel = this;

    if (!unpack_command(cmd)) return false;

    top->pending_bytes[current_counter] += cmd->size;

    top->trace.msg("Incrementing counter (id: %d, bytes: %d, remaining bytes: %d)\n",
        current_counter, cmd->size, top->pending_bytes[current_counter]);

    uint8_t one = 1;
    this->top->cmd_events[cmd->counter_id].event(&one);
    this->top->busy_set(1);

    pending_cmds->push(cmd);

    if (cmd->loc2ext)
        top->nb_core_write_cmd++;
    else
        top->nb_core_read_cmd++;
    top->check_queue();

    return true;
}

void MchanChannel::handle_req(vp::IoReq *req, uint32_t *value)
{
    if (current_cmd == NULL)
    {
        current_cmd = top->get_command();
        top->trace.msg("Starting new command\n");
    }

    current_cmd->content[current_cmd->step++] = *value;

    if (check_command(current_cmd))
        current_cmd = NULL;
}

vp::IoReqStatus MchanChannel::handle_queue_write(vp::IoReq *req, uint32_t *value)
{
    top->trace.msg("Pushing word to queue (channel: %d, value: 0x%x, pending_cmd: %d)\n",
        this->id, *value, pending_cmd);

    if (pending_cmd == top->core_queue_depth)
    {
        // Core command queue full: park the request, the core stalls until a
        // command is popped to the global queue
        top->trace.msg("Core queue is full, stalling calling core\n");
        pending_req = req;
        return vp::IO_REQ_GRANTED;
    }

    handle_req(req, value);

    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

vp::IoReqStatus MchanChannel::handle_queue_req(vp::IoReq *req, bool is_write, uint32_t *value)
{
    if (is_write)
    {
        return handle_queue_write(req, value);
    }
    else
    {
        int counter = top->alloc_counter(req, this);
        if (counter == -1)
        {
            // No TID available: park the request, the core stalls until one
            // is freed
            return vp::IO_REQ_GRANTED;
        }
        *value = counter;
        req->set_resp_status(vp::IO_RESP_OK);
        return vp::IO_REQ_DONE;
    }
}

vp::IoReqStatus MchanChannel::handle_status_req(vp::IoReq *req, bool is_write, uint32_t *value)
{
    if (is_write)
    {
        top->trace.msg("Freeing counters (mask: 0x%x)\n", *value);
        top->free_counters(*value);
    }
    else
    {
        *value = top->get_status();
        top->trace.msg("Getting status (status: 0x%x)\n", *value);
    }
    req->set_resp_status(vp::IO_RESP_OK);
    return vp::IO_REQ_DONE;
}

vp::IoReqStatus MchanChannel::req(vp::IoReq *req)
{
    uint64_t offset = req->get_addr();
    uint8_t *data = req->get_data();
    uint64_t size = req->get_size();
    bool is_write = req->get_is_write();

    top->trace.msg("Mchan access (channel: %d, offset: 0x%lx, size: 0x%lx, is_write: %d)\n",
        id, offset, size, is_write);

    if (size != 4)
    {
        req->set_resp_status(vp::IO_RESP_INVALID);
        return vp::IO_REQ_DONE;
    }

    if (offset == MCHAN_CMD_OFFSET)
        return handle_queue_req(req, is_write, (uint32_t *)data);
    else if (offset == MCHAN_STATUS_OFFSET)
        return handle_status_req(req, is_write, (uint32_t *)data);

    req->set_resp_status(vp::IO_RESP_INVALID);
    return vp::IO_REQ_DONE;
}

void MchanChannel::trigger_event(MchanCmd *cmd)
{
    if (id == top->nb_channels - 1)
    {
        // FC channel: pulse the external irq line (-> FC ITC)
        top->trace.msg("Raising external irq line (channel: %d)\n", id);
        if (top->ext_irq_itf.is_bound())
            top->ext_irq_itf.sync(true);
    }
    else
    {
        if (cmd->raise_irq)
        {
            top->trace.msg("Raising irq line (channel: %d)\n", id);
            irq_itf.sync(true);
        }

        if (cmd->raise_event)
        {
            top->trace.msg("Raising event line (channel: %d)\n", id);
            event_itf.sync(true);
        }

        top->raise_irq_event(cmd->raise_irq, cmd->raise_event);
    }
}


// ---------------------------------------------------------------------------
// Top model
// ---------------------------------------------------------------------------

MchanLocPort::MchanLocPort(Mchan *top, int id)
    : itf(id, &Mchan::loc_retry, &Mchan::loc_resp)
{
    top->new_master_port("loc_itf_" + std::to_string(id), &itf);
}

Mchan::Mchan(vp::ComponentConf &config)
    : vp::Component(config, this->cfg),
      busy(*this, "busy", 1, true, 0),
      source(*this, "source", 32, true, 0),
      dest(*this, "dest", 32, true, 0),
      size(*this, "size", 32, true, 0),
      stride(*this, "stride", 32, true, 0),
      length(*this, "length", 32, true, 0),
      ext2loc_source(*this, "ext2loc_source", 32),
      ext2loc_dest(*this, "ext2loc_dest", 32),
      ext2loc_size(*this, "ext2loc_size", 32),
      ext2loc_stride(*this, "ext2loc_stride", 32),
      ext2loc_length(*this, "ext2loc_length", 32),
      loc2ext_source(*this, "loc2ext_source", 32),
      loc2ext_dest(*this, "loc2ext_dest", 32),
      loc2ext_size(*this, "loc2ext_size", 32),
      loc2ext_stride(*this, "loc2ext_stride", 32),
      loc2ext_length(*this, "loc2ext_length", 32),
      irq_trace(*this, "irq", 1, true, 0),
      event_trace(*this, "event", 1, true, 0),
      ext_burst_addr(*this, "ext_burst_addr", 32, true, 0),
      ext_burst_size(*this, "ext_burst_size", 32, true, 0),
      ext_burst_write(*this, "ext_burst_write", 1, true, 0)
{
    nb_channels = get_js_config()->get_child_int("nb_channels");
    core_queue_depth = get_js_config()->get_child_int("core_queue_depth");
    global_queue_depth = get_js_config()->get_child_int("global_queue_depth");
    max_nb_ext_read_req = get_js_config()->get_child_int("max_nb_ext_read_req");
    max_nb_ext_write_req = get_js_config()->get_child_int("max_nb_ext_write_req");
    max_burst_length = get_js_config()->get_child_int("max_burst_length");
    nb_loc_ports = get_js_config()->get_child_int("nb_loc_ports");
    tcdm_addr_width = get_js_config()->get_child_int("tcdm_addr_width");

    check_queue_event = event_new(Mchan::check_queue_handler);
    check_ext_read_event = event_new(Mchan::check_ext_read_handler);
    check_ext_write_event = event_new(Mchan::check_ext_write_handler);
    check_loc_transfer_event = event_new(Mchan::check_loc_transfer_handler);

    pending_read_cmds = new MchanQueue<MchanCmd>(global_queue_depth);
    pending_write_cmds = new MchanQueue<MchanCmd>(global_queue_depth);
    pending_loc_write_reqs = new MchanQueue<MchanExtReq>(global_queue_depth);
    pending_ext_write_completions = new MchanQueue<MchanExtReq>(max_nb_ext_write_req);
    ready_ext_write_bursts = new MchanQueue<MchanExtReq>(max_nb_ext_write_req);
    ext_write_completion_event = event_new(Mchan::ext_write_completion_handler);

    first_ext_read_entry = NULL;
    for (int i = 0; i < max_nb_ext_read_req; i++)
    {
        MchanExtReq *entry = new MchanExtReq();
        entry->req.set_data(new uint8_t[max_burst_length]);
        entry->req.set_is_write(false);
        entry->req.initiator = entry;
        entry->set_next(first_ext_read_entry);
        first_ext_read_entry = entry;
    }

    first_ext_write_entry = NULL;
    for (int i = 0; i < max_nb_ext_write_req; i++)
    {
        MchanExtReq *entry = new MchanExtReq();
        entry->req.set_data(new uint8_t[max_burst_length]);
        entry->req.set_is_write(true);
        entry->req.initiator = entry;
        entry->set_next(first_ext_write_entry);
        first_ext_write_entry = entry;
    }

    new_master_port("ext_itf", &ext_itf);

    for (int i = 0; i < nb_loc_ports; i++)
        loc_ports.push_back(new MchanLocPort(this, i));

    traces.new_trace("trace", &this->trace, vp::DEBUG);

    for (int i = 0; i < nb_channels; i++)
        channels.push_back(new MchanChannel(i, this));

    for (int i = 0; i < MCHAN_NB_COUNTERS; i++)
        traces.new_trace_event("channel_" + std::to_string(i), &this->cmd_events[i], 8);

    this->new_master_port("ext_irq_itf", &ext_irq_itf);
    this->new_master_port("busy", &busy_itf);

    irq_clear_event = event_new(Mchan::irq_clear_handler);
    busy_count = 0;
    power_pending_ext2loc = false;
    power_pending_loc2ext = false;
    irq_raise_cyclestamp = 0;
    event_raise_cyclestamp = 0;

    loc_burst_addr.reserve(nb_loc_ports);
    loc_burst_size.reserve(nb_loc_ports);
    loc_burst_write.reserve(nb_loc_ports);
    for (int i = 0; i < nb_loc_ports; i++)
    {
        loc_burst_addr.emplace_back(*this, "loc_burst_addr[" + std::to_string(i) + "]", 32, true, 0);
        loc_burst_size.emplace_back(*this, "loc_burst_size[" + std::to_string(i) + "]", 32, true, 0);
        loc_burst_write.emplace_back(*this, "loc_burst_write[" + std::to_string(i) + "]", 32, true, 0);
    }

    vp::new_power_source_from_config(power, "background", &background_power,
        this->cfg.power.background);
    vp::new_power_source_from_config(power, "ext2loc", &ext2loc_power,
        this->cfg.power.ext2loc);
    vp::new_power_source_from_config(power, "loc2ext", &loc2ext_power,
        this->cfg.power.loc2ext);
    vp::new_power_source_from_config(power, "both", &both_power,
        this->cfg.power.both);

    first_command = NULL;
}

vp::IoReqStatus Mchan::channel_req(vp::Block *__this, vp::IoReq *req, int id)
{
    Mchan *_this = (Mchan *)__this;
    return _this->channels[id]->req(req);
}

uint32_t Mchan::get_status()
{
    uint32_t status = 0;
    for (unsigned int i = 0; i < MCHAN_NB_COUNTERS; i++)
    {
        status |= (pending_bytes[i] != 0) << i;
    }
    status |= ((~free_counter_mask) & ((1 << MCHAN_NB_COUNTERS) - 1)) << 16;
    return status;
}

void Mchan::free_counters(uint32_t counter_mask)
{
    free_counter_mask |= counter_mask & ((1 << MCHAN_NB_COUNTERS) - 1);

    // Now that we freed a counter, wake a core that was waiting for one
    if (!alloc_pending_reqs.empty())
    {
        vp::IoReq *req = alloc_pending_reqs.front().first;
        MchanChannel *channel = alloc_pending_reqs.front().second;
        alloc_pending_reqs.pop();

        *(uint32_t *)req->get_data() = do_alloc_counter(channel);
        req->set_resp_status(vp::IO_RESP_OK);
        channel->in.resp(req);
    }
}

int Mchan::do_alloc_counter(MchanChannel *channel)
{
    for (int i = 0; i < MCHAN_NB_COUNTERS; i++)
    {
        if (free_counter_mask & (1 << i))
        {
            free_counter_mask &= ~(1 << i);
            trace.msg("Allocated counter (counter: %d)\n", i);
            channel->current_counter = i;
            return i;
        }
    }
    return -1;
}

int Mchan::alloc_counter(vp::IoReq *req, MchanChannel *channel)
{
    if (free_counter_mask)
    {
        return do_alloc_counter(channel);
    }
    else
    {
        trace.msg("No more counter, stalling core\n");
        alloc_pending_reqs.push({req, channel});
        return -1;
    }
}

MchanCmd *Mchan::get_command()
{
    MchanCmd *cmd;

    if (first_command == NULL)
    {
        cmd = new MchanCmd();
    }
    else
    {
        cmd = first_command;
        first_command = cmd->get_next();
    }
    cmd->init();
    return cmd;
}

void Mchan::free_command(MchanCmd *cmd)
{
    cmd->set_next(first_command);
    first_command = cmd;
}

void Mchan::move_to_global_queue(bool read_queue)
{
    MchanQueue<MchanCmd> *queue = read_queue ? pending_read_cmds : pending_write_cmds;

    for (int i = 0, j = sched_core_queue; i < nb_channels; i++)
    {
        MchanCmd *cmd = channels[j]->pop_cmd(read_queue);

        if (cmd)
        {
            trace.msg("Moving command from core queue to global queue (channel: %d)\n", j);

            queue->push(cmd);

            sched_core_queue++;
            if (sched_core_queue == nb_channels)
                sched_core_queue = 0;

            break;
        }

        j++;
        if (j == nb_channels)
            j = 0;
    }
}

void Mchan::handle_cmd_termination(MchanCmd *cmd)
{
    power_handle_cmd_end(cmd);
    this->cmd_events[cmd->counter_id].event_highz();
    busy_set(-1);
    free_command(cmd);
}

void Mchan::account_transfered_bytes(MchanCmd *cmd, int bytes)
{
    pending_bytes[cmd->counter_id] -= bytes;

    trace.msg("Decreasing counter (id: %d, bytes: %d, remaining bytes: %d)\n",
        cmd->counter_id, bytes, pending_bytes[cmd->counter_id]);

    if (pending_bytes[cmd->counter_id] < 0)
        this->trace.force_warning("Counter became negative (id: %d, count: %d)\n",
            cmd->counter_id, pending_bytes[cmd->counter_id]);

    // Fire on reaching OR crossing zero: if the byte accounting ever overshoots
    // (counter goes straight to negative without landing on exactly 0), an
    // `== 0` test would silently drop the completion event and the waiter would
    // hang. `<= 0` still fires once per counter epoch in normal operation
    // (it only decrements, and a re-used counter is re-armed positive first).
    if (pending_bytes[cmd->counter_id] <= 0)
    {
        trace.msg("Counter reached zero, raising event\n");

        if (cmd->broadcast)
        {
            for (int i = 0; i < nb_channels; i++)
                channels[i]->trigger_event(cmd);
        }
        else
        {
            cmd->channel->trigger_event(cmd);
        }
    }
}


// ---------------------------------------------------------------------------
// GUI / power signal helpers (back-ported from the legacy model)
// ---------------------------------------------------------------------------

void Mchan::start()
{
    // Background (leakage + always-on dynamic) power; no-op when no power
    // model is configured for this DMA.
    background_power.leakage_power_start();
    background_power.dynamic_power_start();
}

// Reflect "any transfer in flight" on the top-level busy signal
void Mchan::busy_set(int count)
{
    busy_count += count;
    if (busy_count < 0)
        busy_count = 0;
    bool busy_state = busy_count != 0;
    if ((busy.get() != 0) != busy_state && this->busy_itf.is_bound())
    {
        this->busy_itf.sync(busy_state);
    }
    busy.set(busy_state);
}

// Pick the dynamic power state matching the directions currently active
void Mchan::power_check()
{
    ext2loc_power.dynamic_power_stop();
    loc2ext_power.dynamic_power_stop();
    both_power.dynamic_power_stop();

    if (power_pending_loc2ext && power_pending_ext2loc)
        both_power.dynamic_power_start();
    else if (power_pending_ext2loc)
        ext2loc_power.dynamic_power_start();
    else if (power_pending_loc2ext)
        loc2ext_power.dynamic_power_start();
}

// A command started processing on the ext side: publish its descriptor on the
// matching direction signals and start its dynamic power.
void Mchan::power_handle_cmd_start(MchanCmd *cmd)
{
    if (cmd->loc2ext)
    {
        power_pending_loc2ext = true;
        loc2ext_source.set(cmd->source);
        loc2ext_dest.set(cmd->dest);
        loc2ext_size.set(cmd->size);
        if (cmd->is_2d)
        {
            loc2ext_stride.set(cmd->stride);
            loc2ext_length.set(cmd->length);
        }
        else
        {
            loc2ext_stride.release();
            loc2ext_length.release();
        }
    }
    else
    {
        power_pending_ext2loc = true;
        ext2loc_source.set(cmd->source);
        ext2loc_dest.set(cmd->dest);
        ext2loc_size.set(cmd->size);
        if (cmd->is_2d)
        {
            ext2loc_stride.set(cmd->stride);
            ext2loc_length.set(cmd->length);
        }
        else
        {
            ext2loc_stride.release();
            ext2loc_length.release();
        }
    }

    power_check();
}

void Mchan::power_handle_cmd_end(MchanCmd *cmd)
{
    if (cmd->loc2ext)
    {
        power_pending_loc2ext = false;
        loc2ext_source.release();
        loc2ext_dest.release();
        loc2ext_size.release();
        loc2ext_stride.release();
        loc2ext_length.release();
    }
    else
    {
        power_pending_ext2loc = false;
        ext2loc_source.release();
        ext2loc_dest.release();
        ext2loc_size.release();
        ext2loc_stride.release();
        ext2loc_length.release();
    }

    power_check();
}

void Mchan::raise_irq_event(bool irq, bool event)
{
    int64_t cycles = clock.get_cycles();
    if (irq)
    {
        irq_raise_cyclestamp = cycles;
        irq_trace.set(1);
    }
    if (event)
    {
        event_raise_cyclestamp = cycles;
        event_trace.set(1);
    }
    if (irq || event)
        event_enqueue(irq_clear_event, 1);
}

// Lower the irq/event pulse one cycle after it was raised
void Mchan::irq_clear_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;
    if (_this->irq_raise_cyclestamp < _this->clock.get_cycles())
        _this->irq_trace.set(0);
    if (_this->event_raise_cyclestamp < _this->clock.get_cycles())
        _this->event_trace.set(0);
}


// ---------------------------------------------------------------------------
// Ext side (big-packet io_v2 bursts to the SoC)
// ---------------------------------------------------------------------------

// The ext port finished a burst (inline DONE or async resp): it may issue
// again once the annotated latency has elapsed
void Mchan::ext_port_freed(int64_t latency)
{
    ext_in_flight = false;
    ext_next_cycle = clock.get_cycles() + latency;
}

bool Mchan::ext_can_issue()
{
    return !ext_is_stalled && !ext_in_flight && clock.get_cycles() >= ext_next_cycle;
}

// Carve the next read burst out of the current ext->loc command and send it
void Mchan::send_ext_read_burst()
{
    MchanCmd *cmd = current_ext_read_cmd;

    int size = cmd->is_2d ? cmd->line_size_to_read : cmd->size_to_read;
    if (size > max_burst_length)
        size = max_burst_length;

    nb_pending_ext_read_req++;

    MchanExtReq *entry = first_ext_read_entry;
    first_ext_read_entry = entry->get_next();

    trace.msg("Sending read request to external interface (req: %p, addr: 0x%lx, size: 0x%x)\n",
        &entry->req, cmd->source, size);

    entry->req.prepare();
    entry->req.set_addr(cmd->source);
    entry->req.set_size(size);
    entry->cmd = cmd;
    entry->loc_addr = cmd->dest & ((1 << tcdm_addr_width) - 1);
    entry->carved = 0;
    entry->completed = 0;

    ext_burst_addr.set(entry->req.get_addr());
    ext_burst_size.set(size);
    ext_burst_write.set(0);

    cmd->dest += size;
    cmd->source += size;
    cmd->size_to_read -= size;
    if (cmd->is_2d)
    {
        cmd->line_size_to_read -= size;
        if (cmd->line_size_to_read == 0)
        {
            cmd->line_size_to_read = cmd->length;
            cmd->source = cmd->source_chunk + cmd->stride;
            cmd->source_chunk = cmd->source;
        }
    }

    if (cmd->size_to_read == 0)
        current_ext_read_cmd = NULL;

    ext_in_flight = true;

    this->traces.declare_access(entry->req.get_addr(), entry->req.get_size(), false);
    vp::IoReqStatus err = ext_itf.req(&entry->req);
    if (err == vp::IO_REQ_DONE)
    {
        ext_port_freed(entry->req.get_latency());
        handle_ext_read_response(entry);
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        // Held until the SoC path signals retry(); re-issued synchronously
        // from the retry callback
        denied_ext_entry = entry;
        ext_is_stalled = true;
    }
    // GRANTED: response comes through ext_response()
}

// An ext read burst response arrived: queue its data for the TCDM ports. The
// data is only valid `latency` cycles from now (big-packet annotation), so
// the drain must not start before that.
void Mchan::handle_ext_read_response(MchanExtReq *entry)
{
    entry->ready_cycle = clock.get_cycles() + entry->req.get_latency();
    pending_loc_write_reqs->push(entry);
}

// Carve the next write burst out of the current loc->ext command; the loc
// ports will fill its buffer from the TCDM, then it is sent
void Mchan::prepare_loc_read_burst()
{
    MchanCmd *cmd = current_ext_write_cmd;

    int size = cmd->is_2d ? cmd->line_size_to_read : cmd->size_to_read;
    if (size > max_burst_length)
        size = max_burst_length;

    nb_pending_ext_write_req++;

    MchanExtReq *entry = first_ext_write_entry;
    first_ext_write_entry = entry->get_next();

    trace.msg("Preparing write request to external interface (req: %p, addr: 0x%lx, size: 0x%x)\n",
        &entry->req, cmd->dest, size);

    entry->req.prepare();
    entry->req.set_addr(cmd->dest);
    entry->req.set_size(size);
    entry->cmd = cmd;
    entry->loc_addr = cmd->source & ((1 << tcdm_addr_width) - 1);
    entry->carved = 0;
    entry->completed = 0;

    cmd->dest += size;
    cmd->source += size;
    cmd->size_to_read -= size;

    if (cmd->is_2d)
    {
        cmd->line_size_to_read -= size;
        if (cmd->line_size_to_read == 0)
        {
            cmd->line_size_to_read = cmd->length;
            cmd->dest = cmd->dest_chunk + cmd->stride;
            cmd->dest_chunk = cmd->dest;
        }
    }

    pending_loc_read_req = entry;

    if (cmd->size_to_read == 0)
        current_ext_write_cmd = NULL;
}

// Send a TCDM-filled write burst on the ext port (the caller checked the
// port is free)
void Mchan::send_ext_write_burst(MchanExtReq *entry)
{
    trace.msg("Sending write request to external interface (req: %p, addr: 0x%lx, size: 0x%lx)\n",
        &entry->req, entry->req.get_addr(), entry->req.get_size());

    ext_burst_addr.set(entry->req.get_addr());
    ext_burst_size.set(entry->req.get_size());
    ext_burst_write.set(1);

    ext_in_flight = true;

    this->traces.declare_access(entry->req.get_addr(), entry->req.get_size(), true);
    vp::IoReqStatus err = ext_itf.req(&entry->req);
    if (err == vp::IO_REQ_DONE)
    {
        ext_port_freed(entry->req.get_latency());
        handle_ext_write_end(entry);
    }
    else if (err == vp::IO_REQ_DENIED)
    {
        denied_ext_entry = entry;
        ext_is_stalled = true;
    }
    // GRANTED: completion comes through ext_response()
}

// An ext write burst response arrived. The write is only really finished
// `latency` cycles from now (big-packet annotation): defer the completion
// accounting so the end-of-transfer event does not fire early.
void Mchan::handle_ext_write_end(MchanExtReq *entry)
{
    int64_t latency = entry->req.get_latency();

    if (latency == 0)
    {
        do_handle_ext_write_end(entry);
        return;
    }

    entry->ready_cycle = clock.get_cycles() + latency;
    pending_ext_write_completions->push(entry);

    if (!ext_write_completion_event->is_enqueued())
        event_enqueue(ext_write_completion_event, latency);
}

void Mchan::ext_write_completion_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;
    int64_t cycles = _this->clock.get_cycles();

    MchanExtReq *entry;
    while ((entry = _this->pending_ext_write_completions->get_first()) != NULL &&
           entry->ready_cycle <= cycles)
    {
        _this->pending_ext_write_completions->pop();
        _this->do_handle_ext_write_end(entry);
    }

    if (entry != NULL)
        _this->event_enqueue(event, entry->ready_cycle - cycles);

    _this->check_queue();
}

// An ext write burst completed: account it against its command
void Mchan::do_handle_ext_write_end(MchanExtReq *entry)
{
    MchanCmd *cmd = entry->cmd;
    int64_t size = entry->req.get_size();

    cmd->size_to_write -= size;

    trace.msg("Updating command (size_to_write: %d)\n", cmd->size_to_write);

    entry->set_next(first_ext_write_entry);
    first_ext_write_entry = entry;
    nb_pending_ext_write_req--;

    account_transfered_bytes(cmd, size);
    if (cmd->size_to_write == 0)
        handle_cmd_termination(cmd);
}

vp::IoRespAck Mchan::ext_response(vp::Block *__this, vp::IoReq *req)
{
    Mchan *_this = (Mchan *)__this;
    MchanExtReq *entry = (MchanExtReq *)req->initiator;

    _this->trace.msg("Received response (req: %p)\n", req);

    _this->ext_port_freed(req->get_latency());

    if (req->get_is_write())
        _this->handle_ext_write_end(entry);
    else
        _this->handle_ext_read_response(entry);

    _this->check_queue();
    return vp::IO_RESP_ACCEPTED;
}

void Mchan::ext_retry(vp::Block *__this, vp::IoRetryChannel channel)
{
    Mchan *_this = (Mchan *)__this;

    MchanExtReq *entry = _this->denied_ext_entry;
    if (entry == NULL)
    {
        _this->ext_is_stalled = false;
        _this->check_queue();
        return;
    }

    // io_v2 contract: the held request must be re-issued from inside the
    // retry callback (same cycle), not deferred to a later clock event
    vp::IoReqStatus err = _this->ext_itf.req(&entry->req);
    if (err == vp::IO_REQ_DENIED)
        return;

    _this->denied_ext_entry = NULL;
    _this->ext_is_stalled = false;

    if (err == vp::IO_REQ_DONE)
    {
        _this->ext_port_freed(entry->req.get_latency());
        if (entry->req.get_is_write())
            _this->handle_ext_write_end(entry);
        else
            _this->handle_ext_read_response(entry);
    }
    // GRANTED: completion comes through ext_response()

    _this->check_queue();
}


// ---------------------------------------------------------------------------
// Loc side (word-wide beats to the TCDM crossbar)
// ---------------------------------------------------------------------------

// A TCDM beat completed: account it against its burst entry
void Mchan::handle_loc_beat_done(MchanLocPort *port)
{
    MchanExtReq *entry = port->entry;
    int size = port->beat_size;

    port->entry = NULL;
    port->ready_cycle = clock.get_cycles() + port->req.get_latency() + 1;

    entry->completed += size;

    if (port->req.get_is_write())
    {
        // Ext read data landed in the TCDM
        MchanCmd *cmd = entry->cmd;
        cmd->size_to_write -= size;
        trace.msg("Updating command (size_to_write: %d)\n", cmd->size_to_write);
        account_transfered_bytes(cmd, size);
        if (cmd->size_to_write == 0)
            handle_cmd_termination(cmd);

        if (entry->completed == entry->req.get_size())
        {
            // Whole burst drained: return it to the read pool
            entry->set_next(first_ext_read_entry);
            first_ext_read_entry = entry;
            nb_pending_ext_read_req--;
        }
    }
    else
    {
        // TCDM data read for an ext write burst: once full, it waits for the
        // ext port
        if (entry->completed == entry->req.get_size())
        {
            trace.msg("Finished filling write burst\n");
            ready_ext_write_bursts->push(entry);
        }
    }
}

void Mchan::check_loc_transfer_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;
    int64_t cycles = _this->clock.get_cycles();

    // Go through the local ports to see if we can send a beat from the
    // pending bursts
    for (int i = 0; i < _this->nb_loc_ports; i++)
    {
        MchanLocPort *port = _this->loc_ports[i];

        // Bypass this port if a denied beat is held or the port is still
        // busy with a previous beat
        if (port->entry != NULL)
            continue;
        if (port->ready_cycle > cycles)
        {
            _this->trace.msg("Bypassing port (port: %d, ready_cycle: %ld)\n",
                i, port->ready_cycle);
            continue;
        }

        // First half of the ports drains ext read data into the TCDM, second
        // half fetches TCDM data for ext writes
        MchanExtReq *entry;
        bool is_write;

        if (i < (_this->nb_loc_ports / 2))
        {
            entry = _this->pending_loc_write_reqs->get_first();
            // The burst data is only valid once its annotated latency has
            // elapsed (big-packet model)
            if (entry == NULL || entry->ready_cycle > cycles)
                continue;
            is_write = true;
        }
        else
        {
            entry = _this->pending_loc_read_req;
            if (entry == NULL)
                continue;
            is_write = false;
        }

        // Carve the next word-aligned beat out of the burst
        uint32_t carved = entry->carved;
        int32_t left = entry->req.get_size() - carved;
        uint32_t addr = entry->loc_addr + carved;
        uint8_t *data = entry->req.get_data() + carved;
        int32_t size = 4;
        if (addr & 0x3) size -= addr & 0x3;
        if (size > left) size = left;

        entry->carved = carved + size;

        // No more beats to carve: this burst no longer blocks the queue head
        if (entry->carved == entry->req.get_size())
        {
            if (is_write)
                _this->pending_loc_write_reqs->pop();
            else
                _this->pending_loc_read_req = NULL;
        }

        vp::IoReq *req = &port->req;
        if (is_write && (addr >= 0x1000 && addr < 0x1010))
        _this->trace.msg("Sending %s beat to local port (req: %p, port: %d, addr: 0x%x, size: 0x%x)\n",
            is_write ? "write" : "read", req, i, addr, size);
        req->prepare();
        req->set_addr(addr);
        req->set_size(size);
        req->set_is_write(is_write);
        req->set_data(data);

        _this->loc_burst_addr[i].set(addr);
        _this->loc_burst_size[i].set(size);
        _this->loc_burst_write[i].set(is_write);

        port->entry = entry;
        port->beat_size = size;

        _this->traces.declare_access(addr, size, is_write);
        vp::IoReqStatus err = port->itf.req(req);
        if (err == vp::IO_REQ_DONE)
        {
            _this->handle_loc_beat_done(port);
        }
        else if (err == vp::IO_REQ_DENIED)
        {
            // Bank conflict: the beat is held, the crossbar will signal
            // retry() once this port wins its election
            port->denied = true;
        }
        // GRANTED: completion comes through loc_resp()
    }

    _this->check_queue();
}

void Mchan::loc_retry(vp::Block *__this, int id, vp::IoRetryChannel channel)
{
    Mchan *_this = (Mchan *)__this;
    MchanLocPort *port = _this->loc_ports[id];

    if (!port->denied)
        return;

    // io_v2 contract: re-issue the held beat from inside the retry callback
    // (same cycle); the crossbar only forwards inline during its election
    vp::IoReqStatus err = port->itf.req(&port->req);
    if (err == vp::IO_REQ_DENIED)
        return;

    port->denied = false;

    if (err == vp::IO_REQ_DONE)
        _this->handle_loc_beat_done(port);
    // GRANTED: completion comes through loc_resp()

    _this->check_queue();
}

vp::IoRespAck Mchan::loc_resp(vp::Block *__this, vp::IoReq *req, int id)
{
    Mchan *_this = (Mchan *)__this;
    _this->handle_loc_beat_done(_this->loc_ports[id]);
    _this->check_queue();
    return vp::IO_RESP_ACCEPTED;
}


// ---------------------------------------------------------------------------
// Scheduling
// ---------------------------------------------------------------------------

void Mchan::check_queue_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;

    if (_this->nb_core_read_cmd && !_this->pending_read_cmds->is_full())
        _this->move_to_global_queue(true);
    else if (_this->nb_core_write_cmd && !_this->pending_write_cmds->is_full())
        _this->move_to_global_queue(false);

    _this->check_queue();
}

void Mchan::check_ext_read_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;

    if (_this->current_ext_read_cmd == NULL)
    {
        _this->current_ext_read_cmd = _this->pending_read_cmds->pop();
        if (_this->current_ext_read_cmd != NULL)
            _this->power_handle_cmd_start(_this->current_ext_read_cmd);
    }

    if (_this->current_ext_read_cmd != NULL)
    {
        if (_this->nb_pending_ext_read_req < _this->max_nb_ext_read_req &&
            _this->ext_can_issue())
            _this->send_ext_read_burst();
    }

    _this->check_queue();
}

void Mchan::check_ext_write_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Mchan *_this = (Mchan *)__this;

    // Issue a TCDM-filled burst when the ext port is free
    if (_this->ext_can_issue())
    {
        MchanExtReq *ready = _this->ready_ext_write_bursts->get_first();
        if (ready != NULL)
        {
            _this->ready_ext_write_bursts->pop();
            _this->send_ext_write_burst(ready);
        }
    }

    if (_this->current_ext_write_cmd == NULL)
    {
        _this->current_ext_write_cmd = _this->pending_write_cmds->pop();
        if (_this->current_ext_write_cmd != NULL)
            _this->power_handle_cmd_start(_this->current_ext_write_cmd);
    }

    if (_this->current_ext_write_cmd != NULL)
    {
        if (_this->nb_pending_ext_write_req < _this->max_nb_ext_write_req &&
            _this->pending_loc_read_req == NULL)
            _this->prepare_loc_read_burst();
    }

    _this->check_queue();
}

void Mchan::check_queue()
{
    if ((nb_core_read_cmd && !pending_read_cmds->is_full()) ||
        (nb_core_write_cmd && !pending_write_cmds->is_full()))
    {
        if (!check_queue_event->is_enqueued())
            event_enqueue(check_queue_event, 1);
    }

    // The ext port issues one burst at a time; the next issue happens at the
    // previous burst's annotated completion. While a burst is in flight
    // (GRANTED or denied-held), the FSMs are re-armed from the response /
    // retry callbacks instead.
    int64_t now = clock.get_cycles();
    int64_t ext_delay = 1;
    if (ext_next_cycle > now + 1)
        ext_delay = ext_next_cycle - now;

    if ((!pending_read_cmds->is_empty() && current_ext_read_cmd == NULL) ||
        (current_ext_read_cmd != NULL && nb_pending_ext_read_req < max_nb_ext_read_req))
    {
        if (!ext_is_stalled && !ext_in_flight)
        {
            if (!check_ext_read_event->is_enqueued())
                event_enqueue(check_ext_read_event, ext_delay);
        }
    }

    bool write_fill_work =
        (!pending_write_cmds->is_empty() && current_ext_write_cmd == NULL) ||
        (current_ext_write_cmd != NULL && nb_pending_ext_write_req < max_nb_ext_write_req &&
         pending_loc_read_req == NULL);
    bool write_issue_work = !ready_ext_write_bursts->is_empty();

    if ((write_fill_work || write_issue_work) && !ext_is_stalled)
    {
        if (!check_ext_write_event->is_enqueued())
        {
            if (write_fill_work)
                event_enqueue(check_ext_write_event, 1);
            else if (!ext_in_flight)
                event_enqueue(check_ext_write_event, ext_delay);
        }
    }

    if (!pending_loc_write_reqs->is_empty() || pending_loc_read_req != NULL)
    {
        if (!check_loc_transfer_event->is_enqueued())
        {
            // Find the earliest cycle at which a port becomes available
            int64_t min_ready_cycle = -1;
            int64_t cycles = clock.get_cycles();

            for (int i = 0; i < nb_loc_ports; i++)
            {
                // Ports holding a denied beat wake up through retry(), not
                // through the FSM
                if (loc_ports[i]->entry != NULL)
                    continue;

                if (min_ready_cycle == -1 || loc_ports[i]->ready_cycle < min_ready_cycle)
                    min_ready_cycle = loc_ports[i]->ready_cycle;
            }

            // If the only pending work is draining a read burst whose data
            // is not yet valid, don't wake up before it is
            if (pending_loc_read_req == NULL && min_ready_cycle != -1)
            {
                MchanExtReq *head = pending_loc_write_reqs->get_first();
                if (head != NULL && head->ready_cycle > min_ready_cycle)
                    min_ready_cycle = head->ready_cycle;
            }

            if (min_ready_cycle != -1)
            {
                if (min_ready_cycle <= cycles)
                    event_enqueue(check_loc_transfer_event, 1);
                else
                    event_enqueue(check_loc_transfer_event, min_ready_cycle - cycles);
            }
        }
    }
}

void Mchan::reset(bool active)
{
    if (active)
    {
        free_counter_mask = (1 << MCHAN_NB_COUNTERS) - 1;

        for (int i = 0; i < MCHAN_NB_COUNTERS; i++)
        {
            pending_bytes[i] = 0;
        }

        for (int i = 0; i < nb_channels; i++)
        {
            channels[i]->reset();
        }

        for (int i = 0; i < nb_loc_ports; i++)
        {
            loc_ports[i]->entry = NULL;
            loc_ports[i]->denied = false;
            loc_ports[i]->ready_cycle = 0;
        }

        while (!alloc_pending_reqs.empty())
            alloc_pending_reqs.pop();

        nb_core_read_cmd = 0;
        nb_core_write_cmd = 0;
        sched_core_queue = 0;
        nb_pending_ext_read_req = 0;
        nb_pending_ext_write_req = 0;
        pending_read_cmds->init();
        pending_write_cmds->init();
        pending_loc_write_reqs->init();
        pending_ext_write_completions->init();
        current_ext_read_cmd = NULL;
        current_ext_write_cmd = NULL;
        pending_loc_read_req = NULL;
        denied_ext_entry = NULL;
        ext_is_stalled = false;
        ext_in_flight = false;
        ext_next_cycle = 0;
        ready_ext_write_bursts->init();

        for (int i = 0; i < MCHAN_NB_COUNTERS; i++)
        {
            this->cmd_events[i].event_highz();
        }

        busy_count = 0;
        power_pending_ext2loc = false;
        power_pending_loc2ext = false;
        ext2loc_source.release();
        ext2loc_dest.release();
        ext2loc_size.release();
        ext2loc_stride.release();
        ext2loc_length.release();
        loc2ext_source.release();
        loc2ext_dest.release();
        loc2ext_size.release();
        loc2ext_stride.release();
        loc2ext_length.release();
    }
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new Mchan(config);
}
