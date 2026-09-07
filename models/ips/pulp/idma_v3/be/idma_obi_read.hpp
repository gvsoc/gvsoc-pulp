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

#pragma once

#include <deque>
#include <string>
#include <vector>
#include <vp/vp.hpp>
#include "idma_manager.hpp"
#include "idma_be.hpp"
#include "idma_obi_port_group.hpp"

/**
 * @brief OBI read manager (idma_obi_read behind the cluster's rready
 * converter), a client of an OBI port group.
 *
 * - The protocol does not burst: every split is one bus word, so the
 *   legalizer runs decoupled and emits one read per cycle at most.
 * - A split is presented to the port group in the cycle it is emitted; the
 *   address channel is busy until the group has issued and been granted it
 *   (one transaction outstanding), then the next split may go out the next
 *   cycle, one word per cycle when the TCDM grants.
 * - The word is valid one cycle after the grant (rvalid); it is pushed into
 *   the back-end buffer if the burst heads the read FIFO and the masked
 *   lanes have room, otherwise held (rready low) until the back-end tick
 *   re-offers it, and no further read is issued meanwhile.
 */
class IdmaObiRead : public vp::Block, public IdmaReadManager, public IdmaObiClient
{
public:
    /// @param top   Owning component (the block is created as its child).
    /// @param name  Block name (obi_read_s0, obi_read_s1).
    /// @param be    Back-end the words are pushed into.
    /// @param ports Port group the words are read through (shared with the
    ///              other stream's read manager); registers as its client.
    IdmaObiRead(vp::Component *top, std::string name, IdmaBackend *be, IdmaObiPortGroup *ports);

    /// Hardware reset: drops the presented and held words.
    void reset(bool active) override;

    // IdmaReadManager (see idma_manager.hpp)
    int protocol() override { return IDMA_PROT_OBI; }
    bool not_bursting() override { return true; }
    uint32_t bytes_to_page_boundary(uint64_t addr) override;
    bool ar_ready() override;
    void *issue_ar(const IdmaSplit &split) override;
    void retry_held_beat() override;
    bool busy() override;

    // IdmaObiClient (see idma_obi_port_group.hpp)
    bool obi_has_word() override;
    void obi_issue_word() override;
    void obi_word_granted(void *token) override;
    void obi_word_done(void *token) override;

private:
    /// One read word: presented, then granted, then valid, then taken.
    struct Word
    {
        /// The bus word the port group fills (at the word offset).
        std::vector<uint8_t> data;
        /// The split (one word) it carries.
        IdmaSplit split;
        bool in_use = false;
    };

    IdmaBackend *be;
    IdmaObiPortGroup *ports;
    vp::Trace trace;
    /// Access width of the port group in bytes (the data path width).
    int width;

    /// Word slots (a few, one transaction is outstanding at a time).
    std::vector<Word> words;
    /// Split presented to the group and not yet granted.
    Word *held_ar = nullptr;
    /// Responses held for lack of buffer room, in completion order (a word
    /// issued in the cycle the previous one got held completes behind it).
    std::deque<Word *> held_resp;
    /// Word slots in use.
    int nb_in_use = 0;
};
