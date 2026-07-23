/*
 * Copyright (C) 2020 GreenWaves Technologies, SAS, ETH Zurich and
 *                    University of Bologna
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <cinttypes>
#include <new>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <pulp/chips/soft_hier_old/snitch/snitch_cluster/cluster_periph_regfields.h>
#include <pulp/chips/soft_hier_old/snitch/snitch_cluster/cluster_periph_gvsoc.h>


using namespace std::placeholders;


namespace
{

constexpr uint64_t ANNOTATION_COMMAND_OFFSET = 0x1c0;
constexpr uint64_t ANNOTATION_ID_OFFSET      = 0x1c4;
constexpr uint64_t ANNOTATION_LENGTH_OFFSET  = 0x1c8;
constexpr uint64_t ANNOTATION_DATA_OFFSET    = 0x1cc;
constexpr uint64_t ANNOTATION_RESULT_OFFSET  = 0x1d0;
constexpr uint64_t ANNOTATION_LOCK_OFFSET    = 0x1d4;

enum AnnotationCommand : uint32_t
{
    ANNOTATION_REGISTER = 1,
    ANNOTATION_WORDS     = 2,
    ANNOTATION_CLEAR     = 3,
    ANNOTATION_CLEAR_ALL = 4,
    ANNOTATION_START     = 5,
    ANNOTATION_STOP      = 6,
};

struct AnnotationRecord
{
    std::string words;
    bool active = false;
    int64_t start_time_ns = 0;
    uint64_t generation = 0;
};

struct AnnotationAccessContext
{
    uint32_t id = 0;
    uint32_t length = 0;
    uint32_t received = 0;
    uint32_t result = 0;
    uint64_t selected_generation = 0;
    uint64_t transfer_generation = 0;
    bool has_id = false;
    bool has_length = false;
    bool has_selected_generation = false;
    bool transfer_active = false;
    std::string staged_words;

    void cancel_transfer()
    {
        this->length = 0;
        this->received = 0;
        this->has_length = false;
        this->transfer_generation = 0;
        this->transfer_active = false;
        std::string().swap(this->staged_words);
    }

    void clear()
    {
        this->cancel_transfer();
        this->id = 0;
        this->result = 0;
        this->selected_generation = 0;
        this->has_id = false;
        this->has_selected_generation = false;
    }
};

bool annotation_words_are_valid(const std::string &words)
{
    if (words.empty())
    {
        return false;
    }

    size_t index = 0;
    while (index < words.size())
    {
        const uint8_t byte0 = static_cast<uint8_t>(words[index]);
        uint32_t codepoint;
        size_t sequence_length;

        if (byte0 <= 0x7f)
        {
            codepoint = byte0;
            sequence_length = 1;
        }
        else if (byte0 >= 0xc2 && byte0 <= 0xdf)
        {
            codepoint = byte0 & 0x1f;
            sequence_length = 2;
        }
        else if (byte0 >= 0xe0 && byte0 <= 0xef)
        {
            codepoint = byte0 & 0x0f;
            sequence_length = 3;
        }
        else if (byte0 >= 0xf0 && byte0 <= 0xf4)
        {
            codepoint = byte0 & 0x07;
            sequence_length = 4;
        }
        else
        {
            return false;
        }

        if (index + sequence_length > words.size())
        {
            return false;
        }

        for (size_t i = 1; i < sequence_length; ++i)
        {
            const uint8_t continuation = static_cast<uint8_t>(words[index + i]);
            if ((continuation & 0xc0) != 0x80)
            {
                return false;
            }
            codepoint = (codepoint << 6) | (continuation & 0x3f);
        }

        // Reject overlong encodings, UTF-16 surrogates, and values outside Unicode.
        if ((sequence_length == 2 && codepoint < 0x80)
            || (sequence_length == 3 && codepoint < 0x800)
            || (sequence_length == 4 && codepoint < 0x10000)
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)
            || codepoint > 0x10ffff)
        {
            return false;
        }

        // Labels must stay on one trace line and contain no control characters.
        if (codepoint <= 0x1f
            || (codepoint >= 0x7f && codepoint <= 0x9f)
            || codepoint == 0x2028
            || codepoint == 0x2029)
        {
            return false;
        }

        index += sequence_length;
    }

    return true;
}

}


class ClusterRegisters : public vp::Component
{

public:

    ClusterRegisters(vp::ComponentConf &config);

    void reset(bool active);

    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);

    inline uint32_t get_nm_n() { return this->regmap.nm_config.format_n_get(); }
    inline uint32_t get_nm_m() { return this->regmap.nm_config.format_m_get(); }

private:
    static void barrier_sync(vp::Block *__this, bool value, int id);
    static vp::IoReqStatus global_barrier_sync(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);
    static void hbm_preload_done_handler(vp::Block *__this, bool value);
    static void inst_preheat_done_handler(vp::Block *__this, bool value);
    void fetch_start_check();
    void cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write);
    void nm_config_req(uint64_t offset, int size, uint8_t *value, bool is_write);
    void annotation_id_req(AnnotationAccessContext &context, uint32_t id);
    void annotation_length_req(AnnotationAccessContext &context, uint32_t length);
    void annotation_data_req(AnnotationAccessContext &context, uint32_t data);
    void annotation_command_req(AnnotationAccessContext &context, uint32_t command);
    uint64_t annotation_next_generation();

    vp::Trace     trace;

    vp_regmap_cluster_periph regmap;

    vp::IoSlave in;
    uint32_t bootaddr;
    uint32_t status;
    int nb_cores;
    uint32_t cluster_id;
    vp::reg_32 barrier_status;
    uint32_t num_cluster_x;
    uint32_t num_cluster_y;

    std::vector<vp::WireSlave<bool>> barrier_req_itf;
    vp::WireMaster<bool> barrier_ack_itf;

    vp::IoSlave  global_barrier_slave_itf;
    vp::IoMaster global_barrier_master_itf;
    vp::IoReq*   global_barrier_master_req;
    uint32_t     global_barrier_addr;
    uint8_t *    global_barrier_buffer;

    vp::WireSlave<bool> hbm_preload_done_itf;
    vp::WireSlave<bool> inst_preheat_done_itf;
    vp::WireMaster<bool> fetch_start_itf;
    uint32_t hbm_preload_done;
    uint32_t inst_preheat_done;
    uint32_t fetch_started;

    std::vector<vp::WireMaster<bool>> external_irq_itf;

    vp::IoReq * global_barrier_query;
    int global_barrier_mutex;

    uint16_t global_sync_enable;
    uint64_t global_sync_timestamp;

    std::unordered_map<uint32_t, AnnotationRecord> annotations;
    std::unordered_map<int, AnnotationAccessContext> annotation_contexts;
    uint64_t annotation_generation = 0;
    bool annotation_lock_held = false;

};

ClusterRegisters::ClusterRegisters(vp::ComponentConf &config)
: vp::Component(config), regmap(*this, "regmap")
{
    this->traces.new_trace("trace", &trace, vp::DEBUG);

    this->in.set_req_meth(&ClusterRegisters::req);
    this->new_slave_port("input", &this->in);

    this->bootaddr = this->get_js_config()->get("boot_addr")->get_int();
    this->nb_cores = this->get_js_config()->get("nb_cores")->get_int();
    this->cluster_id = this->get_js_config()->get("cluster_id")->get_int();
    this->num_cluster_x = this->get_js_config()->get("num_cluster_x")->get_int();
    this->num_cluster_y = this->get_js_config()->get("num_cluster_y")->get_int();

    this->global_barrier_slave_itf.set_req_meth(&ClusterRegisters::global_barrier_sync);
    this->new_slave_port("global_barrier_slave", &this->global_barrier_slave_itf);
    this->new_master_port("global_barrier_master", &this->global_barrier_master_itf);
    this->global_barrier_master_req = this->global_barrier_master_itf.req_new(0, 0, 0, 0);
    this->global_barrier_addr = this->get_js_config()->get("global_barrier_addr")->get_int();
    this->global_barrier_buffer = new uint8_t[4];
    this->global_barrier_master_itf.set_resp_meth(&ClusterRegisters::response);
    this->global_barrier_master_itf.set_grant_meth(&ClusterRegisters::grant);

    this->global_barrier_query = NULL;
    this->global_barrier_mutex = 0;

    this->global_sync_enable = 0;
    this->global_sync_timestamp = 0;

    this->barrier_req_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->barrier_req_itf[i].set_sync_meth_muxed(&ClusterRegisters::barrier_sync, i);
        this->new_slave_port("barrier_req_" + std::to_string(i), &this->barrier_req_itf[i]);
    }

    this->external_irq_itf.resize(this->nb_cores);
    for (int i=0; i<this->nb_cores; i++)
    {
        this->new_master_port("external_irq_" + std::to_string(i), &this->external_irq_itf[i]);
    }

    this->new_master_port("barrier_ack", &this->barrier_ack_itf);

    this->new_slave_port("hbm_preload_done", &this->hbm_preload_done_itf);
    this->new_slave_port("inst_preheat_done", &this->inst_preheat_done_itf);
    this->new_master_port("fetch_start", &this->fetch_start_itf);
    this->hbm_preload_done_itf.set_sync_meth(&ClusterRegisters::hbm_preload_done_handler);
    this->inst_preheat_done_itf.set_sync_meth(&ClusterRegisters::inst_preheat_done_handler);
    this->hbm_preload_done = 0;
    this->inst_preheat_done = 0;
    this->fetch_started = 0;

    this->regmap.build(this, &this->trace, "regmap");
    this->regmap.cl_clint_set.register_callback(std::bind(&ClusterRegisters::cl_clint_set_req, this, _1, _2, _3, _4));
    this->regmap.cl_clint_clear.register_callback(std::bind(&ClusterRegisters::cl_clint_clear_req, this, _1, _2, _3, _4));
    this->regmap.nm_config.register_callback(std::bind(&ClusterRegisters::nm_config_req, this, _1, _2, _3, _4));
}

vp::IoReqStatus ClusterRegisters::req(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    uint64_t offset = req->get_addr();
    bool is_write = req->get_is_write();
    uint64_t size = req->get_size();
    uint32_t *data = (uint32_t *) req->get_data();

    // _this->trace.msg("Received IO req (offset: 0x%llx, size: 0x%llx, is_write: %d) -- Cluster ID: %0d\n", offset, size, is_write, _this->cluster_id);

    if (offset == ANNOTATION_COMMAND_OFFSET
        || offset == ANNOTATION_ID_OFFSET
        || offset == ANNOTATION_LENGTH_OFFSET
        || offset == ANNOTATION_DATA_OFFSET
        || offset == ANNOTATION_RESULT_OFFSET
        || offset == ANNOTATION_LOCK_OFFSET)
    {
        if (size != sizeof(uint32_t))
        {
            return vp::IO_REQ_INVALID;
        }

        if (offset == ANNOTATION_LOCK_OFFSET)
        {
            if (is_write)
            {
                // The release value is intentionally ignored. Any 32-bit write
                // releases the per-cluster annotation protocol lock.
                _this->annotation_lock_held = false;
            }
            else
            {
                // GVSoC dispatches component requests serially, so testing and
                // setting the flag in this callback is one atomic operation.
                data[0] = _this->annotation_lock_held ? 0 : 1;
                if (!_this->annotation_lock_held)
                {
                    _this->annotation_lock_held = true;
                }
            }
            return vp::IO_REQ_OK;
        }

        const int initiator = req->get_initiator();

        if (!is_write)
        {
            if (offset == ANNOTATION_RESULT_OFFSET)
            {
                auto context = _this->annotation_contexts.find(initiator);
                data[0] = context == _this->annotation_contexts.end() ? 0 : context->second.result;
            }
            else
            {
                data[0] = 0;
            }
            return vp::IO_REQ_OK;
        }

        AnnotationAccessContext &context = _this->annotation_contexts[initiator];
        const uint32_t value = data[0];

        switch (offset)
        {
            case ANNOTATION_COMMAND_OFFSET:
                _this->annotation_command_req(context, value);
                break;

            case ANNOTATION_ID_OFFSET:
                _this->annotation_id_req(context, value);
                break;

            case ANNOTATION_LENGTH_OFFSET:
                _this->annotation_length_req(context, value);
                break;

            case ANNOTATION_DATA_OFFSET:
                _this->annotation_data_req(context, value);
                break;

            case ANNOTATION_RESULT_OFFSET:
                context.result = 0;
                break;
        }

        return vp::IO_REQ_OK;
    }

    if (is_write && offset == 0 && _this->global_barrier_query == NULL)
    {
        if (_this->global_barrier_mutex == 1)
        {
            _this->global_barrier_mutex = 0;
            return vp::IO_REQ_OK;
        }else{
            _this->global_barrier_query = req;
            return vp::IO_REQ_PENDING;
        }
    }

    if (!is_write && offset == 0)
    {
        data[0] = _this->cluster_id;
    }

    if(offset == 4){
        data[0] = 1;
    }

    if(offset == 8){
        data[0] = _this->num_cluster_x * _this->num_cluster_y;
    }

    if(offset == 12){
        data[0] = _this->num_cluster_x;
    }

    if(offset == 16){
        data[0] = _this->num_cluster_y;
    }

    if(is_write && offset == 20){
        if (_this->global_sync_enable == 0)
        {
            _this->global_sync_enable = 1;
            _this->global_sync_timestamp = _this->time.get_time()/1000;
        } else {
            uint32_t type = data[0];
            _this->global_sync_enable = 0;
            _this->trace.msg("Cluster Sync: %d ns -> %d ns | period = %d ns | Type = %d\n",
                _this->global_sync_timestamp,
                _this->time.get_time()/1000,
                _this->time.get_time()/1000 - _this->global_sync_timestamp,
                type);
        }
    }

    if(offset == 24){
        data[0] = 0;
    }

    if(is_write && offset == 28){
        uint32_t value = *(uint32_t *)data;
        uint8_t row_mask = value & 0xFFFF; // Lower 16 bits
        uint8_t col_mask = value >> 16; // Upper 16 bits

        // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Global Sync] start wakeup with row_mask: %d and col_mask: %d\n", row_mask, col_mask);

        _this->global_barrier_master_req->prepare();
        _this->global_barrier_master_req->set_is_write(true);
        _this->global_barrier_master_req->set_addr(_this->global_barrier_addr);
        _this->global_barrier_master_req->set_size(4);
        _this->global_barrier_master_req->set_data(_this->global_barrier_buffer);

        uint8_t * payload_ptr = _this->global_barrier_master_req->get_payload();
        payload_ptr[0] = 1; //broadcast
        payload_ptr[1] = row_mask;
        payload_ptr[2] = col_mask;

        vp::IoReqStatus status = _this->global_barrier_master_itf.req(_this->global_barrier_master_req);

        if (status == vp::IO_REQ_INVALID)
        {
            _this->trace.fatal("[Global Sync] There was an error while broadcating wakeup signal\n");
        }
    }

    // _this->regmap.access(offset, size, data, is_write);

    return vp::IO_REQ_OK;
}

uint64_t ClusterRegisters::annotation_next_generation()
{
    ++this->annotation_generation;
    if (this->annotation_generation == 0)
    {
        // Generation zero is reserved for contexts which have no selected record.
        ++this->annotation_generation;
    }
    return this->annotation_generation;
}

void ClusterRegisters::annotation_id_req(AnnotationAccessContext &context, uint32_t id)
{
    context.clear();
    context.id = id;
    context.has_id = true;

    auto annotation = this->annotations.find(id);
    if (annotation != this->annotations.end())
    {
        context.selected_generation = annotation->second.generation;
        context.has_selected_generation = true;
    }
}

void ClusterRegisters::annotation_length_req(AnnotationAccessContext &context, uint32_t length)
{
    context.cancel_transfer();
    context.length = length;
    context.has_length = true;
    context.result = 0;
}

void ClusterRegisters::annotation_data_req(AnnotationAccessContext &context, uint32_t data)
{
    if (!context.transfer_active || context.received >= context.length)
    {
        context.cancel_transfer();
        context.result = 0;
        return;
    }

    const uint32_t remaining = context.length - context.received;
    const uint32_t chunk_size = remaining < sizeof(uint32_t) ? remaining : sizeof(uint32_t);

    try
    {
        for (uint32_t i = 0; i < chunk_size; ++i)
        {
            context.staged_words.push_back(
                static_cast<char>((data >> (i * 8)) & 0xff));
        }
    }
    catch (const std::bad_alloc &)
    {
        context.cancel_transfer();
        context.result = 0;
        return;
    }
    catch (const std::length_error &)
    {
        context.cancel_transfer();
        context.result = 0;
        return;
    }

    context.received += chunk_size;

    // Only the final expected data write can mutate the registered annotation.
    if (context.received != context.length)
    {
        return;
    }

    auto annotation = this->annotations.find(context.id);
    const bool can_commit =
        annotation != this->annotations.end()
        && annotation->second.generation == context.transfer_generation
        && !annotation->second.active
        && annotation_words_are_valid(context.staged_words);

    if (can_commit)
    {
        annotation->second.words = std::move(context.staged_words);
        annotation->second.generation = this->annotation_next_generation();
        context.selected_generation = annotation->second.generation;
        context.has_selected_generation = true;
        context.result = 1;
    }
    else
    {
        context.result = 0;
    }

    context.cancel_transfer();
}

void ClusterRegisters::annotation_command_req(
    AnnotationAccessContext &context, uint32_t command)
{
    context.result = 0;

    if (command == ANNOTATION_WORDS)
    {
        // A WORDS command is a preflight. Data is staged afterwards and the
        // final expected data write performs the atomic validation and commit.
        context.received = 0;
        context.transfer_generation = 0;
        context.transfer_active = false;
        std::string().swap(context.staged_words);

        if (!context.has_id || !context.has_length || context.length == 0)
        {
            context.cancel_transfer();
            return;
        }

        auto annotation = this->annotations.find(context.id);
        if (annotation == this->annotations.end() || annotation->second.active)
        {
            context.cancel_transfer();
            return;
        }

        context.transfer_generation = annotation->second.generation;
        context.selected_generation = annotation->second.generation;
        context.has_selected_generation = true;
        context.transfer_active = true;
        context.result = 1;
        return;
    }

    // Any other command aborts an unfinished WORDS transaction for this initiator.
    context.cancel_transfer();

    switch (command)
    {
        case ANNOTATION_REGISTER:
        {
            if (!context.has_id || this->annotations.find(context.id) != this->annotations.end())
            {
                return;
            }

            try
            {
                AnnotationRecord record;
                record.words = "[Annotated " + std::to_string(context.id) + "]";
                record.generation = this->annotation_next_generation();

                auto inserted = this->annotations.emplace(context.id, std::move(record));
                if (!inserted.second)
                {
                    return;
                }

                context.selected_generation = inserted.first->second.generation;
                context.has_selected_generation = true;
                context.result = 1;
            }
            catch (const std::bad_alloc &)
            {
                context.result = 0;
            }
            catch (const std::length_error &)
            {
                context.result = 0;
            }
            break;
        }

        case ANNOTATION_CLEAR:
        {
            if (!context.has_id || !context.has_selected_generation)
            {
                return;
            }

            auto annotation = this->annotations.find(context.id);
            if (annotation == this->annotations.end()
                || annotation->second.generation != context.selected_generation
                || annotation->second.active)
            {
                return;
            }

            this->annotations.erase(annotation);
            context.has_selected_generation = false;
            context.selected_generation = 0;
            context.result = 1;
            break;
        }

        case ANNOTATION_CLEAR_ALL:
        {
            // Clearing all annotations also cancels active intervals and any
            // in-flight label transfers, without producing timing messages.
            std::unordered_map<uint32_t, AnnotationRecord>().swap(this->annotations);
            for (auto &entry : this->annotation_contexts)
            {
                entry.second.clear();
            }
            context.result = 1;
            break;
        }

        case ANNOTATION_START:
        {
            if (!context.has_id || !context.has_selected_generation)
            {
                return;
            }

            auto annotation = this->annotations.find(context.id);
            if (annotation == this->annotations.end()
                || annotation->second.generation != context.selected_generation
                || annotation->second.active)
            {
                return;
            }

            annotation->second.active = true;
            annotation->second.start_time_ns = this->time.get_time() / 1000;
            annotation->second.generation = this->annotation_next_generation();
            context.selected_generation = annotation->second.generation;
            context.result = 1;
            break;
        }

        case ANNOTATION_STOP:
        {
            if (!context.has_id || !context.has_selected_generation)
            {
                return;
            }

            auto annotation = this->annotations.find(context.id);
            if (annotation == this->annotations.end()
                || annotation->second.generation != context.selected_generation
                || !annotation->second.active)
            {
                return;
            }

            const int64_t start_time_ns = annotation->second.start_time_ns;
            const int64_t end_time_ns = this->time.get_time() / 1000;
            const int64_t period_ns = end_time_ns - start_time_ns;

            annotation->second.active = false;
            annotation->second.generation = this->annotation_next_generation();
            context.selected_generation = annotation->second.generation;
            context.result = 1;

            this->trace.msg(
                "%s: %" PRId64 " ns -> %" PRId64 " ns | period = %" PRId64
                " ns | annotation_id = %" PRIu32 "\n",
                annotation->second.words.c_str(),
                start_time_ns,
                end_time_ns,
                period_ns,
                context.id);
            break;
        }

        default:
            break;
    }
}

void ClusterRegisters::hbm_preload_done_handler(vp::Block *__this, bool value)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->hbm_preload_done = 1;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "HBM Preloading Done\n");
    _this->fetch_start_check();
}

void ClusterRegisters::inst_preheat_done_handler(vp::Block *__this, bool value)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->inst_preheat_done = 1;
    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Instruction Preheating Done\n");
    _this->fetch_start_check();
}

void ClusterRegisters::fetch_start_check()
{
    if (this->hbm_preload_done && this->inst_preheat_done)
    {
        if (this->fetch_started == 0)
        {
            this->fetch_started = 1;
            this->fetch_start_itf.sync(1);
        }
    }
}

vp::IoReqStatus ClusterRegisters::global_barrier_sync(vp::Block *__this, vp::IoReq *req)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;

    // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "[Global Sync] Globale Barrier Triggered\n");

    if (_this->global_barrier_query == NULL)
    {
        _this->global_barrier_mutex = 1;
    }else{
        _this->global_barrier_mutex = 0;
        _this->global_barrier_query->get_resp_port()->resp(_this->global_barrier_query);
        _this->global_barrier_query = NULL;
    }

    return vp::IO_REQ_OK;

}

void ClusterRegisters::barrier_sync(vp::Block *__this, bool value, int id)
{
    ClusterRegisters *_this = (ClusterRegisters *)__this;
    _this->barrier_status.set(_this->barrier_status.get() | (value << id));

    // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier sync (id: %d, status: 0x%x)\n", id, _this->barrier_status.get());

    if (_this->barrier_status.get() == (1ULL << _this->nb_cores) - 1)
    {
        // _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Barrier reached\n");

        _this->barrier_status.set(0);
        _this->barrier_ack_itf.sync(1);
    }
}

void ClusterRegisters::reset(bool active)
{
    if (active)
    {
        std::unordered_map<uint32_t, AnnotationRecord>().swap(this->annotations);
        std::unordered_map<int, AnnotationAccessContext>().swap(this->annotation_contexts);
        this->annotation_generation = 0;
        this->annotation_lock_held = false;
    }

    this->new_reg("barrier_status", &this->barrier_status, 0, true);
}


void ClusterRegisters::cl_clint_set_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_set.update(reg_offset, size, value, is_write);
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_set.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(true);
        }
    }
}

void ClusterRegisters::cl_clint_clear_req(uint64_t reg_offset, int size, uint8_t *value, bool is_write)
{
    this->regmap.cl_clint_clear.update(reg_offset, size, value, is_write);
    for (int i=0; i<this->nb_cores; i++)
    {
        int irq_status = (this->regmap.cl_clint_clear.get() >> i) & 1;
        if (irq_status == 1)
        {
            this->external_irq_itf[i].sync(false);
        }
    }
}

void ClusterRegisters::nm_config_req(uint64_t offset, int size, uint8_t *value, bool is_write)
{
    if (is_write)
    {
        // Let the register update its internal value
        this->regmap.nm_config.update(offset, size, value, is_write);

        // Read back the fields using the auto-generated accessors
        uint32_t n = this->regmap.nm_config.format_n_get();
        uint32_t m = this->regmap.nm_config.format_m_get();

        // Trace it
        this->trace.msg(vp::DEBUG, "NM_CONFIG write: N=%u, M=%u\n", n, m);
    }
    else
    {
        // Just forward to the register’s default read behavior
        this->regmap.nm_config.update(offset, size, value, is_write);
    }
}


void ClusterRegisters::response(vp::Block *__this, vp::IoReq *req)
{

}


void ClusterRegisters::grant(vp::Block *__this, vp::IoReq *req)
{

}


extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new ClusterRegisters(config);
}
