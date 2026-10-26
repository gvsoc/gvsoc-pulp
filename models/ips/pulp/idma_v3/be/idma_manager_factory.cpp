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

#include "idma_manager_factory.hpp"
#include "idma_axi_read.hpp"
#include "idma_axi_write.hpp"
#include "idma_obi_read.hpp"
#include "idma_obi_write.hpp"



std::unique_ptr<IdmaReadManager> idma_new_read_manager(vp::Component *top, IdmaBackend *be,
    const IdmaBackendParams &params, const IdmaPortConfig &cfg)
{
    switch (cfg.protocol)
    {
        case IDMA_PROT_AXI:
            return std::make_unique<IdmaAxiRead>(top, "axi_read", be, params, cfg.axi);
        case IDMA_PROT_OBI:
            return std::make_unique<IdmaObiRead>(top, "obi_read", be, params.width, cfg.obi);
    }
    top->get_trace()->fatal("idma_v3: unsupported read protocol %ld (0 AXI, 1 OBI)\n",
        cfg.protocol);
    return nullptr;
}



std::unique_ptr<IdmaWriteManager> idma_new_write_manager(vp::Component *top, IdmaBackend *be,
    const IdmaBackendParams &params, const IdmaPortConfig &cfg)
{
    switch (cfg.protocol)
    {
        case IDMA_PROT_AXI:
            return std::make_unique<IdmaAxiWrite>(top, "axi_write", be, params, cfg.axi);
        case IDMA_PROT_OBI:
            return std::make_unique<IdmaObiWrite>(top, "obi_write", be, params.width, cfg.obi);
    }
    top->get_trace()->fatal("idma_v3: unsupported write protocol %ld (0 AXI, 1 OBI)\n",
        cfg.protocol);
    return nullptr;
}
