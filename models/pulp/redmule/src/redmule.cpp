#include <redmule.hpp>

#include <stdio.h>
#include <memory.h>

RedMule::RedMule(js::config *config) : vp::component(config) {

}

void RedMule::reset(bool active) {
	if (active) {
		this->state.set(IDLE);
		memset(this->register_file, 0, sizeof register_file);
	}
}

vp::io_req_status_e RedMule::hwpe_slave(void *__this, vp::io_req *req) {
    RedMule *_this = (RedMule *)__this;
	uint32_t address = req->get_addr();

    if (req->get_is_write()) {
		uint32_t data = * ((uint32_t *) (req->get_data()));

		_this->trace.msg("Write request; Address: %x\n", address);

		if (address >= REDMULE_REG_OFFS) {	//Writing registers
			if (address - REDMULE_REG_OFFS > 0x48) {
				//Error handling

				return vp::IO_REQ_OK;
			}

			_this->trace.msg("Writing register #%d\n", (address - REDMULE_REG_OFFS) >> 2);

			_this->register_file [(address - REDMULE_REG_OFFS) >> 2] = data;

			_this->trace.msg("Wrote %x\n", _this->register_file [(address - REDMULE_REG_OFFS) >> 2]);
		} else {	//Writing commands
			switch (address) {
				case REDMULE_TRIGGER:
					_this->trace.msg("JOB TRIGGERED\n");
					_this->event_enqueue(_this->fsm_start_event, 1);
					break;

				case REDMULE_ACQUIRE:
					break;

				case REDMULE_FINISHED:
					break;

				case REDMULE_STATUS:
					break;

				case REDMULE_RUNNING_JOB:
					break;

				case REDMULE_SOFT_CLEAR:
					break;

				default:
					_this->trace.msg("Usupported command!\n");          
			}
		}
    } else {
    	_this->trace.msg("Read request\n");
    }

    return vp::IO_REQ_OK;
}

int RedMule::build() {
	this->traces.new_trace("trace", &this->trace, vp::DEBUG);

	this->new_master_port("out", &this->out);

	this->new_master_port("irq", &this->irq);
	
	this->in.set_req_meth(&RedMule::hwpe_slave);
    this->new_slave_port("input", &this->in);

	this->w_stream = RedMule_Streamer(this, false);
	this->x_stream = RedMule_Streamer(this, false);
	this->y_stream = RedMule_Streamer(this, false);
	this->z_stream = RedMule_Streamer(this, true);

	this->buffers = RedMule_Buffers(this);

	//Event Handlers
    this->fsm_start_event = this->event_new(&RedMule::fsm_start_handler);
    this->fsm_event = this->event_new(&RedMule::fsm_handler);
    this->fsm_end_event = this->event_new(&RedMule::fsm_end_handler);

	this->state.set(IDLE);

	this->trace.msg("Build complete\n");

	return 0;
}

extern "C" vp::component *vp_constructor(js::config *config) {
    return new RedMule(config);
}
