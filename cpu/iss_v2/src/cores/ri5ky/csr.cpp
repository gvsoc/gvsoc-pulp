// SPDX-FileCopyrightText: 2026 ETH Zurich, University of Bologna and EssilorLuxottica SAS
//
// SPDX-License-Identifier: Apache-2.0
//
// Authors: Germain Haugou (germain.haugou@gmail.com)

#include <cpu/iss_v2/include/cores/ri5ky/csr.hpp>

Ri5kyCsr::Ri5kyCsr(Iss &iss)
: Csr(iss)
{
    for (int i=0; i<32; i++)
    {
        this->declare_csr(&this->pccr[i], "pccr" + std::to_string(i), 0x780 + i);
        this->pccr[i].register_callback(std::bind(&Ri5kyCsr::pccr_access, this, std::placeholders::_1,
            std::placeholders::_2, std::placeholders::_3, i));

    }
    this->declare_csr(&this->pcer, "pcer", 0xCC0);
    this->pcer.register_callback(std::bind(&Ri5kyCsr::pcer_access, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));

    this->declare_csr(&this->pcmr, "pcmr", 0xCC1);
    this->pcmr.register_callback(std::bind(&Ri5kyCsr::pcmr_access, this, std::placeholders::_1,
        std::placeholders::_2, std::placeholders::_3));
}

bool Ri5kyCsr::pccr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value, int id)
{
    this->iss.timing.flush_cycles();

#if defined(CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR)
    // In case of counters connected to external signals, we need to synchronize first
    if (id >= CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_FIRST && id < CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_LAST)
    {
        this->update_external_pccr(this->pcer.value, this->pcer.value, id);
    }
#endif
    if (is_write) {
        this->iss.timing.flush_cycles();

        if (id == 31) {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Setting value to all PCCR (value: 0x%x)\n",
                value);
            for (int i=0; i<32; i++) {
                this->pccr[i].value = value;
            }
        } else {
            this->trace.msg(vp::Trace::LEVEL_DEBUG, "Setting PCCR value (pccr: %d, value: 0x%x)\n",
                id, value);
            this->pccr[id].value = value;
        }
    } else {
        value = this->pccr[id].value;
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "Reading PCCR (index: %d, value: 0x%x)\n", id, value);
    }

    return false;
}

bool Ri5kyCsr::pcer_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write) {
        this->iss.timing.flush_cycles();
        this->check_perf_config_change(this->pcer.value, this->pcmr.value);
        this->pcer.value = value & 0x7fffffff;
        return false;
    }
    return true;
}

bool Ri5kyCsr::pcmr_access(iss_insn_t *insn, bool is_write, iss_reg_t &value)
{
    if (is_write) {
        this->iss.timing.flush_cycles();
        this->iss.exec.switch_to_full_mode();
        this->check_perf_config_change(this->pcer.value, this->pcmr.value);
        this->pcmr.value = value;
        return false;
    }
    return true;
}

// static inline void iss_csr_ext_counter_set(Iss *iss, int id, unsigned int value)
// {
//     if (iss->timing.ext_counter[id].is_bound())
//     {
//         iss->timing.ext_counter[id].sync(value);
//     }
// }

// static inline void iss_csr_ext_counter_get(Iss *iss, int id, unsigned int *value)
// {
//     if (iss->timing.ext_counter[id].is_bound())
//     {
//         iss->timing.ext_counter[id].sync_back(value);
//     }
// }

void Ri5kyCsr::update_external_pccr(unsigned int pcer, unsigned int pcmr, int id)
{
//     unsigned int incr = 0;
//     // Only update if the counter is active as the external signal may report
//     // a different value whereas the counter must remain the same
//     if (((pcer & CSR_PCER_EVENT_MASK(id)) && (pcmr & CSR_PCMR_ACTIVE)) || iss->timing.event_trace_is_active(id))
//     {
//         iss_csr_ext_counter_get(iss, id, &incr);
//         iss->csr.pccr[id] += incr;
//         iss->timing.event_trace_account(id, incr);
//     }
//     else
//     {
//         // Nothing to do if the counter is inactive, it will get reset so that
//         // we get read events from now if it becomes active
//     }

//     // Reset the counter
//     if (iss->timing.ext_counter[id].is_bound())
//         iss_csr_ext_counter_set(iss, id, 0);

//     // if (cpu->traceEvent) sim_trace_event_incr(cpu, id, incr);
}

