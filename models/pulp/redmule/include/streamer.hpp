#ifndef STREAMER_HPP
#define STREAMER_HPP

#include <xtensor.hpp>
#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include <stdint.h>

class Streamer {
    public:
        void reset_iteration();
        uint32_t iterate();
        void print_config();
        uint32_t get_base_addr();
        uint32_t get_d0_length();
        uint32_t get_d0_stride();
        uint32_t get_d1_length();
        uint32_t get_d1_stride();
        uint32_t get_d2_length();
        uint32_t get_d2_stride();

        void config(
            uint32_t base_addr  ,
            uint32_t d0_length  ,
            uint32_t d0_stride  ,
            uint32_t d1_length  ,
            uint32_t d1_stride  ,
            uint32_t d2_length  ,
            uint32_t d2_stride
        );

    protected:
        Streamer(
            vp::io_master* out      ,
	        vp::trace* trace
        );  

        virtual vp::io_req* create_request();
    
        vp::io_master* out;
	    vp::trace* trace;
        vp::io_req* req;
    
        uint32_t base_addr;
        uint32_t d0_length;
        uint32_t d0_stride;
        uint32_t d1_length;
        uint32_t d1_stride;
        uint32_t d2_length;
        uint32_t d2_stride;
        // internal
        uint32_t current_addr;
        uint32_t ba;
        uint32_t la;
        uint32_t wa;
        uint32_t bc;
        uint32_t wc;
        uint32_t lc;
        uint32_t oc;
};

template <typename T>
class Streamin : public Streamer {
    public:
        Streamin(
            vp::io_master* out  ,
	        vp::trace* trace
        );

        Streamin();

        vp::io_req* create_request() override;
        xt::xarray<T> ex(int width, int64_t& cycles);

};

template <typename T>
class Streamout : public Streamer {
    public:
    Streamout(
        vp::io_master* out  ,
	    vp::trace* trace
    );

    Streamout();

    vp::io_req* create_request() override;
    void ex(xt::xarray<T> data, int width, int64_t& cycles, int32_t enable);
};

#endif