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

#include "idma_me_nd.hpp"



IdmaMeNd::IdmaMeNd(vp::Component *top, std::string name, int fifo_depth, int nb_dims,
    IdmaFeSource *fe)
:   vp::Block(top, name),
    fe(fe),
    ready_event(this, &IdmaMeNd::ready_handler),
    nb_dims(nb_dims),
    queue(fifo_depth > 0 ? fifo_depth : 1)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
}



void IdmaMeNd::reset(bool active)
{
    if (active)
    {
        // The request being split still heads the queue
        while (!this->queue.empty())
        {
            IdmaNdReq *req = this->queue.head();
            this->queue.pop(0);
            if (req->nb_1d_alive == 0)
            {
                delete req;
            }
        }
        this->queue.reset();
        if (this->pending != nullptr)
        {
            this->pending->parent->nb_1d_alive--;
            delete this->pending;
            this->pending = nullptr;
        }
        this->current = nullptr;
    }
}



void IdmaMeNd::ready_handler(vp::Block *__this, vp::ClockEvent *event)
{
    IdmaMeNd *_this = (IdmaMeNd *)__this;
    _this->fe->me_ready();
}



bool IdmaMeNd::can_accept_nd()
{
    return this->queue.can_push(this->cycles());
}



void IdmaMeNd::push_nd(IdmaNdReq *req, int extra_delay)
{
    int64_t now = this->cycles();

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
        "Queueing ND request (id: %d, src: 0x%lx, dst: 0x%lx, length: 0x%lx, nd: %d, "
        "reps: %ld/%ld, delay: %d)\n", req->id, req->src, req->dst, req->length, req->nd,
        req->reps[0], req->reps[1], extra_delay);

    this->queue.push(req, now, extra_delay);

    if (this->be != nullptr)
    {
        this->be->wake(1 + extra_delay);
    }
}



bool IdmaMeNd::is_busy()
{
    return !this->queue.empty() || this->current != nullptr || this->pending != nullptr;
}



void IdmaMeNd::load(IdmaNdReq *req)
{
    this->current = req;
    this->src = req->src;
    this->dst = req->dst;
    // enable_nd selects how many dimensions are iterated; a repetition count
    // of 0 behaves as 1
    for (int i = 0; i < 2; i++)
    {
        bool enabled = req->nd > i && this->nb_dims > i + 1;
        this->reps[i] = enabled && req->reps[i] != 0 ? req->reps[i] : 1;
        this->idx[i] = 0;
    }
}



// One line of the ND transfer (idma_nd_midend burst generation). The
// addresses advance incrementally: the second strides after every line,
// the third strides instead when the second dimension wraps, so a 3D
// transfer's page k starts where page k-1's last line started plus the
// third stride (this is the RTL behaviour, not base + k * stride_3).
Idma1dReq *IdmaMeNd::build_1d()
{
    IdmaNdReq *req = this->current;

    Idma1dReq *burst = new Idma1dReq();
    burst->src = this->src;
    burst->dst = this->dst;
    burst->length = req->length;
    burst->src_prot = req->src_prot;
    burst->dst_prot = req->dst_prot;
    burst->decouple_rw = req->decouple_rw;
    burst->decouple_aw = req->decouple_aw;
    burst->parent = req;
    req->nb_1d_alive++;

    // Advance the cursors: the second strides after every 1D, the third
    // strides instead on the wrap of the second dimension
    this->idx[0]++;
    if (this->idx[0] < this->reps[0])
    {
        this->src += req->src_stride[0];
        this->dst += req->dst_stride[0];
        burst->super_last = false;
    }
    else
    {
        this->idx[0] = 0;
        this->idx[1]++;
        this->src += req->src_stride[1];
        this->dst += req->dst_stride[1];
        burst->super_last = this->idx[1] >= this->reps[1];
    }

    return burst;
}



// Pull interface for the legalizer: the next 1D request visible this cycle,
// built lazily and kept until take_1d() consumes it (the valid/ready
// handshake of the RTL burst_req channel).
Idma1dReq *IdmaMeNd::peek_1d(int64_t now)
{
    if (this->pending != nullptr)
    {
        return this->pending;
    }

    if (this->current == nullptr)
    {
        if (!this->queue.head_visible(now))
        {
            return nullptr;
        }
        // The request stays at the head of the FIFO until its last 1D is
        // handed over (idma_nd_midend pops on the last burst handshake)
        this->load(this->queue.head());
    }

    this->pending = this->build_1d();

    return this->pending;
}



void IdmaMeNd::take_1d(int64_t now)
{
    this->trace.msg(vp::Trace::LEVEL_TRACE,
        "Handing 1D request (id: %d, src: 0x%lx, dst: 0x%lx, length: 0x%lx, super_last: %d)\n",
        this->pending->parent->id, this->pending->src, this->pending->dst,
        this->pending->length, this->pending->super_last);

    if (this->pending->super_last)
    {
        // Last 1D handed over: the ND request leaves the FIFO now and its
        // slot is usable from next cycle, when a denied launch is retried
        this->queue.pop(now);
        this->current = nullptr;
        this->ready_event.enqueue(1);
    }

    this->pending = nullptr;
}



void IdmaMeNd::complete_1d(Idma1dReq *req, bool force_last)
{
    IdmaNdReq *parent = req->parent;

    if (req->super_last || force_last)
    {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "ND request done (id: %d)\n", parent->id);
        this->fe->complete_nd(parent);
    }

    parent->nb_1d_alive--;
    delete req;

    // The parent is freed once fully split and every child completed
    if (parent->nb_1d_alive == 0 && parent != this->current)
    {
        delete parent;
    }
}
