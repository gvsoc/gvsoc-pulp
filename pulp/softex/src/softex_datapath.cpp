/*
* Copyright (C) 2026 ETH Zurich, University of Bologna, and Fondazione Chips-IT
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
*
* Authors:  Alessandro Nadalini <alessandro.nadalini3@unibo.it>
*/

#include "softex.hpp"
#include <cmath>
#include <cstring>

namespace
{
    /**********************************************************************
    * Accumulation tree
    * Mirrors softex_fp_add_rec.sv's recursive pairwise-tree reduction at
    * FP32 (ACC format): split into A = first half (rounded up)
    * and B = the rest, reduce each half recursively, then add the two
    * partial sums. The RTL always builds this tree at the full
    * SOFTEX_N_ROWS width, with inactive (strobe-low) lanes forced to 0.0
    * before the add; since adding 0.0 is always exact in IEEE arithmetic,
    * pre-zeroing `vals` beyond a beat's valid element count (see
    * complete_acc_beat()) reproduces the exact same tree topology -- and
    * therefore the same rounding -- as the real hardware, including for a
    * stream's final, partial beat.
    **********************************************************************/
    
    flexfloat_t acc_tree_add(const flexfloat_t *vals, int lo, int n_inp)
    {
        if (n_inp == 1)
        {
            return vals[lo];
        }

        int a_width = (n_inp + 1) / 2;
        int b_width = n_inp - a_width;

        flexfloat_t a = acc_tree_add(vals, lo, a_width);
        flexfloat_t b = acc_tree_add(vals, lo + a_width, b_width);

        flexfloat_t res;
        ff_init(&res, SoftexFormat::acc());
        ff_add(&res, &a, &b);
        return res;
    }
}

void Softex::complete_acc_beat(const uint8_t *buf, uint32_t valid_bytes, uint32_t beat_idx)
{
    int elem_size = this->job.cast_input() ? 1 : 2;
    int n_elem = valid_bytes / elem_size;

    // softex_fp_glob_minmax.sv reduces an entire incoming beat (up to
    // SOFTEX_N_ROWS elements) down to a single block max before
    // comparing it against the running max, so a beat fires at most one
    // rescale event -- never one per element. Max/min comparisons don't
    // round, so a plain scan over the beat's valid elements lands on
    // exactly the same block max the RTL's reduction tree would, without
    // needing to replicate that tree's shape (unlike the sum below).
    flexfloat_t block_max = this->cast_in_to_ff(buf, 0, this->job.cast_input());
    for (int i = 1; i < n_elem; i++)
    {
        flexfloat_t x = this->cast_in_to_ff(buf, i, this->job.cast_input());
        if (ff_gt(&x, &block_max))
        {
            block_max = x;
        }
    }

    // All comparisons/arithmetic are done at the RTL's actual formats via
    // flexfloat: max tracking in FP16ALT, the running sum in FP32, and the
    // exponential itself is softex's approximate hardware exp unit
    // (expu_exp: schraudolph_exp() + its mandatory correction stage)
    if (ff_gt(&block_max, &this->running_max))
    {
        flexfloat_t diff;
        ff_init(&diff, SoftexFormat::in());
        ff_sub(&diff, &this->running_max, &block_max); // old_max - new_max (<=0, or -inf on the first beat)

        flexfloat_t rescale16 = this->expu_exp(diff);
        flexfloat_t rescale32;
        ff_init(&rescale32, SoftexFormat::acc());
        ff_cast(&rescale32, &rescale16, SoftexFormat::acc());

        flexfloat_t new_sum;
        ff_init(&new_sum, SoftexFormat::acc());
        ff_mul(&new_sum, &this->running_sum, &rescale32);
        this->running_sum = new_sum;

        this->running_max = block_max;
    }

    // softex_fp_red_sum.sv casts each of the beat's exp() results up to
    // FP32 individually, then reduces all SOFTEX_N_ROWS lanes (zero-padded
    // beyond n_elem, exactly like the RTL's strobe-masked lanes) through a
    // pairwise tree -- only that single per-beat partial sum is added into
    // the running accumulator, not one accumulator add per element.
    flexfloat_t vals[SOFTEX_N_ROWS];
    for (int i = 0; i < SOFTEX_N_ROWS; i++)
    {
        if (i < n_elem)
        {
            flexfloat_t x = this->cast_in_to_ff(buf, i, this->job.cast_input());

            flexfloat_t diff2;
            ff_init(&diff2, SoftexFormat::in());
            ff_sub(&diff2, &x, &this->running_max); // <= 0, so schraudolph_exp never has to saturate high

            flexfloat_t e16 = this->expu_exp(diff2);
            ff_init(&vals[i], SoftexFormat::acc());
            ff_cast(&vals[i], &e16, SoftexFormat::acc());
        }
        else
        {
            ff_init_double(&vals[i], 0.0, SoftexFormat::acc());
        }
    }

    flexfloat_t beat_sum = acc_tree_add(vals, 0, SOFTEX_N_ROWS);

    flexfloat_t new_sum2;
    ff_init(&new_sum2, SoftexFormat::acc());
    ff_add(&new_sum2, &this->running_sum, &beat_sum);
    this->running_sum = new_sum2;

}

