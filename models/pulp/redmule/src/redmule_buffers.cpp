#include <redmule.hpp>

#include <cmath>
#include <memory.h>

RedMule_Buffers::RedMule_Buffers() {
    this->w_pointer = 0;
	this->x_pointer = 0;
	this->y_pointer = 0;
	this->z_pointer = 0;

    this->n = 0;

    this->x_cols_lftovr = 0;
	this->x_rows_lftovr = 0;
	this->w_cols_lftovr = 0;
	this->w_rows_lftovr = 0;

    this->x_offs = 0;
    this->y_offs = 0;

	this->w_iters = 0;
	this->x_d0_iters = 0;
    this->x_d1_iters = 0;
	this->y_iters = 0;
	this->z_iters = 0;

	this->w = NULL;
	this->x = NULL;

    /*this->y = new dst_fmt_t[ARRAY_WIDTH * 2] [(PIPE_REGS + 1) * ARRAY_HEIGHT];
    this->z = new dst_fmt_t[ARRAY_WIDTH] [(PIPE_REGS + 1) * ARRAY_HEIGHT];*/

	memset(this->y, 0, sizeof(this->y));
	memset(this->z, 0, sizeof(this->z));

    /*for (int i = 0; i < ARRAY_WIDTH; i++) {
        for (int j = 0; j < 16; j++) {
            printf("Z [%d,%d]: %x\n", i, j, this->z[i][j]);
        }
    }*/
}

void RedMule_Buffers::alloc_buffers(uint32_t n, uint8_t x_rows_lftovr, uint8_t x_cols_lftovr, uint8_t w_rows_lftovr, uint8_t w_cols_lftovr) {
    this->x = new dst_fmt_t*[ARRAY_WIDTH]; 
    
    for (int i = 0; i < ARRAY_WIDTH; i++) {
        this->x[i] = new dst_fmt_t[n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1) /* sizeof(dst_fmt_t)*/];
    }

    printf("Row of X measures: %d\n", n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1));

                        //[n + 2 * DATA_WIDTH / 8 / sizeof(dst_fmt_t)];//(dst_fmt_t *) malloc(ARRAY_WIDTH * (n + 2 * DATA_WIDTH / 8 / sizeof(dst_fmt_t)) * sizeof(dst_fmt_t));     //Is DATA_WIDTH ok?
    this->w = new dst_fmt_t*[n];//(dst_fmt_t *) malloc(n * sizeof(dst_fmt_t) * (PIPE_REGS + 1) * ARRAY_HEIGHT);

    for (int i = 0; i < n; i++) {
        this->w[i] = new dst_fmt_t[(PIPE_REGS + 1) * ARRAY_HEIGHT];
    }

    this->n = n;

    this->x_cols_lftovr = x_cols_lftovr;
    this->x_rows_lftovr = x_rows_lftovr;
    this->w_cols_lftovr = w_cols_lftovr;
    this->w_rows_lftovr = w_rows_lftovr;

    printf("N is %d\n", n);

    printf("x_cols_lftovr %d x_rows_lftovr %d w_cols_lftovr %d w_rows_lftovr %d",
        x_cols_lftovr,
        x_rows_lftovr,
        w_cols_lftovr,
        w_rows_lftovr);
}

void RedMule_Buffers::free_buffers() {
    /*free(this->x);
    free(this->w);*/

    for (int i = 0; i < ARRAY_WIDTH; i++) {
        delete this->x[i];
    }
    delete this->x;

    for (int i = 0; i < n; i++) {
        delete this->w[i];
    }
    delete this->w;

    this->x_offs = 0;
    this->y_offs = 0;

    memset(this->y, 0, sizeof(this->y));
	memset(this->z, 0, sizeof(this->z));
}

dst_fmt_t* RedMule_Buffers::get_next_w() {
    dst_fmt_t* res = this->w_iters < this->n ? this->w[w_iters] : NULL;//(dst_fmt_t *) ((void *) this->w) + this->w_pointer;

    printf("W: %d\n", this->w_iters);

    this->w_iters++;

    if (this->w_iters == this->n + ((ARRAY_HEIGHT - this->w_rows_lftovr ) % ARRAY_HEIGHT)) {
        this->w_iters = 0;
        //this->w_pointer = 0;
    } /*else {
        this->w_pointer += sizeof(dst_fmt_t) * (PIPE_REGS + 1) * ARRAY_HEIGHT;
    }*/

    printf("W: OK %d\n", this->w_iters);

    return res;
}

