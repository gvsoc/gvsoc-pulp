/*
 * Copyright (C) 2025 Fondazione Chips-IT
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
 * Authors: Lorenzo Zuolo, Chips-IT (lorenzo.zuolo@chips.it)
 */

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <vp/itf/wire.hpp>
#include <vp/controller.hpp>

#include <algorithm>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#ifndef _Static_assert
#define _Static_assert static_assert
#endif

extern "C" {
#include <vfio-user/libvfio-user.h>
}

#include <poll.h>
#include <sys/mman.h>
#include <unistd.h>

#ifndef PCI_CAP_ID_MSIX
#define PCI_CAP_ID_MSIX 0x11
#endif

class PCIeVfioMemBridge : public vp::Component
{
public:
    PCIeVfioMemBridge(vp::ComponentConf &conf);
    ~PCIeVfioMemBridge() override;

private:
    struct DmaRegs {
        uint64_t src_addr;
        uint64_t dst_addr;
        uint32_t len;
        uint32_t ctrl;
        uint32_t status;
        uint32_t error;
        uint32_t magic;
        uint32_t direction;
    };

    struct PendingDma {
        bool valid = false;
        uint64_t src_addr = 0;
        uint64_t dst_addr = 0;
        uint32_t len = 0;
        uint32_t ctrl = 0;
        uint32_t direction = 0;
    };

    struct ActiveDmaState {
        uint8_t *host_ptr = nullptr;
        uint64_t device_addr = 0;
        uint32_t offset = 0;
        uint32_t remaining = 0;
        bool is_write = false;
    };

    struct __attribute__((packed)) MsixCap {
        uint8_t cap_id;
        uint8_t next;
        uint16_t msgctl;
        uint32_t table;
        uint32_t pba;
    };

    struct DmaMapping {
        uint64_t iova = 0;
        uint64_t size = 0;
        uint8_t *vaddr = nullptr;
        uint32_t prot = 0;
    };

    enum : uint64_t {
        BAR0_DMA_SRC_ADDR_LO = 0x00,
        BAR0_DMA_SRC_ADDR_HI = 0x04,
        BAR0_DMA_DST_ADDR_LO = 0x08,
        BAR0_DMA_DST_ADDR_HI = 0x0C,
        BAR0_DMA_LEN         = 0x10,
        BAR0_DMA_CTRL        = 0x14,
        BAR0_DMA_STATUS      = 0x18,
        BAR0_DMA_ERROR       = 0x1C,
        BAR0_DMA_MAGIC       = 0x20,
        BAR0_DMA_DIRECTION   = 0x24,
        BAR0_ENTRY_POINT     = 0x28,
        BAR0_FETCH_ENABLE    = 0x2C,
        BAR0_MSIX_TABLE_OFF  = 0x40,
        BAR0_MSIX_PBA_OFF    = 0x80,
    };

    enum : uint32_t {
        DMA_CTRL_START  = 1u << 0,
        DMA_CTRL_ABORT  = 1u << 1,
        DMA_CTRL_IRQ_EN = 1u << 2,
    };

    enum : uint32_t {
        DMA_STATUS_BUSY  = 1u << 0,
        DMA_STATUS_DONE  = 1u << 1,
        DMA_STATUS_ERROR = 1u << 2,
    };

    enum : uint32_t {
        DMA_DIR_HOST_TO_CARD = 0,
        DMA_DIR_CARD_TO_HOST = 1,
    };

    enum : uint32_t {
        DMA_ERR_NONE          = 0,
        DMA_ERR_BAD_DIRECTION = 1,
        DMA_ERR_ZERO_LENGTH   = 2,
        DMA_ERR_CARD_RANGE    = 3,
        DMA_ERR_HOST_ADDR     = 4,
        DMA_ERR_HOST_IO       = 5,
        DMA_ERR_SGL_TOO_SMALL = 6,
        DMA_ERR_BUSY          = 7,
        DMA_ERR_HOST_PERM     = 8,
        DMA_ERR_DEVICE_IO     = 9,
    };

    static constexpr uint32_t DMA_MAGIC_VALUE = 0x44504131; //DPA1
    static constexpr uint32_t MSIX_TABLE_BIR = 0;
    static constexpr uint32_t MSIX_PBA_BIR   = 0;
    static constexpr uint32_t MSIX_TABLE_OFFSET = BAR0_MSIX_TABLE_OFF;
    static constexpr uint32_t MSIX_PBA_OFFSET   = BAR0_MSIX_PBA_OFF;
    static constexpr uint32_t NUM_MSIX_VECTORS  = 1;
    static constexpr int VFIO_ATTACH_RETRY_US = 1000;
    static constexpr int VFIO_POLL_TIMEOUT_ATTACHED_MS = 1;
    static constexpr int VFIO_POLL_TIMEOUT_DETACHED_MS = 50;

    vp::Trace trace;
    vp::IoMaster mem_itf;
    vp::WireSlave<bool> done_irq_itf;
    vp::WireMaster<bool> fetch_enable_itf;
    vp::WireMaster<uint64_t> entry_addr_itf;

