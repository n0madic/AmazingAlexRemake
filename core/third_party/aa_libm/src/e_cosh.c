
/* @(#)e_cosh.c 1.3 95/01/18 */
/*
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice 
 * is preserved.
 * ====================================================
 */

#ifndef lint
static char rcsid[] = "$FreeBSD: src/lib/msun/src/e_cosh.c,v 1.8 2005/02/04 18:26:05 das Exp $";
#endif

/* aa_cosh(x)
 * Method : 
 * mathematically aa_cosh(x) if defined to be (aa_exp(x)+aa_exp(-x))/2
 *	1. Replace x by |x| (aa_cosh(x) = aa_cosh(-x)). 
 *	2. 
 *		                                        [ aa_exp(x) - 1 ]^2 
 *	    0        <= x <= ln2/2  :  aa_cosh(x) := 1 + -------------------
 *			       			           2*aa_exp(x)
 *
 *		                                  aa_exp(x) +  1/aa_exp(x)
 *	    ln2/2    <= x <= 22     :  aa_cosh(x) := -------------------
 *			       			          2
 *	    22       <= x <= lnovft :  aa_cosh(x) := aa_exp(x)/2 
 *	    lnovft   <= x <= ln2ovft:  aa_cosh(x) := aa_exp(x/2)/2 * aa_exp(x/2)
 *	    ln2ovft  <  x	    :  aa_cosh(x) := huge*huge (overflow)
 *
 * Special cases:
 *	aa_cosh(x) is |x| if x is +INF, -INF, or NaN.
 *	only aa_cosh(0)=1 is exact for finite x.
 */

#include "math.h"
#include "math_private.h"

static const double one = 1.0, half=0.5, huge = 1.0e300;

double
aa_cosh(double x)
{
	double t,w;
	int32_t ix;
	u_int32_t lx;

    /* High word of |x|. */
	GET_HIGH_WORD(ix,x);
	ix &= 0x7fffffff;

    /* x is INF or NaN */
	if(ix>=0x7ff00000) return x*x;	

    /* |x| in [0,0.5*ln2], return 1+aa_expm1(|x|)^2/(2*aa_exp(|x|)) */
	if(ix<0x3fd62e43) {
	    t = aa_expm1(fabs(x));
	    w = one+t;
	    if (ix<0x3c800000) return w;	/* aa_cosh(tiny) = 1 */
	    return one+(t*t)/(w+w);
	}

    /* |x| in [0.5*ln2,22], return (aa_exp(|x|)+1/aa_exp(|x|)/2; */
	if (ix < 0x40360000) {
		t = aa_exp(fabs(x));
		return half*t+half/t;
	}

    /* |x| in [22, aa_log(maxdouble)] return half*aa_exp(|x|) */
	if (ix < 0x40862E42)  return half*aa_exp(fabs(x));

    /* |x| in [aa_log(maxdouble), overflowthresold] */
	GET_LOW_WORD(lx,x);
	if (ix<0x408633CE ||
	      ((ix==0x408633ce)&&(lx<=(u_int32_t)0x8fb9f87d))) {
	    w = aa_exp(half*fabs(x));
	    t = half*w;
	    return t*w;
	}

    /* |x| > overflowthresold, aa_cosh(x) overflow */
	return huge*huge;
}