// Computes the normalized output for one DIVIDING beat into `outbuf`
// (caller-provided, SOFTEX_N_ROWS*2 bytes) and returns its valid byte
// count. Only computes the numerics -- streaming the result back out is
// stream_advance_beat()'s job (softex_stream.cpp).
uint32_t Softex::complete_div_read_beat(const uint8_t *buf, uint32_t valid_bytes, uint32_t beat_idx, uint8_t *outbuf)
{
    int elem_size_in = this->job.cast_input() ? 1 : 2;
    int elem_size_out = this->job.cast_output() ? 1 : 2;
    int n_elem = valid_bytes / elem_size_in;

    memset(outbuf, 0, SOFTEX_N_ROWS * 2);

    // softex_datapath.sv's i_inv_cast narrows the FP32 reciprocal down to
    // FP16ALT before the normalization multiply -- the multiply itself
    // (softex_fp_vect_addmul's FMA, FPFORMAT = IN_FPFORMAT) is a
    // single-format unit that runs entirely at FP16ALT precision, not a
    // widen-multiply-then-narrow at FP32. Cast once per beat (the
    // reciprocal doesn't change during DIVIDING) rather than per element.
    flexfloat_t recip16;
    ff_init(&recip16, SoftexFormat::in());
    ff_cast(&recip16, &this->reciprocal, SoftexFormat::in());

    for (int i = 0; i < n_elem; i++)
    {
        flexfloat_t x = this->cast_in_to_ff(buf, i, this->job.cast_input());

        flexfloat_t diff;
        ff_init(&diff, SoftexFormat::in());
        ff_sub(&diff, &x, &this->running_max);

        flexfloat_t e16 = this->expu_exp(diff);

        flexfloat_t y16;
        ff_init(&y16, SoftexFormat::in());
        ff_mul(&y16, &e16, &recip16);

        this->cast_out_from_ff(outbuf, i, y16, this->job.cast_output());
    }

    return (uint32_t)(n_elem * elem_size_out);
}

/**************************************************************************
* BF16 (bfloat16: 1 sign, 8 exp, 7 mantissa) <-> FP32 conversions
**************************************************************************/

float Softex::bf16_to_f32(uint16_t bits)
{
    uint32_t widened = ((uint32_t)bits) << 16;
    float f;
    memcpy(&f, &widened, sizeof(f));
    return f;
}

uint16_t Softex::f32_to_bf16(float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    // round-to-nearest-even truncation to the top 16 bits
    uint32_t rounded = bits + 0x7FFF + ((bits >> 16) & 1);
    return (uint16_t)(rounded >> 16);
}

/**************************************************************************
* Approximate exponential (rtl/expu/expu_schraudolph.sv) and
* Newton-Raphson reciprocal, via flexfloat
**************************************************************************/