    vfu_ctx_t *vfu_ctx = nullptr;
    std::thread server_thread;
    bool server_thread_started = false;

    uint64_t bar0_size = 0;
    uint32_t dma_chunk_bytes = 0;
    std::vector<uint8_t> bar0;
    std::string socket_path;

    std::mutex mutex;
    std::condition_variable conn_cv;
    bool connected = false;
    bool stop_requested = false;
    bool msix_vector0_masked = false;
    bool entry_update_pending = false;
    bool fetch_update_pending = false;
    bool irq_pending = false;
    bool reset_pending = false;

    PendingDma pending_dma;
    PendingDma active_dma;
    ActiveDmaState active_dma_state;
    bool dma_inflight = false;
    std::vector<DmaMapping> dma_mappings;

    vp::ClockEvent *kick_event = nullptr;
    vp::ClockEvent *reset_event = nullptr;
    vp::ClockEvent *dma_event = nullptr;
    vp::IoReq dma_req;

    static ssize_t bar0_access(vfu_ctx_t *vfu_ctx,
                               char *buf,
                               size_t count,
                               loff_t offset,
                               bool is_write);
    static int device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type);
    static void dma_register_cb(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info);
    static void dma_unregister_cb(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info);
    static void irq_state_cb(vfu_ctx_t *vfu_ctx, uint32_t start, uint32_t count, bool mask);
    static void kick_handler(vp::Block *__this, vp::ClockEvent *event);
    static void reset_handler(vp::Block *__this, vp::ClockEvent *event);
    static void dma_handler(vp::Block *__this, vp::ClockEvent *event);
    static void mem_response(vp::Block *__this, vp::IoReq *req);
    static void done_req(vp::Block *__this, bool active);

    void start() override;
    void stop() override;
    void reset(bool active) override;

    void init_vfio();
    void stop_vfio();
    void vfio_server_thread();
    void process_pending_irq();

    uint32_t read32_nolock(uint64_t offset) const;
    void write32_nolock(uint64_t offset, uint32_t value);
    DmaRegs snapshot_dma_regs_nolock() const;
    void reset_registers_nolock();
    void clear_dma_status_nolock();
    bool schedule_dma_from_regs_nolock();
    uint8_t *get_host_ptr_nolock(uint64_t iova, uint32_t len, int prot);
    void finish_dma_success(uint32_t ctrl);
    void finish_dma_error(uint32_t ctrl, uint32_t error);
    void launch_dma(const PendingDma &req);
    void submit_next_dma_chunk();
};

/* Build the bridge component, expose GVSoC interfaces and initialize software-visible state. */
PCIeVfioMemBridge::PCIeVfioMemBridge(vp::ComponentConf &conf)
: vp::Component(conf)
{
    this->traces.new_trace("trace", &this->trace, vp::DEBUG);

    this->new_master_port("mem", &this->mem_itf);
    this->mem_itf.set_resp_meth(&PCIeVfioMemBridge::mem_response);

    this->done_irq_itf.set_sync_meth(&PCIeVfioMemBridge::done_req);
    this->new_slave_port("done_irq", &this->done_irq_itf);
    this->new_master_port("fetch_en", &this->fetch_enable_itf);
    this->new_master_port("entry_addr", &this->entry_addr_itf);

    this->socket_path = this->get_js_config()->get_child_str("socket_path");
    this->bar0_size = this->get_js_config()->get_child_int("bar0_size");
    this->dma_chunk_bytes = static_cast<uint32_t>(this->get_js_config()->get_child_int("dma_chunk_bytes"));

    if (this->bar0_size < 0x100) {
        throw std::runtime_error("bar0_size must be at least 0x100 for DMA control/MSI-X registers");
    }

    if (this->dma_chunk_bytes == 0) {
        throw std::runtime_error("dma_chunk_bytes must be strictly greater than zero");
    }

    this->bar0.resize(this->bar0_size, 0);
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->reset_registers_nolock();
    }

    this->kick_event = this->event_new(&PCIeVfioMemBridge::kick_handler);
    this->reset_event = this->event_new(&PCIeVfioMemBridge::reset_handler);
    this->dma_event = this->event_new(&PCIeVfioMemBridge::dma_handler);
}

/* Stop background activity and release VFIO resources when the component is destroyed. */
PCIeVfioMemBridge::~PCIeVfioMemBridge()
{
    this->stop_vfio();
}

/* Start the VFIO transport and wait until a client is attached or the component is stopped. */
void PCIeVfioMemBridge::start()
{
    this->init_vfio();

    std::unique_lock<std::mutex> lock(this->mutex);
    this->trace.msg(vp::Trace::LEVEL_INFO, "Waiting for QEMU connection on %s\n", this->socket_path.c_str());
    this->conn_cv.wait(lock, [this]() { return this->connected || this->stop_requested; });
    if (this->connected) {
        this->trace.msg(vp::Trace::LEVEL_INFO, "QEMU connected\n");
    }
}

