#ifndef __REDMULE_HPP__
#define __REDMULE_HPP__

#include <vp/vp.hpp>
#include <vp/itf/io.hpp>
#include "archi_redmule.h"
#include "config.h"

//Enums here

enum redmule_state {
	IDLE,
	STARTING,
	COMPUTING, 
	BUFFERING, 
	STORING, 
	FINISHED
};

typedef uint64_t strobe_t;

class RedMule;

class RedMule_Engine {
	public:
		RedMule_Engine();
		void compute_next();

	private:
		dst_fmt_t z_buffer [ARRAY_HEIGHT * (PIPE_REGS + 1)] [ARRAY_WIDTH];
		dst_fmt_t x_buffer  [ARRAY_HEIGHT * 2] [ARRAY_WIDTH];
		dst_fmt_t w_buffer [ARRAY_HEIGHT] [ARRAY_HEIGHT * (PIPE_REGS + 1)];

		dst_fmt_t* get_next_z();
		dst_fmt_t* get_next_x();
		dst_fmt_t* get_next_w();
};

class RedMule_Buffers {
	public:
		RedMule_Buffers();

		void alloc_buffers(uint32_t n, uint8_t x_rows_lftovr, uint8_t x_cols_lftovr, uint8_t w_rows_lftovr, uint8_t w_cols_lftovr);
		void free_buffers();

		dst_fmt_t* get_next_w();
		dst_fmt_t* get_next_x();
		dst_fmt_t* get_next_y();
		dst_fmt_t* get_next_z();

		void compute_z();

	private:
		uint32_t w_pointer;
		uint32_t x_pointer;
		uint32_t y_pointer;
		uint32_t z_pointer;

		uint32_t n;

		uint8_t x_cols_lftovr;
		uint8_t x_rows_lftovr;
		uint8_t w_cols_lftovr;
		uint8_t w_rows_lftovr;

		uint32_t x_offs;
		uint32_t y_offs;

		uint32_t w_iters;
		uint32_t x_d0_iters;
		uint32_t x_d1_iters;
		uint32_t y_iters;
		uint32_t z_iters;

		int x_row_offs(int k);

		dst_fmt_t** w;
		dst_fmt_t** x;

		dst_fmt_t y [ARRAY_WIDTH * 2] [(PIPE_REGS + 1) * ARRAY_HEIGHT]; //[ARRAY_WIDTH * 2] [(PIPE_REGS + 1) * ARRAY_HEIGHT];
		dst_fmt_t z [ARRAY_WIDTH] [(PIPE_REGS + 1) * ARRAY_HEIGHT]; //[ARRAY_WIDTH] [(PIPE_REGS + 1) * ARRAY_HEIGHT];
};

class RedMule_Streamer {
	public:
		RedMule_Streamer(RedMule* redmule, bool is_write);
		RedMule_Streamer();
		int iterate(void* buf, strobe_t strb);
		void configure(
			uint32_t	base_addr	,
			uint32_t 	tot_len 	,
			uint32_t 	d0_len 		,
			uint32_t 	d0_stride 	,
			uint32_t 	d1_len 		,
			uint32_t 	d1_stride 	,
			uint32_t	d2_len		,
			uint32_t 	d2_stride	,
			uint32_t	d3_stride
		);
		void set_base_addr(uint32_t addr);
		uint32_t get_base_addr();
		bool is_done();

	private:
		RedMule* redmule;
		vp::io_req* req;

		//dst_fmt_t out_buf [sizeof(dst_fmt_t) * DATA_WIDTH/(8 * sizeof(dst_fmt_t))];
		uint32_t pos;
		uint32_t tot_iters;
		uint32_t d0_iters;
		uint32_t d1_iters;
		uint32_t d2_iters;

		uint32_t	base_addr	;
		uint32_t 	tot_len 	;
		uint32_t 	d0_len 		;
		uint32_t 	d0_stride 	;
		uint32_t 	d1_len 		;
		uint32_t 	d1_stride 	;
		uint32_t 	d2_len		;
		uint32_t 	d2_stride	;
		uint32_t	d3_stride	;
		bool		is_write	;

		int rw_data(int width, void* buf, strobe_t strb);
};

//The class itself
class RedMule : public vp::component {

	friend class RedMule_base;

	public:
    	RedMule(js::config *config);
		
		int build();
		void reset(bool active);
		
		vp::io_slave in;

		//vp::io_req io_req;
		vp::io_master out;
		vp::trace trace;
		vp::reg_32 state;

	private:
		//HW Parameters
		
	
		static vp::io_req_status_e hwpe_slave(void *__this, vp::io_req *req);

    	static void fsm_start_handler(void *__this, vp::clock_event *event);
    	static void fsm_handler(void *__this, vp::clock_event *event);
    	static void fsm_end_handler(void *__this, vp::clock_event *event);

    	// MAIN FSM and LOOP
    	int  fsm();
    	void fsm_loop();

		bool preload_iter(int* latency);
		bool compute_iter(int* latency);
		bool store_iter(int* latency);

		void first_iter_routine(int* latency);
		void standard_iter_routine(int* latency);
		void last_iter_routine(int* latency);

		int subcycle_routine(bool skip_w, int label, strobe_t strb);

		int buf_disamb(int label, strobe_t strb);

		void reset_sched();

		//RF
		uint32_t register_file [19];

		
		vp::wire_master<bool> irq;

		vp::clock_event *fsm_start_event;
    	vp::clock_event *fsm_event;
    	vp::clock_event *fsm_end_event;

		//Everything else...

		uint32_t preload_cnt;
		uint32_t compute_cnt;
		uint32_t store_ctn;

		uint32_t w_cols_iters;
		uint32_t x_rows_iters;

		strobe_t z_strb;

		bool last_x_row;

		uint32_t z_cycle_stores;

		bool done;

		uint32_t hypercycle_cnt;
		uint32_t cycle_cnt;
		uint32_t subcycle_cnt;

		RedMule_Streamer z_stream;
		RedMule_Streamer x_stream;
		RedMule_Streamer y_stream;
		RedMule_Streamer w_stream;

		RedMule_Buffers buffers;
};

#endif
