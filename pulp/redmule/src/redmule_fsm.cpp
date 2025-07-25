#include <redmule.hpp>

#include <memory.h>

#define JMP ARRAY_HEIGHT * (PIPE_REGS + 1) * sizeof(src_fmt_t)

void RedMule::fsm_start_handler(void *__this, vp::clock_event *event) {
    RedMule* _this = (RedMule *) __this;

    _this->trace.msg("Starting op...\n");

	_this->trace.msg("Parameters:\n");
	_this->trace.msg("\tX ROWS LEFTOVER:\t%x\n", (_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 24) & 0x000000ff);
	_this->trace.msg("\tX COLS LEFTOVER:\t%x\n", (_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 16) & 0x000000ff);
	_this->trace.msg("\tW ROWS LEFTOVER:\t%x\n", (_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 8) & 0x000000ff);
	_this->trace.msg("\tW COLS LEFTOVER:\t%x\n", _this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] & 0x000000ff);
	_this->trace.msg("\tW COLS ITERS:\t%d\n", _this->register_file [REDMULE_REG_W_ITER_PTR>>2] & 0x0000ffff);
	_this->trace.msg("\tW ROWS ITERS:\t%d\n", _this->register_file [REDMULE_REG_W_ITER_PTR>>2]>>16);
	_this->trace.msg("\tX COLS ITERS:\t%d\n", _this->register_file [REDMULE_REG_X_ITER_PTR>>2] & 0x0000ffff);
	_this->trace.msg("\tX ROWS ITERS:\t%d\n", _this->register_file [REDMULE_REG_X_ITER_PTR>>2]>>16);
	_this->trace.msg("\tYZ TOT LEN:\t%d\n", _this->register_file [REDMULE_REG_YZ_TOT_LEN_PTR>>2]);
	_this->trace.msg("\tYZ D0 STRIDE:\t%d\n", _this->register_file [REDMULE_REG_YZ_D0_STRIDE_PTR>>2]);
	_this->trace.msg("\tYZ D2 STRIDE:\t%d\n", _this->register_file [REDMULE_REG_YZ_D2_STRIDE_PTR >> 2]);
	_this->trace.msg("\tW TOT LEN:\t%d\n", _this->register_file [REDMULE_REG_W_TOT_LEN_PTR>>2]);
	_this->trace.msg("\tX TOT LEN:\t%d\n", _this->register_file [REDMULE_REG_X_TOT_LEN_PTR>>2]);

	_this->trace.msg("Configuring z_stream:\n");

	_this->z_stream.configure(
		_this->register_file [REDMULE_REG_Z_PTR>>2],							//base_addr
		_this->register_file [REDMULE_REG_YZ_TOT_LEN_PTR>>2],					//tot_len
		ARRAY_WIDTH,															//d0_len
		_this->register_file [REDMULE_REG_YZ_D0_STRIDE_PTR>>2],					//d0_stride
		_this->register_file [REDMULE_REG_W_ITER_PTR >> 2] & 0x0000ffff,		//d1_len
		JMP,																	//d1_stride
		0,																		//d2_len
		_this->register_file [REDMULE_REG_YZ_D2_STRIDE_PTR >> 2],				//d2_stride
		0																		//d3_stride
	);

	_this->trace.msg("Configuring x_stream:\n");

	_this->x_stream.configure(
		_this->register_file [REDMULE_REG_X_PTR>>2],							//base_addr	
		_this->register_file [REDMULE_REG_X_TOT_LEN_PTR>>2],					//tot_len 	
		ARRAY_WIDTH,															//d0_len 		
		_this->register_file [REDMULE_REG_X_D1_STRIDE_PTR>>2],					//d0_stride	
		_this->register_file [REDMULE_REG_X_ITER_PTR>>2] & 0x0000ffff,			//d1_len 		
		JMP,																	//d1_stride 	
		_this->register_file [REDMULE_REG_W_ITER_PTR>>2] & 0x0000ffff,			//d2_len
		0,																		//d2_stride
		_this->register_file [REDMULE_REG_X_D1_STRIDE_PTR>>2] * ARRAY_WIDTH		//d3_stride
	);

	_this->trace.msg("Configuring y_stream:\n");

	_this->y_stream.configure(
		_this->register_file [REDMULE_REG_Y_PTR>>2],							//base_addr
		_this->register_file [REDMULE_REG_YZ_TOT_LEN_PTR>>2],					//tot_len
		ARRAY_WIDTH,															//d0_len
		_this->register_file [REDMULE_REG_YZ_D0_STRIDE_PTR>>2],					//d0_stride
		_this->register_file [REDMULE_REG_W_ITER_PTR >> 2] & 0x0000ffff,		//d1_len
		JMP,																	//d1_stride
		0,																		//d2_len
		_this->register_file [REDMULE_REG_YZ_D2_STRIDE_PTR >> 2],				//d2_stride
		0																		//d3_stride
	);

	_this->trace.msg("Configuring w_stream:\n");

	_this->w_stream.configure(
		_this->register_file [REDMULE_REG_W_PTR>>2],							//base_addr
		_this->register_file [REDMULE_REG_W_TOT_LEN_PTR>>2],					//tot_len
		_this->register_file [REDMULE_REG_W_ITER_PTR>>2]>>16,					//d0_len
		_this->register_file [REDMULE_REG_W_D0_STRIDE_PTR>>2],					//d0_stride
		_this->register_file [REDMULE_REG_W_ITER_PTR>>2] & 0x0000ffff,			//d1_len
		JMP,																	//d1_stride
		0,																		//d2_len
		0,																		//d2_stride
		0																		//d3_stride
	);

	_this->buffers.alloc_buffers(
		_this->register_file [REDMULE_REG_X_D1_STRIDE_PTR>>2]/sizeof(src_fmt_t),
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 24) & 0x000000ff,
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 16) & 0x000000ff,
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 8) & 0x000000ff,
		_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] & 0x000000ff
	);

	_this->reset_sched();

    _this->state.set(STARTING);
    _this->fsm_loop();
}

void RedMule::fsm_handler(void *__this, vp::clock_event *event) {
    RedMule* _this = (RedMule *) __this;

    _this->fsm_loop();
}

void RedMule::fsm_end_handler(void *__this, vp::clock_event *event) {
    RedMule* _this = (RedMule *) __this;
	_this->buffers.free_buffers();

    _this->state.set(IDLE);

	_this->irq.sync(true);
}

void RedMule::fsm_loop() {
    uint32_t latency = 0;

    do {
        latency = this->fsm();
    } while(latency == 0 && state.get() != FINISHED);

    if(state.get() == FINISHED && !this->fsm_end_event->is_enqueued()) {
        this->event_enqueue(this->fsm_end_event, latency);
    } else if (!this->fsm_event->is_enqueued()) {
        this->event_enqueue(this->fsm_event, latency);
    }
}

int RedMule::fsm() {
    auto next_state = this->state.get();

	int latency = 0;
    switch (this->state.get()) {
	    case STARTING:			
			if (this->preload_iter(&latency))
				next_state = COMPUTING;

            break;

	    case COMPUTING:
			if (this->compute_iter(&latency))
				next_state = STORING;
			
            break;

	    case STORING:
			if (this->store_iter(&latency)) {
				if (this->done) {
					next_state = FINISHED;
				} else {
					next_state = COMPUTING;
				}
			}

            break;

	    case FINISHED:
            break;

        default:
            this->trace.fatal("redmule_fsm: UNKNOWN STATE (%d)!\n", this->state.get());
    }

    this->state.set(next_state);
    return latency;
}