/* Stop the VFIO side when the simulator is shutting down. */
void PCIeVfioMemBridge::stop()
{
    this->stop_vfio();
}

/* Restore bridge-local runtime state when the GVSoC reset line is asserted. */
void PCIeVfioMemBridge::reset(bool active)
{
    if (!active) {
        if (this->dma_event->is_enqueued()) {
            this->event_cancel(this->dma_event);
        }

        std::lock_guard<std::mutex> lock(this->mutex);
        this->reset_registers_nolock();
        this->entry_update_pending = false;
        this->fetch_update_pending = false;
        this->irq_pending = false;
        this->reset_pending = false;
        this->pending_dma = PendingDma();
        this->active_dma = PendingDma();
        this->active_dma_state = ActiveDmaState();
        this->dma_inflight = false;
    }
}

/* Create and realize the libvfio-user context, then launch the transport thread. */
void PCIeVfioMemBridge::init_vfio()
{
    std::lock_guard<std::mutex> lock(this->mutex);

    if (this->vfu_ctx != nullptr || this->server_thread_started) {
        return;
    }

    this->stop_requested = false;
    unlink(this->socket_path.c_str());

    this->vfu_ctx = vfu_create_ctx(VFU_TRANS_SOCK,
                                   this->socket_path.c_str(),
                                   LIBVFIO_USER_FLAG_ATTACH_NB,
                                   this,
                                   VFU_DEV_TYPE_PCI);
    if (this->vfu_ctx == nullptr) {
        throw std::runtime_error("vfu_create_ctx failed");
    }

    if (vfu_pci_init(this->vfu_ctx, VFU_PCI_TYPE_CONVENTIONAL, PCI_HEADER_TYPE_NORMAL, 0) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_pci_init failed");
    }

    vfu_pci_set_id(this->vfu_ctx, 0x1d1d, 0x0001, 0x0000, 0x0000);
    vfu_pci_set_class(this->vfu_ctx, 0x12, 0x00, 0x00);

    if (vfu_setup_device_reset_cb(this->vfu_ctx, &PCIeVfioMemBridge::device_reset) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_setup_device_reset_cb failed");
    }

    if (vfu_setup_device_dma(this->vfu_ctx,
                             &PCIeVfioMemBridge::dma_register_cb,
                             &PCIeVfioMemBridge::dma_unregister_cb) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_setup_device_dma failed");
    }

    if (vfu_setup_device_nr_irqs(this->vfu_ctx, VFU_DEV_MSIX_IRQ, NUM_MSIX_VECTORS) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_setup_device_nr_irqs MSI-X failed");
    }

    if (vfu_setup_irq_state_callback(this->vfu_ctx, VFU_DEV_MSIX_IRQ, &PCIeVfioMemBridge::irq_state_cb) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_setup_irq_state_callback MSI-X failed");
    }

    MsixCap msix_cap {};
    msix_cap.cap_id = PCI_CAP_ID_MSIX;
    msix_cap.next = 0;
    msix_cap.msgctl = static_cast<uint16_t>((NUM_MSIX_VECTORS - 1) & 0x7ff);
    msix_cap.table = (MSIX_TABLE_OFFSET & ~0x7u) | MSIX_TABLE_BIR;
    msix_cap.pba = (MSIX_PBA_OFFSET & ~0x7u) | MSIX_PBA_BIR;

    if (vfu_pci_add_capability(this->vfu_ctx, 0, 0, &msix_cap) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_pci_add_capability MSI-X failed");
    }

    if (vfu_setup_region(this->vfu_ctx,
                         VFU_PCI_DEV_BAR0_REGION_IDX,
                         this->bar0_size,
                         &PCIeVfioMemBridge::bar0_access,
                         VFU_REGION_FLAG_RW | VFU_REGION_FLAG_MEM,
                         nullptr,
                         0,
                         -1,
                         0) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_setup_region BAR0 failed");
    }

    if (vfu_realize_ctx(this->vfu_ctx) < 0) {
        vfu_destroy_ctx(this->vfu_ctx);
        this->vfu_ctx = nullptr;
        throw std::runtime_error("vfu_realize_ctx failed");
    }

    this->server_thread = std::thread(&PCIeVfioMemBridge::vfio_server_thread, this);
    this->server_thread_started = true;
}

/* Tear down the VFIO transport thread and release all transport-owned state. */
void PCIeVfioMemBridge::stop_vfio()
{
    std::thread local_thread;
    vfu_ctx_t *local_ctx = nullptr;

    {
        std::lock_guard<std::mutex> lock(this->mutex);
        this->stop_requested = true;
        this->conn_cv.notify_all();
        if (this->server_thread_started) {
            local_thread = std::move(this->server_thread);
            this->server_thread_started = false;
        }
    }

    if (local_thread.joinable()) {
        local_thread.join();
    }

    if (this->dma_event->is_enqueued()) {
        this->event_cancel(this->dma_event);
    }

    {
        std::lock_guard<std::mutex> lock(this->mutex);
        local_ctx = this->vfu_ctx;
        this->vfu_ctx = nullptr;
        this->connected = false;
        this->entry_update_pending = false;
        this->fetch_update_pending = false;
        this->irq_pending = false;
        this->reset_pending = false;
        this->pending_dma = PendingDma();
        this->active_dma = PendingDma();
        this->active_dma_state = ActiveDmaState();
        this->dma_inflight = false;
        this->dma_mappings.clear();
    }

    if (local_ctx != nullptr) {
        vfu_destroy_ctx(local_ctx);
    }

    if (!this->socket_path.empty()) {
        unlink(this->socket_path.c_str());
    }
}

