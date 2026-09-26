// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

// Explicit private sideband on the bridge -> SoftHier NI binding. The request
// identity prevents metadata from being applied to unrelated native v2 traffic.
struct SoftHierCollective
{
    void *request = nullptr;
    uint8_t type = 0;
    uint16_t row_mask = 0;
    uint16_t col_mask = 0;
};

// The two IO protocols define different vp::IoReq classes. Keep their faces
// in separate components/translation units and exchange only this plain data.
// The sender owns the access and its buffer until the completion wire fires.
struct SoftHierNocAccess
{
    uint64_t addr;
    uint64_t size;
    uint8_t *data;
    bool write;
    bool error = false;
    uint64_t latency = 0;
    void *owner = nullptr;
    SoftHierCollective collective;
};