// void flushExternalCounters(Iss *iss)
// {
//     int i;
//     for (int i = CSR_PCER_FIRST_EXTERNAL_EVENTS; i < CSR_PCER_FIRST_EXTERNAL_EVENTS + CSR_PCER_NB_EXTERNAL_EVENTS; i++)
//     {
//         update_external_pccr(iss, i, iss->csr.pcer, iss->csr.pcmr);
//     }
// }

void Ri5kyCsr::check_perf_config_change(unsigned int pcer, unsigned int pcmr)
{
#if defined(CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR)
    // In case PCER or PCMR is modified, there is a special care about external signals as they
    // are still counting whatever the event active flag is. Reset them to start again from a
    // clean state
    {
        int i;
        // Check every external signal separatly
        for (int i = CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_FIRST; i < CONFIG_GVSOC_ISS_CSR_EXTERNAL_PCCR_LAST; i++)
        {
            // This will update our internal counter in case it is active or just reset it
            this->update_external_pccr(pcer, pcmr, i);
        }
    }
#endif
}

// static bool perfCounters_read(Iss *iss, int reg, iss_reg_t *value)
// {
// #if defined(CONFIG_GVSOC_ISS_EXTERNAL_PCCR)
//     // In case of counters connected to external signals, we need to synchronize first
//     if (reg >= CSR_PCCR(CSR_PCER_FIRST_EXTERNAL_EVENTS) && reg < CSR_PCCR(CSR_PCER_FIRST_EXTERNAL_EVENTS + CSR_PCER_NB_EXTERNAL_EVENTS))
//     {
//         update_external_pccr(iss, reg - CSR_PCCR(0), iss->csr.pcer, iss->csr.pcmr);
//         *value = iss->csr.pccr[reg - CSR_PCCR(0)];
//         iss->csr.trace.msg("Reading PCCR (index: %d, value: 0x%x)\n", reg - CSR_PCCR(0), *value);
//     }
// #endif
//     else
//     {
//         *value = iss->csr.pccr[reg - CSR_PCCR(0)];
//         iss->csr.trace.msg("Reading PCCR (index: %d, value: 0x%x)\n", reg - CSR_PCCR(0), *value);
//     }

//     return false;
// }

// static bool perfCounters_write(Iss *iss, int reg, unsigned int value)
// {
// #if defined(CONFIG_GVSOC_ISS_EXTERNAL_PCCR)
//     // In case of counters connected to external signals, we need to synchronize the external one
//     // with our
//     if (reg >= CSR_PCCR(CSR_PCER_FIRST_EXTERNAL_EVENTS) && reg < CSR_PCCR(CSR_PCER_FIRST_EXTERNAL_EVENTS + CSR_PCER_NB_EXTERNAL_EVENTS))
//     {
//         // This will update out counter, which will be overwritten afterwards by the new value and
//         // also set the external counter to 0 which makes sure they are synchroninez
//         update_external_pccr(iss, reg - CSR_PCCR(0), iss->csr.pcer, iss->csr.pcmr);
//     }
// #endif
//     else if (reg == CSR_PCCR(CSR_NB_PCCR))
//     {
//         iss->csr.trace.msg("Setting value to all PCCR (value: 0x%x)\n", value);

//         int i;
//         for (i = 0; i < 31; i++)
//         {
//             iss->csr.pccr[i] = value;
// #if defined(CONFIG_GVSOC_ISS_EXTERNAL_PCCR)
//             if (i >= CSR_PCER_FIRST_EXTERNAL_EVENTS && i < CSR_PCER_FIRST_EXTERNAL_EVENTS + CSR_PCER_NB_EXTERNAL_EVENTS)
//             {
//                 update_external_pccr(iss, i, 0, 0);
//             }
// #endif
//         }
//     }
//     else
//     {
//         iss->csr.trace.msg("Setting PCCR value (pccr: %d, value: 0x%x)\n", reg - CSR_PCCR(0), value);
//         iss->csr.pccr[reg - CSR_PCCR(0)] = value;
//     }
//     return false;
// }
// #endif