/* Read a 32-bit BAR register from the software shadow without taking any lock. */
uint32_t PCIeVfioMemBridge::read32_nolock(uint64_t offset) const
{
    uint32_t value = 0;
    std::memcpy(&value, &this->bar0.at(offset), sizeof(value));
    return value;
}

/* Write a 32-bit BAR register into the software shadow without taking any lock. */
void PCIeVfioMemBridge::write32_nolock(uint64_t offset, uint32_t value)
{
    std::memcpy(&this->bar0.at(offset), &value, sizeof(value));
}

/* Snapshot all DMA control registers from BAR0 into a convenient structure. */
PCIeVfioMemBridge::DmaRegs PCIeVfioMemBridge::snapshot_dma_regs_nolock() const
{
    DmaRegs regs;
    regs.src_addr = (static_cast<uint64_t>(read32_nolock(BAR0_DMA_SRC_ADDR_HI)) << 32) |
                    static_cast<uint64_t>(read32_nolock(BAR0_DMA_SRC_ADDR_LO));
    regs.dst_addr = (static_cast<uint64_t>(read32_nolock(BAR0_DMA_DST_ADDR_HI)) << 32) |
                    static_cast<uint64_t>(read32_nolock(BAR0_DMA_DST_ADDR_LO));
    regs.len = read32_nolock(BAR0_DMA_LEN);
    regs.ctrl = read32_nolock(BAR0_DMA_CTRL);
    regs.status = read32_nolock(BAR0_DMA_STATUS);
    regs.error = read32_nolock(BAR0_DMA_ERROR);
    regs.magic = read32_nolock(BAR0_DMA_MAGIC);
    regs.direction = read32_nolock(BAR0_DMA_DIRECTION);
    return regs;
}

/* Restore the BAR shadow and local execution state to their reset values. */
void PCIeVfioMemBridge::reset_registers_nolock()
{
    std::fill(this->bar0.begin(), this->bar0.end(), 0);
    this->write32_nolock(BAR0_DMA_MAGIC, DMA_MAGIC_VALUE);
    this->write32_nolock(BAR0_DMA_DIRECTION, DMA_DIR_HOST_TO_CARD);
    this->write32_nolock(BAR0_DMA_STATUS, 0);
    this->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_NONE);
    this->write32_nolock(BAR0_ENTRY_POINT, 0);
    this->write32_nolock(BAR0_FETCH_ENABLE, 0);
    this->pending_dma = PendingDma();
    this->active_dma = PendingDma();
    this->active_dma_state = ActiveDmaState();
    this->dma_inflight = false;
    this->msix_vector0_masked = false;
}

/* Clear DMA completion/error bits before accepting a new transfer. */
void PCIeVfioMemBridge::clear_dma_status_nolock()
{
    this->write32_nolock(BAR0_DMA_STATUS, 0);
    this->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_NONE);
}

/* Latch a DMA request from BAR0 so that it can later be executed from the GVSoC thread. */
bool PCIeVfioMemBridge::schedule_dma_from_regs_nolock()
{
    DmaRegs regs = this->snapshot_dma_regs_nolock();

    if (this->pending_dma.valid || this->dma_inflight) {
        this->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_BUSY);
        this->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_DONE | DMA_STATUS_ERROR);
        this->write32_nolock(BAR0_DMA_CTRL, regs.ctrl & ~DMA_CTRL_START);
        return false;
    }

    this->pending_dma.valid = true;
    this->pending_dma.src_addr = regs.src_addr;
    this->pending_dma.dst_addr = regs.dst_addr;
    this->pending_dma.len = regs.len;
    this->pending_dma.ctrl = regs.ctrl;
    this->pending_dma.direction = regs.direction;

    this->clear_dma_status_nolock();
    this->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_BUSY);
    this->write32_nolock(BAR0_DMA_CTRL, regs.ctrl & ~DMA_CTRL_START);

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
                    "DMA scheduled dir=%u src=0x%llx dst=0x%llx len=0x%x\n",
                    this->pending_dma.direction,
                    (unsigned long long)this->pending_dma.src_addr,
                    (unsigned long long)this->pending_dma.dst_addr,
                    this->pending_dma.len);

    return true;
}

