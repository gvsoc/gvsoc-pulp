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

#include "idma_obi_read.hpp"

// Words alive at once: one presented, one granted and awaiting its data, one
// held for the buffer, plus margin
#define NB_WORDS 4



IdmaObiRead::IdmaObiRead(vp::Component *top, std::string name, IdmaBackend *be,
    IdmaObiPortGroup *ports)
:   vp::Block(top, name),
    be(be),
    ports(ports),
    width(ports->access_width())
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->words.resize(NB_WORDS);
    for (Word &word: this->words)
    {
        word.data.resize(this->width);
    }

    ports->add_client(this);
}



void IdmaObiRead::reset(bool active)
{
    if (active)
    {
        for (Word &word: this->words)
        {
            word.in_use = false;
        }
        this->held_ar = nullptr;
        this->held_resp.clear();
        this->nb_in_use = 0;
    }
}



uint32_t IdmaObiRead::bytes_to_page_boundary(uint64_t addr)
{
    return this->width - (addr & (this->width - 1));
}



// The OBI address phase as the legalizer sees it: the cluster's rready
// converter allows one transaction outstanding, so a new word can only be
// presented once the previous one was granted (gnt) and its response taken.
bool IdmaObiRead::ar_ready()
{
    // One transaction outstanding on the port: the previous split must have
    // been granted and its data taken before the next is presented
    return this->held_ar == nullptr && this->held_resp.empty()
        && this->nb_in_use < NB_WORDS;
}



void *IdmaObiRead::issue_ar(const IdmaSplit &split)
{
    Word *word = nullptr;
    for (Word &w: this->words)
    {
        if (!w.in_use)
        {
            word = &w;
            break;
        }
    }
    if (word == nullptr)
    {
        this->trace.fatal("No free word slot for a read split\n");
        return nullptr;
    }

    word->in_use = true;
    word->split = split;
    this->nb_in_use++;
    this->held_ar = word;

    this->trace.msg(vp::Trace::LEVEL_TRACE, "Presenting read word (addr: 0x%lx, size: %d)\n",
        split.addr, split.num_bytes);

    // The request goes out in this cycle when the port group is free
    this->ports->update();

    return word;
}



bool IdmaObiRead::obi_has_word()
{
    return this->held_ar != nullptr && this->held_resp.empty();
}



void IdmaObiRead::obi_issue_word()
{
    Word *word = this->held_ar;
    int offset = word->split.addr & (this->width - 1);
    this->ports->send(this, word, word->split.addr, word->data.data() + offset,
        word->split.num_bytes, false);
}



void IdmaObiRead::obi_word_granted(void *token)
{
    // gnt: the address channel is free for the next split next cycle
    this->held_ar = nullptr;
    this->be->wake();
}



// rvalid: the word is valid one cycle after the grant (TCDM latency through
// the port group's done event). Like the R beat of the AXI manager it enters
// the buffer only if its lanes have room, else it is held (rready low) and
// re-offered from the back-end tick, in order.
void IdmaObiRead::obi_word_done(void *token)
{
    Word *word = (Word *)token;
    int64_t now = this->be->cycles();

    if (!this->held_resp.empty() || !this->be->read_beat_can_accept(this, word, now))
    {
        // rready low: hold the word, no further read until it is taken
        this->trace.msg(vp::Trace::LEVEL_TRACE, "Read word held (addr: 0x%lx)\n",
            word->split.addr);
        this->held_resp.push_back(word);
        this->be->wake();
        return;
    }

    this->be->read_beat_accept(this, word, word->data.data(), now);
    word->in_use = false;
    this->nb_in_use--;
}



void IdmaObiRead::retry_held_beat()
{
    if (this->held_resp.empty())
    {
        return;
    }

    // One word per cycle enters the buffer
    Word *word = this->held_resp.front();
    int64_t now = this->be->cycles();
    if (!this->be->read_beat_can_accept(this, word, now))
    {
        return;
    }

    this->held_resp.pop_front();
    this->be->read_beat_accept(this, word, word->data.data(), now);
    word->in_use = false;
    this->nb_in_use--;

    // A split may have been waiting for the port
    this->ports->update();
    this->be->wake();
}



bool IdmaObiRead::busy()
{
    return this->nb_in_use != 0;
}
