#include <redmule.hpp>

#include <memory.h>

#define JMP ARRAY_HEIGHT * (PIPE_REGS + 1) * sizeof(src_fmt_t)//2*DATA_WIDTH/ADDR_WIDTH*sizeof(src_fmt_t) //4*DATA_WIDTH/ADDR_WIDTH

void RedMule::fsm_start_handler(void *__this, vp::clock_event *event) {
    RedMule* _this = (RedMule *) __this;

    printf("Starting op...\n");

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
	
	printf("PRE ALLOC\n");

	printf("LEFTS %x\n", _this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2]);

	printf("W COLS ITERS %d\n", _this->register_file [REDMULE_REG_W_ITER_PTR>>2] & 0x0000ffff);
	printf("W ROWS ITERS %d\n", _this->register_file [REDMULE_REG_W_ITER_PTR>>2]>>16);

	printf("YZ TOT LEN %d\n", _this->register_file [REDMULE_REG_YZ_TOT_LEN_PTR>>2]);

	printf("YZ D0 STRIDE %d\n", _this->register_file [REDMULE_REG_YZ_D0_STRIDE_PTR>>2]);

	printf("YZ D2 STRIDE %d\n", _this->register_file [REDMULE_REG_YZ_D2_STRIDE_PTR >> 2]);

	printf("W TOT LEN %d\n", _this->register_file [REDMULE_REG_W_TOT_LEN_PTR>>2]);

	_this->buffers.alloc_buffers(
		_this->register_file [REDMULE_REG_X_D1_STRIDE_PTR>>2]/sizeof(src_fmt_t),
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 24) & 0x000000ff,
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 16) & 0x000000ff,
		(_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] >> 8) & 0x000000ff,
		_this->register_file [REDMULE_REG_LEFTOVERS_PTR>>2] & 0x000000ff
	);

	printf("POST ALLOC\n");

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

    printf("Finishing op...\n");

	_this->buffers.free_buffers();

    _this->state.set(IDLE);

	_this->irq.sync(true);

	printf("POST IRQ\n");
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
		//printf("ENQ EVT %x\n", rand());
    }
}

int RedMule::fsm() {
    auto next_state = this->state.get();

	int latency = 0;
    switch (this->state.get()) {
        case IDLE:  //Unreachable, fsm_start_handler is used instead
            break;

	    case STARTING:      //We wait for W to be loaded, then we start computing
            //printf("Now in STARTING STATE!\n");
			
			if (this->preload_iter(&latency))
				next_state = COMPUTING;

            //printf("This took: %d\n", latency);
            break;

	    case COMPUTING:     //Once the block is finished, we start buffering
			//printf("Now in COMPUTING STATE!\n");

			if (this->compute_iter(&latency))
				next_state = STORING;
			
			//printf("This took: %d\n", latency);
            break;


		//Remove, we store w/out buffering
	    case BUFFERING:     //Finally, when the buffer is full we store the result
            break;

	    case STORING:       //While the buffer is emptying we load Y, then we compute again
			//printf("Now in STORING STATE!\n");

			if (this->store_iter(&latency)) {
				if (this->done/*this->w_cols_iters == 10*/) {
					next_state = FINISHED;
				} else {
					next_state = COMPUTING;
				}
			}

			//printf("This took: %d\n", latency);

            break;

	    case FINISHED:  //Unreachable, fsm_end_handler is used instead
            break;

        default:
            this->trace.fatal("redmule_fsm: UNKNOWN STATE (%d)!\n", this->state.get());
    }

    this->state.set(next_state);
    return latency;
}