/* Resolve a guest IOVA to a host pointer using the mappings provided by libvfio-user. */
uint8_t *PCIeVfioMemBridge::get_host_ptr_nolock(uint64_t iova, uint32_t len, int prot)
{
    const uint64_t end = iova + static_cast<uint64_t>(len);

    for (const auto &mapping : this->dma_mappings) {
        const uint64_t map_start = mapping.iova;
        const uint64_t map_end = mapping.iova + mapping.size;

        if (iova >= map_start && end <= map_end) {
            if (mapping.vaddr == nullptr) {
                errno = EFAULT;
                return nullptr;
            }
            if ((mapping.prot & prot) != static_cast<uint32_t>(prot)) {
                errno = EACCES;
                return nullptr;
            }
            return mapping.vaddr + (iova - map_start);
        }
    }

    errno = ENOENT;
    return nullptr;
}

/* Mark the current DMA as completed successfully and optionally queue an interrupt. */
void PCIeVfioMemBridge::finish_dma_success(uint32_t ctrl)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_DONE);
    this->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_NONE);
    this->dma_inflight = false;
    this->active_dma = PendingDma();
    this->active_dma_state = ActiveDmaState();
    if (ctrl & DMA_CTRL_IRQ_EN) {
        this->irq_pending = true;
    }
}

/* Mark the current DMA as failed and optionally queue an interrupt. */
void PCIeVfioMemBridge::finish_dma_error(uint32_t ctrl, uint32_t error)
{
    std::lock_guard<std::mutex> lock(this->mutex);
    this->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_DONE | DMA_STATUS_ERROR);
    this->write32_nolock(BAR0_DMA_ERROR, error);
    this->dma_inflight = false;
    this->active_dma = PendingDma();
    this->active_dma_state = ActiveDmaState();
    if (ctrl & DMA_CTRL_IRQ_EN) {
        this->irq_pending = true;
    }
}

/* Translate one latched DMA request into a chunked GVSoC IO transfer sequence. */
void PCIeVfioMemBridge::launch_dma(const PendingDma &req)
{
    if (req.len == 0) {
        this->finish_dma_error(req.ctrl, DMA_ERR_ZERO_LENGTH);
        return;
    }

    uint64_t device_addr = 0;
    uint64_t host_addr = 0;
    int prot = 0;
    bool is_write = false;

    switch (req.direction) {
        case DMA_DIR_HOST_TO_CARD:
            host_addr = req.src_addr;
            device_addr = req.dst_addr;
            prot = PROT_READ;
            is_write = true;
            break;

        case DMA_DIR_CARD_TO_HOST:
            device_addr = req.src_addr;
            host_addr = req.dst_addr;
            prot = PROT_WRITE;
            is_write = false;
            break;

        default:
            this->finish_dma_error(req.ctrl, DMA_ERR_BAD_DIRECTION);
            return;
    }

    uint8_t *host_ptr = nullptr;
    {
        std::lock_guard<std::mutex> lock(this->mutex);
        errno = 0;
        host_ptr = this->get_host_ptr_nolock(host_addr, req.len, prot);
        if (host_ptr == nullptr) {
            this->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_DONE | DMA_STATUS_ERROR);
            this->write32_nolock(BAR0_DMA_ERROR, errno == EACCES ? DMA_ERR_HOST_PERM : DMA_ERR_HOST_ADDR);
            return;
        }

        this->active_dma = req;
        this->active_dma_state.host_ptr = host_ptr;
        this->active_dma_state.device_addr = device_addr;
        this->active_dma_state.offset = 0;
        this->active_dma_state.remaining = req.len;
        this->active_dma_state.is_write = is_write;
        this->dma_inflight = true;
    }

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
                    "Launching DMA dir=%u host=0x%llx device=0x%llx len=0x%x chunk=0x%x\n",
                    req.direction,
                    (unsigned long long)host_addr,
                    (unsigned long long)device_addr,
                    req.len,
                    this->dma_chunk_bytes);

    this->submit_next_dma_chunk();
}

/* Submit one memory chunk of the active DMA sequence and schedule the next one in simulated time. */
void PCIeVfioMemBridge::submit_next_dma_chunk()
{
    uint32_t ctrl = 0;
    uint32_t total_len = 0;
    uint32_t chunk_size = 0;
    uint32_t progress = 0;
    uint32_t remaining = 0;
    uint64_t chunk_addr = 0;
    uint8_t *chunk_data = nullptr;
    bool is_write = false;

    {
        std::lock_guard<std::mutex> lock(this->mutex);
        if (!this->dma_inflight) {
            return;
        }

        if (this->active_dma_state.remaining == 0) {
            ctrl = this->active_dma.ctrl;
        } else {
            chunk_size = std::min(this->active_dma_state.remaining, this->dma_chunk_bytes);
            chunk_addr = this->active_dma_state.device_addr + this->active_dma_state.offset;
            chunk_data = this->active_dma_state.host_ptr + this->active_dma_state.offset;
            is_write = this->active_dma_state.is_write;

            this->active_dma_state.offset += chunk_size;
            this->active_dma_state.remaining -= chunk_size;
            progress = this->active_dma_state.offset;
            remaining = this->active_dma_state.remaining;
            total_len = this->active_dma.len;
            ctrl = this->active_dma.ctrl;
        }
    }

    if (chunk_size == 0) {
        this->finish_dma_success(ctrl);
        return;
    }

    this->dma_req.init();
    this->dma_req.set_addr(chunk_addr);
    this->dma_req.set_size(chunk_size);
    this->dma_req.set_data(chunk_data);
    this->dma_req.set_is_write(is_write);

    this->trace.msg(vp::Trace::LEVEL_DEBUG,
                    "DMA chunk addr=0x%llx chunk=0x%x progress=0x%x/0x%x remaining=0x%x\n",
                    (unsigned long long)chunk_addr,
                    chunk_size,
                    progress,
                    total_len,
                    remaining);

    vp::IoReqStatus status = this->mem_itf.req(&this->dma_req);

    if (status == vp::IO_REQ_OK) {
        uint64_t latency = this->dma_req.get_full_latency();
        this->trace.msg(vp::Trace::LEVEL_DEBUG,
                        "DMA chunk accepted latency=%llu cycles\n",
                        (unsigned long long)latency);
        this->event_enqueue(this->dma_event, latency == 0 ? 1 : latency);
        return;
    }

    if (status == vp::IO_REQ_PENDING) {
        return;
    }

    this->finish_dma_error(ctrl, DMA_ERR_DEVICE_IO);
}