// Classic Schraudolph (1999) fast exponential: exp(x) is approximated by
// reinterpreting round(x * 2^frac_bits/ln(2)) + bias*2^frac_bits directly
// as the raw bits of a float in the target format. This is exactly what
// rtl/expu/expu_schraudolph.sv computes (in fixed-point, for efficiency);
// doing it here in real arithmetic and packing the result through
// flexfloat's bit accessors gives the same approximate result -- same
// several-percent-scale error as the real hardware exp, not an exact
// exp() -- without needing to replicate the RTL's internal fixed-point
// bit widths. The real hardware never uses this raw result on its own --
// see expu_correction()/expu_exp() below for the mandatory correction
// stage applied on top of it.
flexfloat_t Softex::schraudolph_exp(flexfloat_t x) const
{
    const flexfloat_desc_t desc = SoftexFormat::in();
    const int frac_bits = desc.frac_bits;   // 7
    const int exp_bits = desc.exp_bits;     // 8
    const long bias = (1L << (exp_bits - 1)) - 1;
    const long max_biased_exp = (1L << exp_bits) - 1; // all-ones = inf pattern

    flexfloat_t result;
    ff_init(&result, desc);

    double xv = ff_get_double(&x);

    if (!std::isfinite(xv))
    {
        // -inf (e.g. the very first accumulation step, old_max == -inf)
        // saturates to 0, matching expu_schraudolph.sv's underflow branch.
        flexfloat_set_bits(&result, 0);
        return result;
    }

    static const double A = std::exp2((double)frac_bits) / std::log(2.0);
    long raw = std::lround(xv * A) + (bias << frac_bits);

    if (raw <= 0)
    {
        flexfloat_set_bits(&result, 0); // underflow to zero
    }
    else if (raw >= (max_biased_exp << frac_bits))
    {
        // overflow: saturate to +inf, matching the RTL's ovfr branch.
        // (softmax inputs are always <= running max after subtraction, so
        // this path isn't reachable in normal operation -- it's here only
        // for robustness against callers feeding schraudolph_exp a raw,
        // un-subtracted value.)
        flexfloat_set_bits(&result, (uint32_t)(max_biased_exp << frac_bits));
    }
    else
    {
        flexfloat_set_bits(&result, (uint32_t)raw);
    }

    return result;
}

