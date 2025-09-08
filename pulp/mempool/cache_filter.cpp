/*
 * Copyright (C) 2025 ETH Zurich and University of Bologna
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
 * Authors: Yinrong Li, ETH Zurich (yinrli@student.ethz.ch)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <cpu/iss/include/offload.hpp>
#include <vector>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <memory>

class CacheFilter : public vp::Component
{

public:
    CacheFilter(vp::ComponentConf &config);

private:
    struct CacheRule { uint64_t start, end; };
    struct RuleSnapshot { std::vector<CacheRule> intervals; std::vector<uint64_t> max_hi_prefix; };
    void add_rule(uint64_t start, uint64_t end);
    void update_rule(size_t idx, uint64_t start, uint64_t end);
    void get_rule(size_t idx, uint64_t &start, uint64_t &end);
    bool match(uint64_t addr, uint64_t size);
    void build_snapshot_unlocked();
    mutable std::mutex mu_;
    std::vector<CacheRule> rules_;
    std::shared_ptr<RuleSnapshot> snapshot_{nullptr};

    static void config_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn);
    static vp::IoReqStatus req(vp::Block *__this, vp::IoReq *req);
    static void grant(vp::Block *__this, vp::IoReq *req);
    static void response(vp::Block *__this, vp::IoReq *req);

    vp::Trace trace;
    vp::WireSlave<IssOffloadInsn<uint32_t> *> config_itf;
    vp::IoSlave input_itf;
    vp::IoMaster cache_itf;
    vp::IoMaster bypass_itf;

    bool bypass;
    int cache_latency;
};



CacheFilter::CacheFilter(vp::ComponentConf &config)
    : vp::Component(config)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);
    this->config_itf.set_sync_meth(&CacheFilter::config_sync);
    this->input_itf.set_req_meth(&CacheFilter::req);
    this->cache_itf.set_resp_meth(&CacheFilter::response);
    this->cache_itf.set_grant_meth(&CacheFilter::grant);
    this->bypass_itf.set_resp_meth(&CacheFilter::response);
    this->bypass_itf.set_grant_meth(&CacheFilter::grant);

    this->new_slave_port("config", &this->config_itf);
    this->new_slave_port("input", &this->input_itf);
    this->new_master_port("cache", &this->cache_itf);
    this->new_master_port("bypass", &this->bypass_itf);

    bypass = get_js_config()->get_child_bool("bypass");
    cache_latency = get_js_config()->get_child_int("cache_latency");
    js::Config *cache_rules_config = get_js_config()->get("cache_rules");
    if (cache_rules_config != NULL)
    {
        for (auto rule : cache_rules_config->get_elems())
        {
            uint64_t start = rule->get_elem(0)->get_uint();
            uint64_t end = rule->get_elem(1)->get_uint();
            add_rule(start, end);
        }
    }
    else
    {
        this->trace.msg("CacheFilter: no cache rules defined\n");
    }
}

void CacheFilter::add_rule(uint64_t start, uint64_t end)
{
    if (start >= end)
    {
        this->trace.fatal("CacheFilter: invalid cache rule [0x%lx, 0x%lx)\n", start, end);
        return;
    }
    std::lock_guard<std::mutex> g(mu_);
    rules_.push_back({start, end});
    build_snapshot_unlocked();
}

void CacheFilter::update_rule(size_t idx, uint64_t start, uint64_t end)
{
    if (start >= end)
    {
        this->trace.fatal("CacheFilter: invalid cache rule [0x%lx, 0x%lx)\n", start, end);
        return;
    }
    std::lock_guard<std::mutex> g(mu_);
    if (idx >= rules_.size())
    {
        this->trace.fatal("CacheFilter: invalid rule index %zu\n", idx);
        return;
    }
    rules_[idx] = {start, end};
    build_snapshot_unlocked();
}

void CacheFilter::get_rule(size_t idx, uint64_t &start, uint64_t &end)
{
    std::lock_guard<std::mutex> g(mu_);
    if (idx >= rules_.size())
    {
        this->trace.fatal("CacheFilter: invalid rule index %zu\n", idx);
        return;
    }
    start = rules_[idx].start;
    end = rules_[idx].end;
}

bool CacheFilter::match(uint64_t addr, uint64_t size)
{
    if (size == 0) return false;
    uint64_t end = addr + size;
    if (end < addr) return false;

    auto snap = std::atomic_load_explicit(&snapshot_, std::memory_order_acquire);
    if (!snap || snap->intervals.empty()) return false;

    const auto &ivs = snap->intervals;
    auto it = std::upper_bound(ivs.begin(), ivs.end(), end - 1,
                                [](uint64_t value, const CacheRule &r){ return value < r.start; });
    if (it == ivs.begin()) return false;
    size_t j = static_cast<size_t>(std::distance(ivs.begin(), it) - 1);
    return snap->max_hi_prefix[j] > addr;
}

void CacheFilter::build_snapshot_unlocked()
{
    std::vector<CacheRule> ivs = rules_;
    std::sort(ivs.begin(), ivs.end(), [](const CacheRule &a, const CacheRule &b){
        if (a.start != b.start) return a.start < b.start;
        return a.end < b.end;
    });

    ivs.erase(std::remove_if(ivs.begin(), ivs.end(),
                [](const CacheRule &r){ return r.end <= r.start; }), ivs.end());

    std::vector<uint64_t> prefix;
    prefix.reserve(ivs.size());
    uint64_t cur = 0;
    for (size_t i = 0; i < ivs.size(); ++i) {
        cur = (i == 0) ? ivs[i].end : std::max(cur, ivs[i].end);
        prefix.push_back(cur);
    }

    auto snap = std::make_shared<RuleSnapshot>();
    snap->intervals     = std::move(ivs);
    snap->max_hi_prefix = std::move(prefix);
    std::atomic_store_explicit(&snapshot_, snap, std::memory_order_release);
}

void CacheFilter::config_sync(vp::Block *__this, IssOffloadInsn<uint32_t> *insn)
{
    CacheFilter *_this = (CacheFilter *)__this;
    _this->trace.msg("CacheFilter: config sync opcode=0x%x arg_a=%u arg_b=%u arg_c=%u\n",
                    insn->opcode, insn->arg_a, insn->arg_b, insn->arg_c);
    uint64_t start, end;
    _this->get_rule(insn->arg_a, start, end);
    if (insn->arg_b == 0)
    {
        start = insn->arg_c;
    }
    else
    {
        end = insn->arg_c;
    }
    _this->update_rule(insn->arg_a, start, end);
}

vp::IoReqStatus CacheFilter::req(vp::Block *__this, vp::IoReq *req)
{
    CacheFilter *_this = (CacheFilter *)__this;

    if (_this->bypass || !_this->match(req->get_addr(), req->get_size()))
    {
        _this->trace.msg("CacheFilter: bypassing req addr=0x%lx size=%lu\n", req->get_addr(), req->get_size());
        return _this->bypass_itf.req_forward(req);
    }
    else
    {
        _this->trace.msg("CacheFilter: caching req addr=0x%lx size=%lu\n", req->get_addr(), req->get_size());
        req->inc_latency(_this->cache_latency);
        return _this->cache_itf.req_forward(req);
    }
}

void CacheFilter::grant(vp::Block *__this, vp::IoReq *req)
{

}

void CacheFilter::response(vp::Block *__this, vp::IoReq *req)
{
}

extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new CacheFilter(config);
}