/* Handle a VFIO reset request by restoring bridge state visible to the guest. */
int PCIeVfioMemBridge::device_reset(vfu_ctx_t *vfu_ctx, vfu_reset_type_t type)
{
    PCIeVfioMemBridge *bridge = static_cast<PCIeVfioMemBridge *>(vfu_get_private(vfu_ctx));
    std::lock_guard<std::mutex> lock(bridge->mutex);

    bridge->trace.msg(vp::Trace::LEVEL_INFO, "VFIO device reset requested type=%d\n", type);
    bridge->reset_registers_nolock();
    bridge->dma_mappings.clear();
    bridge->entry_update_pending = false;
    bridge->fetch_update_pending = false;
    bridge->irq_pending = false;
    bridge->reset_pending = false;

    return 0;
}

/* Remember a new DMA mapping exported by the VFIO client. */
void PCIeVfioMemBridge::dma_register_cb(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    PCIeVfioMemBridge *bridge = static_cast<PCIeVfioMemBridge *>(vfu_get_private(vfu_ctx));
    std::lock_guard<std::mutex> lock(bridge->mutex);

    DmaMapping mapping;
    mapping.iova = reinterpret_cast<uintptr_t>(info->iova.iov_base);
    mapping.size = info->iova.iov_len;
    mapping.vaddr = reinterpret_cast<uint8_t *>(info->vaddr);
    mapping.prot = info->prot;
    bridge->dma_mappings.push_back(mapping);

    bridge->trace.msg(vp::Trace::LEVEL_DEBUG,
                      "DMA map iova=0x%llx len=0x%llx prot=0x%x\n",
                      (unsigned long long)mapping.iova,
                      (unsigned long long)mapping.size,
                      mapping.prot);
}

/* Forget a DMA mapping that has been removed by the VFIO client. */
void PCIeVfioMemBridge::dma_unregister_cb(vfu_ctx_t *vfu_ctx, vfu_dma_info_t *info)
{
    PCIeVfioMemBridge *bridge = static_cast<PCIeVfioMemBridge *>(vfu_get_private(vfu_ctx));
    std::lock_guard<std::mutex> lock(bridge->mutex);

    const uint64_t iova = reinterpret_cast<uintptr_t>(info->iova.iov_base);
    const uint64_t size = info->iova.iov_len;

    bridge->dma_mappings.erase(
        std::remove_if(bridge->dma_mappings.begin(),
                       bridge->dma_mappings.end(),
                       [iova, size](const DmaMapping &mapping) {
                           return mapping.iova == iova && mapping.size == size;
                       }),
        bridge->dma_mappings.end());

    bridge->trace.msg(vp::Trace::LEVEL_DEBUG,
                      "DMA unmap iova=0x%llx len=0x%llx\n",
                      (unsigned long long)iova,
                      (unsigned long long)size);
}

/* Track whether MSI-X vector 0 is currently masked by the guest. */
void PCIeVfioMemBridge::irq_state_cb(vfu_ctx_t *vfu_ctx, uint32_t start, uint32_t count, bool mask)
{
    PCIeVfioMemBridge *bridge = static_cast<PCIeVfioMemBridge *>(vfu_get_private(vfu_ctx));
    std::lock_guard<std::mutex> lock(bridge->mutex);

    if (start == 0 && count > 0) {
        bridge->msix_vector0_masked = mask;
        bridge->trace.msg(vp::Trace::LEVEL_DEBUG,
                          "MSI-X vector0 %s\n",
                          mask ? "masked" : "unmasked");
    }
}