// Piecewise-quadratic mantissa correction (rtl/expu/expu_correction.sv),
// applied unconditionally after schraudolph_exp() by the real hardware's
// expu_row.sv (EXPU_ENABLE_MANT_CORRECTION defaults to 1). This is an
// exact translation of the Verilog's integer bit widths and shifts --
// not a real-arithmetic reformulation -- because the branch constants
// are already narrow fixed-point roundings baked in by softex_pkg.sv's
// own parameter expressions (SystemVerilog's real->int cast rounds to
// nearest, ties away from zero):
//   EXPU_ALPHA_FIXED   = round(0.24609375 * 2^4) = 4    (not 0.24609375)
//   EXPU_BETA_FIXED    = round(0.41015625 * 2^4) = 7    (not 0.41015625)
//   EXPU_GAMMA_1_FIXED = round(2.8359375  * 2^7) = 363  (exact)
//   EXPU_GAMMA_2_FIXED = round(2.16796875 * 2^7) = 278  (not 277)
// Reproducing the *real-valued* constants instead of these already-
// quantized integers would silently compute a different (wrong) curve.
// Spot-checked at f=0.25 and f=0.5 (the hardest point for the raw linear
// estimate): this correction recovers 2^0.25 and 2^0.5 to within
// ~0.02%, versus several-percent error from schraudolph_exp() alone --
// but it has not been checked against an actual RTL simulation run.
flexfloat_t Softex::expu_correction(flexfloat_t sch_result) const
{
    const flexfloat_desc_t desc = SoftexFormat::in();
    const int mant_bits = desc.frac_bits; // 7
    const int exp_bits = desc.exp_bits;   // 8

    // softex_pkg.sv defaults (EXPU_COEFFICIENT_FRACTION, EXPU_CONSTANT_FRACTION,
    // EXPU_MUL_SURPLUS_BITS, EXPU_NOT_SURPLUS_BITS, and the four *_FIXED
    // constants derived from them) -- independent of FPFORMAT_IN's width.
    const int COEFFICIENT_FRACTION = 4;
    const int CONSTANT_FRACTION = 7;
    const int MUL_SURPLUS_BITS = 1;
    const int NOT_SURPLUS_BITS = 0;
    const uint32_t ALPHA_FIXED = 4;
    const uint32_t BETA_FIXED = 7;
    const uint32_t GAMMA_1_FIXED = 363;
    const uint32_t GAMMA_2_FIXED = 278;

    const int sum_fraction = (mant_bits > CONSTANT_FRACTION) ? mant_bits : CONSTANT_FRACTION;
    const int shift_amount = sum_fraction + COEFFICIENT_FRACTION + MUL_SURPLUS_BITS - NOT_SURPLUS_BITS;

    uint32_t bits = (uint32_t)flexfloat_get_bits(&sch_result);
    uint32_t exponent = (bits >> mant_bits) & ((1u << exp_bits) - 1);
    uint32_t frac = bits & ((1u << mant_bits) - 1);
    bool hi_half = (frac >> (mant_bits - 1)) & 1u; // mantissa[MANTISSA_BITS-1] -- branch select

    uint32_t mant_mul_1_mask = (1u << (mant_bits - 1 + MUL_SURPLUS_BITS)) - 1;
    uint32_t mant_mul_1, alpha_beta;
    if (!hi_half)
    {
        mant_mul_1 = (frac << MUL_SURPLUS_BITS) & mant_mul_1_mask;
        alpha_beta = ALPHA_FIXED;
    }
    else
    {
        uint32_t full_width = mant_bits + MUL_SURPLUS_BITS;
        uint32_t compl_full = (~(frac << MUL_SURPLUS_BITS)) & ((1u << full_width) - 1);
        mant_mul_1 = compl_full & mant_mul_1_mask;
        alpha_beta = BETA_FIXED;
    }

    uint32_t res_mul_1 = mant_mul_1 * alpha_beta;

    uint32_t mant_add_1 = (frac << (sum_fraction - mant_bits)) & ((1u << sum_fraction) - 1);
    uint32_t gamma = hi_half ? GAMMA_2_FIXED : GAMMA_1_FIXED;
    uint32_t gamma_add_1 = (gamma << (sum_fraction - CONSTANT_FRACTION)) & ((1u << (sum_fraction + 2)) - 1);
    uint32_t res_add_1 = mant_add_1 + gamma_add_1;

    uint64_t res_mul_2 = (uint64_t)res_mul_1 * (uint64_t)res_add_1;

    uint32_t pre_inv_mask = (1u << (mant_bits + NOT_SURPLUS_BITS)) - 1;
    uint32_t res_pre_inversion = (uint32_t)(res_mul_2 >> shift_amount) & pre_inv_mask;

    uint32_t corrected_mantissa = (!hi_half ? res_pre_inversion : (~res_pre_inversion & pre_inv_mask)) >> NOT_SURPLUS_BITS;
    corrected_mantissa &= (1u << mant_bits) - 1;

    uint32_t out_bits = (exponent << mant_bits) | corrected_mantissa; // sign always 0: exp(x<=0) is in (0,1]

    flexfloat_t result;
    ff_init(&result, desc);
    flexfloat_set_bits(&result, out_bits);
    return result;
}

flexfloat_t Softex::expu_exp(flexfloat_t x) const
{
    return this->expu_correction(this->schraudolph_exp(x));
}

