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

#include <memory>
#include <string>
#include <vp/vp.hpp>
#include <ips/pulp/idma_v3/protocol_config/idma_port_config.hpp>
#include "idma_manager.hpp"
#include "idma_be.hpp"

/**
 * @brief Build the read manager of a side whose protocol is configurable
 *
 * AXI: the master axi_read. OBI: the ports obi_read_<i>, through a port group
 * the manager owns. The caller registers it into the back-end.
 */
std::unique_ptr<IdmaReadManager> idma_new_read_manager(vp::Component *top, IdmaBackend *be,
    const IdmaBackendParams &params, const IdmaPortConfig &cfg);

/**
 * @brief Build the write manager of a side whose protocol is configurable
 *
 * AXI: the master axi_write. OBI: the ports obi_write_<i>.
 */
std::unique_ptr<IdmaWriteManager> idma_new_write_manager(vp::Component *top, IdmaBackend *be,
    const IdmaBackendParams &params, const IdmaPortConfig &cfg);