dst_fmt_t* RedMule_Buffers::get_next_x() {
    dst_fmt_t* res = &this->x[x_d0_iters][x_pointer];//(dst_fmt_t *) ((void *) this->x) + this->x_pointer;

    printf("X: (%d,%d)\n", this->x_d0_iters, this->x_pointer);

    this->x_d0_iters++;

    if (this->x_d0_iters == ARRAY_WIDTH) {
        this->x_d0_iters = 0;
        this->x_d1_iters++;
        this->x_pointer += ARRAY_HEIGHT * (PIPE_REGS + 1);//DATA_WIDTH / 8 / sizeof(dst_fmt_t);


        /*this->x_pointer -= this->n * sizeof(dst_fmt_t) * (ARRAY_WIDTH - 1);
        this->x_pointer += DATA_WIDTH/8;
        this->x_d0_iters = 0;
        this->x_d1_iters++;*/

        if (this->x_d1_iters == (this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT)) / (/*DATA_WIDTH / 8 / sizeof(dst_fmt_t)*/ARRAY_HEIGHT * (PIPE_REGS + 1)) + 2) {
            this->x_d1_iters = 0;
            this->x_pointer = 0;
        }
    } /*else {
        this->x_pointer += this->n * sizeof(dst_fmt_t);
    }*/

    return res;
}

dst_fmt_t* RedMule_Buffers::get_next_y() {
    dst_fmt_t* res = this->y[y_iters];//(dst_fmt_t *) ((void *) this->y) + this->y_pointer;

    printf("Y: %d\n", this->y_iters);

    this->y_iters++;

    if (this->y_iters == 2 * ARRAY_WIDTH) {
        this->y_pointer = 0;
        this->y_iters = 0;
    } else {
        this->y_pointer += sizeof(dst_fmt_t) * (PIPE_REGS + 1) * ARRAY_HEIGHT;
    }

    /*printf("DUMPING Y:\n");
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 16; j++) {
            printf("0x%x ", * (uint16_t *) &(this->y[this->y_offs + i][j]));
        }
        printf("\n");
    }
    printf("\n\n");*/

    return res;
}

dst_fmt_t* RedMule_Buffers::get_next_z() {
    dst_fmt_t* res = this->z[z_iters];//(dst_fmt_t *) ((void *) this->z) + this->z_pointer;

    printf("Z: %d\n", this->z_iters);

    this->z_iters++;

    if (this->z_iters == ARRAY_WIDTH) {
        this->z_pointer = 0;
        this->z_iters = 0;
    } else {
        this->z_pointer += sizeof(dst_fmt_t) * (PIPE_REGS + 1) * ARRAY_HEIGHT;
    }

    return res;
}

int RedMule_Buffers::x_row_offs(int k) {
    int res = this->x_offs + k;

    if (res >= this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1)/*DATA_WIDTH / 8 / sizeof(dst_fmt_t)*/) {
        res -= this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1)/*DATA_WIDTH / 8 / sizeof(dst_fmt_t)*/;
    }

    //printf("X ROW OFFS: %d\n", res);

    return res;
}