/* Dispatch pending BAR-triggered actions from the GVSoC thread context. */
void PCIeVfioMemBridge::kick_handler(vp::Block *__this, vp::ClockEvent *event)
{
    PCIeVfioMemBridge *_this = static_cast<PCIeVfioMemBridge *>(__this);
    PendingDma dma_req;
    bool send_entry = false;
    bool send_fetch = false;
    uint64_t entry = 0;
    bool fetch_enable = false;

    {
        std::lock_guard<std::mutex> lock(_this->mutex);

        send_entry = _this->entry_update_pending;
        send_fetch = _this->fetch_update_pending;
        entry = static_cast<uint64_t>(_this->read32_nolock(BAR0_ENTRY_POINT));
        fetch_enable = _this->read32_nolock(BAR0_FETCH_ENABLE) != 0;
        _this->entry_update_pending = false;
        _this->fetch_update_pending = false;

        if (!_this->dma_inflight && _this->pending_dma.valid) {
            dma_req = _this->pending_dma;
            _this->pending_dma.valid = false;
        }
    }

    if (send_entry) {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "ENTRY_POINT written 0x%llx\n", (unsigned long long)entry);
        _this->entry_addr_itf.sync(entry);
    }

    if (send_fetch) {
        _this->trace.msg(vp::Trace::LEVEL_DEBUG, "FETCH_ENABLE written %d\n", fetch_enable);
        _this->fetch_enable_itf.sync(fetch_enable);
    }

    if (dma_req.valid) {
        _this->launch_dma(dma_req);
    }
}

/* Execute the deferred global reset directly from the event callback. */
void PCIeVfioMemBridge::reset_handler(vp::Block *__this, vp::ClockEvent *event)
{
    PCIeVfioMemBridge *_this = static_cast<PCIeVfioMemBridge *>(__this);

    {
        std::lock_guard<std::mutex> lock(_this->mutex);
        if (!_this->reset_pending) {
            return;
        }
        _this->reset_pending = false;
    }

    _this->trace.msg(vp::Trace::LEVEL_INFO, "Triggering full GVSoC reset after done IRQ\n");

    vp::Block *top = _this;
    while (top->get_parent() != nullptr) {
        top = top->get_parent();
    }

    top->reset_all(true);
    top->reset_all(false);
}

/* Continue a chunked DMA sequence when an asynchronous memory request completes. */
void PCIeVfioMemBridge::mem_response(vp::Block *__this, vp::IoReq *req)
{
    PCIeVfioMemBridge *_this = static_cast<PCIeVfioMemBridge *>(__this);
    _this->submit_next_dma_chunk();
}

/* Resume a chunked DMA transfer after the latency of the previous accepted request has elapsed. */
void PCIeVfioMemBridge::dma_handler(vp::Block *__this, vp::ClockEvent *event)
{
    PCIeVfioMemBridge *_this = static_cast<PCIeVfioMemBridge *>(__this);
    _this->submit_next_dma_chunk();
}

/* Emit a pending MSI-X interrupt once the VFIO transport is ready to accept it. */
void PCIeVfioMemBridge::process_pending_irq()
{
    bool trigger = false;

    {
        std::lock_guard<std::mutex> lock(this->mutex);
        trigger = this->irq_pending && !this->msix_vector0_masked && this->connected && this->vfu_ctx != nullptr;
        if (trigger) {
            this->irq_pending = false;
        }
    }

    if (!trigger) {
        return;
    }

    errno = 0;
    if (vfu_irq_trigger(this->vfu_ctx, 0) < 0) {
        this->trace.warning("vfu_irq_trigger failed errno=%d\n", errno);
    } else {
        this->trace.msg(vp::Trace::LEVEL_DEBUG, "MSI-X vector0 triggered\n");
    }
}

/* React to the accelerator completion IRQ by disabling fetch and requesting a full reset. */
void PCIeVfioMemBridge::done_req(vp::Block *__this, bool active)
{
    PCIeVfioMemBridge *_this = static_cast<PCIeVfioMemBridge *>(__this);
    if (!active) {
        return;
    }

    _this->trace.msg(vp::Trace::LEVEL_DEBUG, "Received end of compute from accelerator\n");
    _this->fetch_enable_itf.sync(false);

    {
        std::lock_guard<std::mutex> lock(_this->mutex);
        _this->reset_pending = true;
    }

    if (_this->reset_event != nullptr && !_this->reset_event->is_enqueued()) {
        _this->event_enqueue(_this->reset_event, 1);
    }
}

