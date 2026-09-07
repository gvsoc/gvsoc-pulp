/*
 * Copyright (C) 2026 ETH Zurich and University of Bologna
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

/*
 * Authors: Germain Haugou (germain.haugou@gmail.com)
 */

#include "idma_obi_write.hpp"

// Words alive at once: one being granted, one awaiting its response, plus
// margin
#define NB_WORDS 4
// The RTL aw FIFO in front of the OBI write manager
#define AW_FIFO_DEPTH 2



IdmaObiWrite::IdmaObiWrite(vp::Component *top, std::string name, IdmaBackend *be,
    IdmaObiPortGroup *ports)
:   vp::Block(top, name),
    be(be),
    ports(ports),
    width(ports->access_width()),
    aw_fifo(AW_FIFO_DEPTH)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->words.resize(NB_WORDS);
    for (Word &word: this->words)
    {
        word.data.resize(this->width);
    }

    ports->add_client(this);
}



void IdmaObiWrite::reset(bool active)
{
    if (active)
    {
        this->aw_fifo.reset();
        for (Word &word: this->words)
        {
            word.in_use = false;
        }
        this->nb_in_use = 0;
        this->last_issue_cycle = -1;
    }
}



uint32_t IdmaObiWrite::bytes_to_page_boundary(uint64_t addr)
{
    return this->width - (addr & (this->width - 1));
}



bool IdmaObiWrite::aw_ready()
{
    return this->aw_fifo.can_push(this->be->cycles());
}



void IdmaObiWrite::issue_aw(const IdmaSplit &split)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE, "Queueing write word (addr: 0x%lx, size: %d)\n",
        split.addr, split.num_bytes);
    this->aw_fifo.push(split, this->be->cycles());
}



// idma_obi_write ready_to_write: address (the aw FIFO head, visible one
// cycle after its split) and data (every strobed byte visible in the buffer)
// go out together, one word per cycle. The port group asks this when it
// arbitrates and calls obi_issue_word() when we win.
bool IdmaObiWrite::obi_has_word()
{
    int64_t now = this->be->cycles();

    if (!this->aw_fifo.head_visible(now) || this->nb_in_use >= NB_WORDS
        || this->last_issue_cycle == now)
    {
        return false;
    }

    IdmaSplit *split;
    int beat_idx;
    uint64_t mask;
    return this->be->write_beat_ready(this, &split, &beat_idx, &mask, now);
}



void IdmaObiWrite::obi_issue_word()
{
    int64_t now = this->be->cycles();

    Word *word = nullptr;
    for (Word &w: this->words)
    {
        if (!w.in_use)
        {
            word = &w;
            break;
        }
    }
    word->in_use = true;
    this->nb_in_use++;

    IdmaSplit *split;
    int beat_idx;
    uint64_t mask;
    this->be->write_beat_ready(this, &split, &beat_idx, &mask, now);
    uint64_t addr = split->addr;

    // Address and data together: the word leaves the buffer as it is requested
    this->be->write_beat_take(this, word->data.data(), now);
    this->aw_fifo.pop(now);
    this->last_issue_cycle = now;

    int first = __builtin_ctzll(mask);
    int count = __builtin_popcountll(mask);
    uint64_t word_addr = (addr & ~(uint64_t)(this->width - 1)) + first;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Issuing write word (addr: 0x%lx, size: %d)\n",
        word_addr, count);

    this->ports->send(this, word, word_addr, word->data.data() + first, count, true);
}



// The write response: OBI has no B channel, rvalid of the write access
// (one cycle after the grant) retires the burst in the back-end.
void IdmaObiWrite::obi_word_done(void *token)
{
    Word *word = (Word *)token;
    word->in_use = false;
    this->nb_in_use--;

    // rvalid is the write response
    this->be->write_burst_done(this, false);
}



bool IdmaObiWrite::tick(int64_t now)
{
    // The port group decides when the word goes out; tell it a word may be
    // ready and report whether one left this cycle
    if (this->obi_has_word())
    {
        this->ports->update();
    }
    return this->last_issue_cycle == now;
}



bool IdmaObiWrite::busy()
{
    return !this->aw_fifo.empty() || this->nb_in_use != 0;
}
