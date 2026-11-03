// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna
//
// SPDX-License-Identifier: Apache-2.0

#include <cpu/iss_v2/include/iss.hpp>

SnitchMempoolEvents::SnitchMempoolEvents(Iss &iss)
: Events(iss), release_event(&iss, &SnitchMempoolEvents::release_handler)
{
}

void SnitchMempoolEvents::reset(bool active)
{
    Events::reset(active);
    if (active)
    {
        this->pending.clear();
        this->last_done = -1;
    }
}

void SnitchMempoolEvents::schedule_release()
{
    if (this->pending.empty() || this->release_event.is_enqueued())
    {
        return;
    }
    int64_t delay = this->pending.front().done - this->iss.clock.get_cycles();
    this->release_event.enqueue(delay > 0 ? delay : 1);
}

void SnitchMempoolEvents::release_handler(vp::Block *__this, vp::ClockEvent *event)
{
    Iss *iss = (Iss *)__this;
    SnitchMempoolEvents *_this = &iss->timing;
    int64_t now = iss->clock.get_cycles();

    while (!_this->pending.empty() && _this->pending.front().done <= now)
    {
        _this->iss.regfile.sb_reg_invalid_clear_mask(_this->pending.front().reg_mask);
        _this->pending.pop_front();
    }
    _this->schedule_release();
}
