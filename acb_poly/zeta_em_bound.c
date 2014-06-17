/*=============================================================================

    This file is part of ARB.

    ARB is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    ARB is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ARB; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA

=============================================================================*/
/******************************************************************************

    Copyright (C) 2014 Fredrik Johansson

******************************************************************************/

#include "acb_poly.h"


static void
bound_I(arb_ptr I, const arb_t A, const arb_t B, const arb_t C, long len, long wp)
{
    long k;

    arb_t D, Dk, L, T, Bm1;

    arb_init(D);
    arb_init(Dk);
    arb_init(Bm1);
    arb_init(T);
    arb_init(L);

    arb_sub_ui(Bm1, B, 1, wp);
    arb_one(L);

    /* T = 1 / (A^Bm1 * Bm1) */
    arb_inv(T, A, wp);
    arb_pow(T, T, Bm1, wp);
    arb_div(T, T, Bm1, wp);

    if (len > 1)
    {
        arb_log(D, A, wp);
        arb_add(D, D, C, wp);
        arb_mul(D, D, Bm1, wp);
        arb_set(Dk, D);
    }

    for (k = 0; k < len; k++)
    {
        if (k > 0)
        {
            arb_mul_ui(L, L, k, wp);
            arb_add(L, L, Dk, wp);
            arb_mul(Dk, Dk, D, wp);
        }

        arb_mul(I + k, L, T, wp);
        arb_div(T, T, Bm1, wp);
    }

    arb_clear(D);
    arb_clear(Dk);
    arb_clear(Bm1);
    arb_clear(T);
    arb_clear(L);
}

/* 0.5*(B/AN)^2 + |B|/AN */
static void
bound_C(arb_t C, const arb_t AN, const arb_t B, long wp)
{
    arb_t t;
    arb_init(t);
    arb_abs(t, B);
    arb_div(t, t, AN, wp);
    arb_mul_2exp_si(C, t, -1);
    arb_add_ui(C, C, 1, wp);
    arb_mul(C, C, t, wp);
    arb_clear(t);
}

static void
bound_K(arb_t C, const arb_t AN, const arb_t B, const arb_t T, long wp)
{
    if (arb_is_zero(B) || arb_is_zero(T))
    {
        arb_one(C);
    }
    else
    {
        arb_div(C, B, AN, wp);
        /* TODO: atan is dumb, should also bound by pi/2 */
        arb_atan(C, C, wp);
        arb_mul(C, C, T, wp);
        if (arb_is_nonpositive(C))
            arb_one(C);
        else
            arb_exp(C, C, wp);
    }
}

/* Absolute value of rising factorial (could speed up once complex gamma is available). */
void
acb_rfac_abs_ubound2(arf_t bound, const acb_t s, ulong n, long prec)
{
    arf_t term, t;
    ulong k;

    /* M(k) = (a+k)^2 + b^2
       M(0) = a^2 + b^2
       M(k+1) = M(k) + 2*a + (2*k+1)
    */
    arf_init(t);
    arf_init(term);

    arf_one(bound);

    /* M(0) = a^2 + b^2 */
    arb_get_abs_ubound_arf(t, acb_realref(s), prec);
    arf_mul(term, t, t, prec, ARF_RND_UP);
    arb_get_abs_ubound_arf(t, acb_imagref(s), prec);
    arf_mul(t, t, t, prec, ARF_RND_UP);
    arf_add(term, term, t, prec, ARF_RND_UP);

    /* we add t = 2*a to each term. note that this can be signed;
       we always want the most positive value */
    arf_set_mag(t, arb_radref(acb_realref(s)));
    arf_add(t, arb_midref(acb_realref(s)), t, prec, ARF_RND_CEIL);
    arf_mul_2exp_si(t, t, 1);

    for (k = 0; k < n; k++)
    {
        arf_mul(bound, bound, term, prec, ARF_RND_UP);
        arf_add_ui(term, term, 2 * k + 1, prec, ARF_RND_UP);
        arf_add(term, term, t, prec, ARF_RND_UP);
    }

    arf_sqrt(bound, bound, prec, ARF_RND_UP);

    arf_clear(t);
    arf_clear(term);
}


static void
bound_rfac(arb_ptr F, const acb_t s, ulong n, long len, long wp)
{
    if (len == 1)
    {
        acb_rfac_abs_ubound2(arb_midref(F + 0), s, n, wp);
        mag_zero(arb_radref(F + 0));
    }
    else
    {
        arb_struct sx[2];
        arb_init(sx + 0);
        arb_init(sx + 1);
        acb_abs(sx + 0, s, wp);
        arb_one(sx + 1);
        _arb_vec_zero(F, len);
        _arb_poly_rising_ui_series(F, sx, 2, n, len, wp);
        arb_clear(sx + 0);
        arb_clear(sx + 1);
    }
}

void
_acb_poly_zeta_em_bound(arb_ptr bound, const acb_t s, const acb_t a, ulong N, ulong M, long len, long wp)
{
    arb_t K, C, AN, S2M;
    arb_ptr F, R;
    long k;

    arb_srcptr alpha = acb_realref(a);
    arb_srcptr beta  = acb_imagref(a);
    arb_srcptr sigma = acb_realref(s);
    arb_srcptr tau   = acb_imagref(s);

    arb_init(AN);
    arb_init(S2M);

    /* require alpha + N > 1, sigma + 2M > 1 */
    arb_add_ui(AN, alpha, N - 1, wp);
    arb_add_ui(S2M, sigma, 2*M - 1, wp);

    if (!arb_is_positive(AN) || !arb_is_positive(S2M) || N < 1 || M < 1)
    {
        arb_clear(AN);
        arb_clear(S2M);

        for (k = 0; k < len; k++)
            arb_pos_inf(bound + k);

        return;
    }

    /* alpha + N, sigma + 2M */
    arb_add_ui(AN, AN, 1, wp);
    arb_add_ui(S2M, S2M, 1, wp);

    R = _arb_vec_init(len);
    F = _arb_vec_init(len);

    arb_init(K);
    arb_init(C);

    /* bound for power integral */
    bound_C(C, AN, beta, wp);
    bound_K(K, AN, beta, tau, wp);
    bound_I(R, AN, S2M, C, len, wp);

    for (k = 0; k < len; k++)
    {
        arb_mul(R + k, R + k, K, wp);
        arb_div_ui(K, K, k + 1, wp);
    }

    /* bound for rising factorial */
    bound_rfac(F, s, 2*M, len, wp);

    /* product (TODO: only need upper bound; write a function for this) */
    _arb_poly_mullow(bound, F, len, R, len, len, wp);

    /* bound for bernoulli polynomials, 4 / (2pi)^(2M) */
    arb_const_pi(C, wp);
    arb_mul_2exp_si(C, C, 1);
    arb_pow_ui(C, C, 2 * M, wp);
    arb_ui_div(C, 4, C, wp);
    _arb_vec_scalar_mul(bound, bound, len, C, wp);

    arb_clear(K);
    arb_clear(C);
    arb_clear(AN);
    arb_clear(S2M);

    _arb_vec_clear(R, len);
    _arb_vec_clear(F, len);
}

void
_acb_poly_zeta_em_bound1(arf_t bound,
        const acb_t s, const acb_t a, long N, long M, long len, long wp)
{
    arb_ptr vec = _arb_vec_init(len);
    _acb_poly_zeta_em_bound(vec, s, a, N, M, len, wp);
    _arb_vec_get_abs_ubound_arf(bound, vec, len, wp);
    _arb_vec_clear(vec, len);
}