/* Serve guest BAR0 MMIO accesses and defer any GVSoC-side work to the simulator thread. */
ssize_t PCIeVfioMemBridge::bar0_access(vfu_ctx_t *vfu_ctx,
                                       char *buf,
                                       size_t count,
                                       loff_t offset,
                                       bool is_write)
{
    PCIeVfioMemBridge *bridge = static_cast<PCIeVfioMemBridge *>(vfu_get_private(vfu_ctx));
    bool schedule_event = false;

    {
        std::lock_guard<std::mutex> lock(bridge->mutex);

        if (offset + count > bridge->bar0.size()) {
            errno = EINVAL;
            return -1;
        }

        if (!is_write) {
            std::memcpy(buf, &bridge->bar0[offset], count);
            return static_cast<ssize_t>(count);
        }

        std::memcpy(&bridge->bar0[offset], buf, count);

        const bool touched_ctrl =
            (offset <= static_cast<loff_t>(BAR0_DMA_CTRL + 3)) &&
            (offset + count > static_cast<loff_t>(BAR0_DMA_CTRL));
        const bool touched_entry =
            (offset <= static_cast<loff_t>(BAR0_ENTRY_POINT + 3)) &&
            (offset + count > static_cast<loff_t>(BAR0_ENTRY_POINT));
        const bool touched_fetch =
            (offset <= static_cast<loff_t>(BAR0_FETCH_ENABLE + 3)) &&
            (offset + count > static_cast<loff_t>(BAR0_FETCH_ENABLE));

        if (touched_entry) {
            bridge->entry_update_pending = true;
            schedule_event = true;
        }

        if (touched_fetch) {
            bridge->fetch_update_pending = true;
            schedule_event = true;
        }

        if (touched_ctrl) {
            const uint32_t ctrl = bridge->read32_nolock(BAR0_DMA_CTRL);

            if (ctrl & DMA_CTRL_ABORT) {
                bridge->pending_dma.valid = false;
                bridge->write32_nolock(BAR0_DMA_CTRL, ctrl & ~(DMA_CTRL_START | DMA_CTRL_ABORT));

                if (bridge->dma_inflight) {
                    bridge->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_BUSY);
                    bridge->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_BUSY);
                } else {
                    bridge->active_dma = PendingDma();
                    bridge->write32_nolock(BAR0_DMA_STATUS, DMA_STATUS_DONE | DMA_STATUS_ERROR);
                    bridge->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_NONE);
                }
            } else if ((ctrl & DMA_CTRL_START) && bridge->connected) {
                schedule_event = bridge->schedule_dma_from_regs_nolock() || schedule_event;
            }
        }
    }

    if (schedule_event) {
        gv::Controller::get().engine_lock();
        if (bridge->kick_event != nullptr && !bridge->kick_event->is_enqueued()) {
            bridge->event_enqueue(bridge->kick_event, 1);
        }
        gv::Controller::get().engine_unlock();
    }

    return static_cast<ssize_t>(count);
}

/* Run the libvfio-user event loop, handling attach, transport polling and disconnects. */
void PCIeVfioMemBridge::vfio_server_thread()
{
    bool attached = false;

    while (true) {
        {
            std::lock_guard<std::mutex> lock(this->mutex);
            if (this->stop_requested) {
                break;
            }
        }

        if (!attached) {
            errno = 0;
            int ret = vfu_attach_ctx(this->vfu_ctx);
            if (ret == 0) {
                attached = true;
                {
                    std::lock_guard<std::mutex> lock(this->mutex);
                    this->connected = true;
                }
                this->conn_cv.notify_all();
                this->trace.msg(vp::Trace::LEVEL_INFO,
                                "QEMU attached poll_fd=%d\n",
                                vfu_get_poll_fd(this->vfu_ctx));
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                this->trace.warning("vfu_attach_ctx failed errno=%d\n", errno);
                break;
            }
        }

        this->process_pending_irq();

        int fd = vfu_get_poll_fd(this->vfu_ctx);
        if (fd < 0) {
            // No poll fd is available yet: back off a little to avoid spinning
            // while the transport is still transitioning through attach/re-attach.
            usleep(VFIO_ATTACH_RETRY_US);
            continue;
        }

        struct pollfd pfd;
        pfd.fd = fd;
        pfd.events = POLLIN;
        pfd.revents = 0;

        const int timeout_ms = attached ? VFIO_POLL_TIMEOUT_ATTACHED_MS
                                        : VFIO_POLL_TIMEOUT_DETACHED_MS;
        int pret = poll(&pfd, 1, timeout_ms);
        if (pret < 0) {
            if (errno == EINTR) {
                continue;
            }
            this->trace.warning("poll failed errno=%d\n", errno);
            break;
        }

        if (pret == 0) {
            continue;
        }

        errno = 0;
        int ret = vfu_run_ctx(this->vfu_ctx);
        if (ret == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                continue;
            }

            if (errno == ENOTCONN) {
                attached = false;
                std::lock_guard<std::mutex> lock(this->mutex);
                this->connected = false;
                this->pending_dma = PendingDma();
                this->active_dma = PendingDma();
                this->dma_inflight = false;
                this->irq_pending = false;
                this->reset_pending = false;
                this->dma_mappings.clear();
                this->write32_nolock(BAR0_DMA_STATUS, 0);
                this->write32_nolock(BAR0_DMA_ERROR, DMA_ERR_NONE);
                this->trace.msg(vp::Trace::LEVEL_INFO, "VFIO client disconnected, waiting for re-attach\n");
                continue;
            }

            this->trace.warning("VFIO error errno=%d\n", errno);
            break;
        }
    }
}

/* Factory entry point used by GVSoC to instantiate the component module. */
extern "C" vp::Component *gv_new(vp::ComponentConf &config)
{
    return new PCIeVfioMemBridge(config);
}