// Newton-Raphson reciprocal: x_{n+1} = x_n * (2 - d*x_n), seeded from an
// ordinary double division (a stand-in for whatever fast-seed bit-trick
// the real inv_appr stage uses) and refined with N_NEWTON_ITERS steps of
// correctly-rounded FP32 flexfloat arithmetic. Two Newton iterations from
// an already-close seed converge far beyond FP32 precision, so this ends
// up correctly rounded in practice -- the iteration is kept explicit
// (rather than just calling a one-shot correctly-rounded divide) to stay
// structurally close to the RTL's N_NEWTON_ITERS-iteration algorithm.
flexfloat_t Softex::newton_reciprocal(flexfloat_t denom) const
{
    flexfloat_desc_t acc = SoftexFormat::acc();

    flexfloat_t result;
    ff_init(&result, acc);

    double d = ff_get_double(&denom);
    if (!(d > 0.0))
    {
        ff_init_double(&result, 0.0, acc);
        return result;
    }

    flexfloat_t x;
    ff_init_double(&x, 1.0 / d, acc);

    flexfloat_t two;
    ff_init_double(&two, 2.0, acc);

    for (int iter = 0; iter < SoftexLatency::N_NEWTON_ITERS; iter++)
    {
        flexfloat_t t;
        ff_init(&t, acc);
        ff_mul(&t, &denom, &x); // d * x_n

        flexfloat_t t2;
        ff_init(&t2, acc);
        ff_sub(&t2, &two, &t); // 2 - d*x_n

        flexfloat_t xn;
        ff_init(&xn, acc);
        ff_mul(&xn, &x, &t2); // x_n * (2 - d*x_n)

        x = xn;
    }

    return x;
}

// ---------------------------------------------------------------------
// Fixed-point <-> float cast (softex_cast_in.sv / softex_cast_out.sv)
//
// int_bits is the number of integer bits in a signed/unsigned Q-format
// value packed into INT_W=8 bits total (CAST_CTRL register, see
// archi_softex.h). Fixed-point values are exactly representable in
// double, so the only rounding step is the final ff_init_double() into
// FP16ALT below -- that rounding step itself IS modeled correctly (unlike
// the previous version of this model, which stayed in double past this
// point). The scale-factor arithmetic otherwise is a *functional*
// approximation of the RTL's cast (not bit-matched to the fixed-point
// RTL casters' own internal rounding); validate against
// softex/golden-model/golden.py if you need bit-exact casts.
// ---------------------------------------------------------------------

flexfloat_t Softex::cast_in_to_ff(const uint8_t *raw, int byte_idx, bool is_int) const
{
    flexfloat_t result;
    ff_init(&result, SoftexFormat::in());

    if (!is_int)
    {
        uint16_t bits;
        memcpy(&bits, raw + byte_idx * 2, sizeof(bits));
        flexfloat_set_bits(&result, bits); // already FP16ALT-encoded in memory
        return result;
    }

    const int INT_W = 8;
    int int_bits = this->job.in_int_bits();
    int frac_bits = INT_W - int_bits - (this->job.in_signed() ? 1 : 0);
    double scale = std::pow(2.0, -frac_bits);

    double value;
    if (this->job.in_signed())
    {
        int8_t v = (int8_t)raw[byte_idx];
        value = (double)v * scale;
    }
    else
    {
        uint8_t v = raw[byte_idx];
        value = (double)v * scale;
    }

    ff_init_double(&result, value, SoftexFormat::in());
    return result;
}

void Softex::cast_out_from_ff(uint8_t *raw, int byte_idx, flexfloat_t value, bool is_int) const
{
    if (!is_int)
    {
        uint16_t bits = (uint16_t)flexfloat_get_bits(&value);
        memcpy(raw + byte_idx * 2, &bits, sizeof(bits));
        return;
    }

    double v = ff_get_double(&value);

    const int INT_W = 8;
    int int_bits = this->job.out_int_bits();
    int frac_bits = INT_W - int_bits - (this->job.out_signed() ? 1 : 0);
    double scale = std::pow(2.0, frac_bits);

    double scaled = std::round(v * scale);

    if (this->job.out_signed())
    {
        if (scaled > 127.0) scaled = 127.0;
        if (scaled < -128.0) scaled = -128.0;
        raw[byte_idx] = (uint8_t)(int8_t)scaled;
    }
    else
    {
        if (scaled > 255.0) scaled = 255.0;
        if (scaled < 0.0) scaled = 0.0;
        raw[byte_idx] = (uint8_t)scaled;
    }
}
