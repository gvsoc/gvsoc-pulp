// SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>

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
};
