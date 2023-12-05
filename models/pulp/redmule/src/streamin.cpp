#include <streamer.hpp>

#define L1_MASK 0x0003FFFF

//This is a placeholder
#define STREAM_MAX_WIDTH_BYTES 16

template <typename T>
Streamin<T>::Streamin(
  vp::io_master* out  ,
  vp::trace* trace
) : Streamer(out, trace) {};

template <typename T>
Streamin<T>::Streamin() : Streamer((vp::io_master *) NULL, (vp::trace *) NULL) {};

template <typename T>
vp::io_req* Streamin<T>::create_request() {
    return this->out->req_new(0, 0, 0, false);
}

template <typename T>
xt::xarray<T> Streamin<T>::ex(int width, int64_t& cycles) {
    auto addr = this->iterate();
    uint8_t load_data[STREAM_MAX_WIDTH_BYTES];
    auto width_padded = width + 4;
    auto addr_padded = addr & ~0x3;
    auto width_words = width_padded*sizeof(T)/4;
    auto width_rem   = width_padded*sizeof(T)%4;
    int64_t max_latency = 0;
  
    for(auto i=0; i<width_words; i++) {
        this->req->set_addr(addr_padded+i*4 & L1_MASK);
      
        this->req->set_size(4);
        this->req->set_data(load_data+i*4);
        int err = this->out->req(this->req);

        if (err == vp::IO_REQ_OK) {
            int64_t latency = this->req->get_latency();
            if (latency > max_latency) {
                max_latency = latency;
            }
        } else {
            this->trace->fatal("Unsupported asynchronous reply\n");
        }
    }

    if(width_rem) {
        this->req->init();
        this->req->set_addr(addr_padded+width_words*4 & L1_MASK);
        this->req->set_size(width_rem);
        this->req->set_data(load_data+width_words*4);
        this->req->set_is_write(false);
        int err = this->out->req(this->req);
        if (err == vp::IO_REQ_OK) {
        // int64_t latency = this->req->get_latency();
        // if (latency > max_latency) {
        //   max_latency = latency;
        // }
        } else {
            this->trace->fatal("Unsupported asynchronous reply\n");
        }
    }

    std::ostringstream stringStream;

    // if (this->trace_level == L3_ALL) {
    this->trace->msg(vp::trace::LEVEL_DEBUG, "Issuing read request (addr=0x%08x, size=%dB, latency=%d)\n", addr & L1_MASK, width*sizeof(T), cycles+1);
    // }

    xt::xarray<T> x = xt::zeros<T>({width});

    for(auto i=0; i<width; i++) {
        xt::view(x, i) = *(T *)(load_data + (addr & 0x3) + i*sizeof(T));
    }

    xt::print_options::set_line_width(1000);
    
    stringStream << "Read data: " << std::hex << x << std::dec << "\n";
    
    string s = stringStream.str();

    //if (this->trace_level == L3_ALL) {
      this->trace->msg(vp::trace::LEVEL_DEBUG, s.c_str());
    //}

    cycles += max_latency + 1;

    return x;
}

template class Streamin<_Float16>;