void RedMule_Buffers::compute_z() {

    //Computing stuff...

    /*for (int i = 0; i < ARRAY_WIDTH; i++) {
        for (int j = 0; j < 16; j++) {
            printf("Z [%d,%d]: %x\n", i, j, * (uint16_t *) &(this->z[i][j]));
            printf("Y [%d,%d]: %x\n", i, j, * (uint16_t *) &(this->y[i][j]));
        }
    }*/

    for (int i = 0; i < ARRAY_WIDTH; i++) {
        for (int j = 0; j < (PIPE_REGS + 1) * ARRAY_HEIGHT; j++) {
        #if DST_FMT!=FP8
            float tmp_z;

            tmp_z = (float) this->y[i + this->y_offs][j];

            for (int k = 0; k < this->n; k++) {
                tmp_z = fma((float) this->x[i][this->x_row_offs(k)], (float) this->w[k][j], tmp_z);
            }

            this->z[i][j] = (dst_fmt_t) tmp_z;

            /*this->z[i][j] = this->y[i + this->y_offs][j];
            for (int k = 0; k < this->n; k++) {
                this->z[i][j] += this->x[i][this->x_row_offs(k)] * this->w[k][j];//* (dst_fmt_t *) (((void *) this->x) + i * n * sizeof(dst_fmt_t) + j * sizeof(dst_fmt_t)) * * (dst_fmt_t *) (((void *) this->w) + j * (PIPE_REGS + 1) * ARRAY_HEIGHT * sizeof(dst_fmt_t) + i * sizeof(dst_fmt_t));
                //this->z[i][j] = fma((dst_fmt_t) this->x[i][this->x_row_offs(k)], (dst_fmt_t) this->w[k][j], (dst_fmt_t) this->z[i][j]);
            }*/
        #else
            uint16_t x_buf, w_buf, z_buf;
            float tmp_z, tmp_x, tmp_w;
            _Float16 z16 = 0.0f;

            z_buf = ((uint16_t) this->y[i + this->y_offs][j]) << 8;
            tmp_z = (float) * (_Float16 *) &z_buf;

            for (int k = 0; k < this->n; k++) {
                x_buf = ((uint16_t) this->x[i][this->x_row_offs(k)]) << 8;
                tmp_x = (float) * (_Float16 *) &x_buf;

                w_buf = ((uint16_t) this->w[k][j]) << 8;
                tmp_w = (float) * (_Float16 *) &w_buf;

                tmp_z = fma(tmp_x, tmp_w, tmp_z);
            }

            z16 = (_Float16) tmp_z;
            z_buf = * (uint16_t *) &z16;

            this->z[i][j] = z_buf >> 8;
        #endif
        }
    }


    printf("DUMPING Y:\n");
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 16; j++) {
            printf("0x%x, ", * (uint32_t *) &(this->y[this->y_offs + i][j]));
        }
        printf("\n");
    }
    printf("\n\n");

    printf("DUMPING W:\n");
    for (int i = 0; i < this->n; i++) {
        for (int j = 0; j < 16; j++) {
            printf("0x%x, ", * (uint32_t *) &(this->w[i][j]));
        }
        printf("\n");
    }
    printf("\n\n");

    printf("DUMPING Z:\n");
    for (int i = 0; i < 12; i++) {
        for (int j = 0; j < 16; j++) {
            printf("0x%x, ", * (uint32_t *) &(this->z[i][j]));
        }
        printf("\n");
    }
    printf("\n\n");

    /*printf("SAMPLEDD Y IS %x\n", * (uint16_t *) &(this->y[this->y_offs][0]));
    for (int k = 0; k < this->n; k++) {
        printf("SAMPLED X IS %x (%d)\n", * (uint16_t *) &(this->x[0][this->x_row_offs(k)]), this->x_row_offs(k));
        printf("SAMPLED W is %x\n", * (uint16_t *) &(this->w[k][0]));
    }*/

    /*for (int i = 0; i < ARRAY_WIDTH; i++) {
        for (int j = 0; j < 16; j++) {
            printf("Z [%d,%d]: %x\n", i, j, * (uint16_t *) &(this->z[i][j]));
        }
    }*/

    this->x_offs += this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT);//2 * DATA_WIDTH / 8;

    if (this->x_offs >= this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1)/*DATA_WIDTH / 8 / sizeof(dst_fmt_t)*/) {
        this->x_offs -= this->n + ((PIPE_REGS + 1) * ARRAY_HEIGHT - this->x_cols_lftovr ) % ((PIPE_REGS + 1) * ARRAY_HEIGHT) + 2 * ARRAY_HEIGHT * (PIPE_REGS + 1)/*DATA_WIDTH / 8 / sizeof(dst_fmt_t)*/;
    }

    this->y_offs = this->y_offs == 0 ? ARRAY_WIDTH : 0;
}