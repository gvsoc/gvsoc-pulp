// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/iss.hpp>

SpatzEvents::SpatzEvents(Iss &iss)
: Events(iss), release_event(&iss, &SpatzEvents::release_handler)
{
}

void SpatzEvents::reset(bool active)
{
    Events::reset(active);
    if (active)
    {
        this->pending.clear();
        this->last_done = -1;
        this->div_latency = 0;
    }
}

int SpatzEvents::serdiv_iterations(int width, bool is_signed, uint64_t a, uint64_t b)
{
    // spatz_serdiv.sv: the operands are aligned on their leading one (the
    // leading zero of the inverted value for negative signed operands), the
    // counter is loaded with lzc(b) - lzc(a), a zero divisor counts as the
    // full width, and a negative count means the quotient is zero and the
    // division terminates at once.
    uint64_t width_mask = width == 64 ? ~(uint64_t)0 : (((uint64_t)1 << width) - 1);
    a &= width_mask;
    b &= width_mask;
    bool a_neg = is_signed && ((a >> (width - 1)) & 1);
    bool b_neg = is_signed && ((b >> (width - 1)) & 1);
    uint64_t lzc_a_in = (a_neg ? ((~a << 1) & width_mask) : a);
    uint64_t lzc_b_in = (b_neg ? (~b & width_mask) : b);

    auto lzc = [width](uint64_t v) -> int {
        return v == 0 ? width : __builtin_clzll(v) - (64 - width);
    };

    int shift_a = lzc(lzc_a_in);
    int div_shift = lzc_b_in == 0 ? width : lzc(lzc_b_in) - shift_a;
    return div_shift < 0 ? -1 : div_shift;
}

void SpatzEvents::schedule_release()
{
    if (this->pending.empty() || this->release_event.is_enqueued())
    {
        return;
    }
    int64_t delay = this->pending.front().done - this->iss.clock.get_cycles();
    this->release_event.enqueue(delay > 0 ? delay : 1);
}

void SpatzEvents::release_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Iss *iss = (Iss *)__this;
    SpatzEvents *_this = &iss->timing;
    int64_t now = iss->clock.get_cycles();

    while (!_this->pending.empty() && _this->pending.front().done <= now)
    {
        _this->iss.regfile.sb_reg_invalid_clear_mask(_this->pending.front().reg_mask);
        _this->pending.pop_front();
    }
    _this->schedule_release();
}
