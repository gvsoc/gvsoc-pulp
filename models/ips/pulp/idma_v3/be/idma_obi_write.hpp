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

#include <string>
#include <vector>
#include <vp/vp.hpp>
#include "idma_manager.hpp"
#include "idma_be.hpp"
#include "idma_fifo.hpp"
#include "idma_obi_port_group.hpp"

/**
 * @brief OBI write manager (idma_obi_write behind the cluster's rready
 * converter), a client of an OBI port group.
 *
 * - The protocol does not burst: every split is one bus word.
 * - The address channel is a registered two-entry FIFO (the RTL aw FIFO): a
 *   split emitted in one cycle can be written from the next.
 * - Address and data go out together: the word is requested once the buffer
 *   holds every byte it strobes, and popped from the buffer on that request.
 *   One transaction outstanding; the response (rvalid, one cycle after the
 *   grant) is the write response that retires the burst in the back-end.
 */
class IdmaObiWrite : public vp::Block, public IdmaWriteManager, public IdmaObiClient
{
public:
    /// @param top   Owning component (the block is created as its child).
    /// @param name  Block name (obi_write).
    /// @param be    Back-end the words are popped from.
    /// @param ports Port group the words are written through; registers as
    ///              its client.
    IdmaObiWrite(vp::Component *top, std::string name, IdmaBackend *be, IdmaObiPortGroup *ports);

    /// Hardware reset: empties the address FIFO and the word slots.
    void reset(bool active) override;

    // IdmaWriteManager (see idma_manager.hpp)
    int protocol() override { return IDMA_PROT_OBI; }
    bool not_bursting() override { return true; }
    uint32_t bytes_to_page_boundary(uint64_t addr) override;
    bool aw_ready() override;
    void issue_aw(const IdmaSplit &split) override;
    bool tick(int64_t now) override;
    bool busy() override;

    // IdmaObiClient (see idma_obi_port_group.hpp)
    bool obi_has_word() override;
    void obi_issue_word() override;
    void obi_word_done(void *token) override;

private:
    /// One write word, from its request to its response.
    struct Word
    {
        /// The bus word popped from the buffer; the request points into it.
        std::vector<uint8_t> data;
        bool in_use = false;
    };

    IdmaBackend *be;
    IdmaObiPortGroup *ports;
    vp::Trace trace;
    /// Access width of the port group in bytes (the data path width).
    int width;

    /// Address FIFO (the split of each pending write), registered, depth 2.
    IdmaTimedFifo<IdmaSplit> aw_fifo;
    /// Word slots (a few, one transaction is outstanding at a time) and the
    /// number in use.
    std::vector<Word> words;
    int nb_in_use = 0;
    /// A word was issued this cycle (one per cycle).
    int64_t last_issue_cycle = -1;
};
