// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

// B-spline validity checks and their stable diagnostic indices live together.
// This is the implementation of the existing SmBSplineCurve interface; no
// validation state or alternate caller interpretation is introduced here.
#include "StdAfx.h"

#include <SmAssertArray.h>
#include <SmBSplineCurve.h>
#include <nurbs.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#if defined(_M_X64) || defined(__SSE2__)
#include <xmmintrin.h>
#endif

#ifdef SM_DEBUG_CODE
#include <SmBrep.h>
#include <SmCrvOnSurf.h>
#include <SmEdgeuse.h>
#include <SmFace.h>
#endif

namespace
{
struct RegularityProof
{
    bool speed = false;
    bool radius = false;
};

// A proof only enables an early pass. It never rejects a curve, modifies its
// representation, or supplies a fabricated worst-case parameter for a report.
// Unsupported arithmetic environments and inconclusive bounds use the GPA path.
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
#    ifdef _MSC_VER
#        pragma float_control(precise, on, push)
#    endif
struct Interval
{
    double lo = 0;
    double hi = 0;

    bool finite() const
    {
        return std::isfinite(lo) && std::isfinite(hi) && lo <= hi;
    }
};

double lower(double value)
{
    return std::nextafter(value, -std::numeric_limits<double>::infinity());
}
double upper(double value)
{
    return std::nextafter(value, std::numeric_limits<double>::infinity());
}
Interval point(double value)
{
    return { value, value };
}
Interval operator+(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return { 1, -1 };
    }
    return { lower(a.lo + b.lo), upper(a.hi + b.hi) };
}
Interval operator-(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return { 1, -1 };
    }
    return { lower(a.lo - b.hi), upper(a.hi - b.lo) };
}
Interval operator*(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return { 1, -1 };
    }
    const std::array values{ a.lo * b.lo, a.lo * b.hi, a.hi * b.lo, a.hi * b.hi };
    return { lower(*std::min_element(values.begin(), values.end())), upper(*std::max_element(values.begin(), values.end())) };
}
Interval operator/(Interval a, Interval positive)
{
    if (!positive.finite() || positive.lo <= 0)
    {
        return { 1, -1 };
    }
    return a * Interval{ lower(1.0 / positive.hi), upper(1.0 / positive.lo) };
}
Interval hull(Interval a, Interval b)
{
    if (!a.finite() || !b.finite())
    {
        return { 1, -1 };
    }
    return { std::min(a.lo, b.lo), std::max(a.hi, b.hi) };
}

using Pole = std::array<Interval, 4>;
using Bezier = std::array<Pole, 4>;

RegularityProof boundBezier(const Bezier& poles, int degree, int dimension, Interval length, double tolerance)
{
    // Translate homogeneous coordinates before taking hulls to reduce interval
    // dependency on a large common origin. The origin may be any finite double:
    // subtraction and multiplication below enclose the exact translated curve.
    Bezier translated = poles;
    for (int axis = 0; axis < dimension; ++axis)
    {
        const double origin = poles[0][axis].lo / poles[0][3].lo;
        if (!std::isfinite(origin))
        {
            return {};
        }
        for (int i = 0; i <= degree; ++i)
        {
            translated[i][axis] = poles[i][axis] - point(origin) * poles[i][3];
        }
    }

    Pole position = translated[0];
    Pole first{}, second{};
    Bezier differences{};
    for (int axis = 0; axis < 4; ++axis)
    {
        for (int i = 1; i <= degree; ++i)
        {
            position[axis] = hull(position[axis], translated[i][axis]);
        }
        for (int i = 0; i < degree; ++i)
        {
            differences[i][axis] = (translated[i + 1][axis] - translated[i][axis]) * point(degree) / length;
            first[axis] = i == 0 ? differences[i][axis] : hull(first[axis], differences[i][axis]);
        }
        for (int i = 0; i < degree - 1; ++i)
        {
            const Interval value = (differences[i + 1][axis] - differences[i][axis]) * point(degree - 1) / length;
            second[axis] = i == 0 ? value : hull(second[axis], value);
        }
        if (!position[axis].finite() || !first[axis].finite() || !second[axis].finite())
        {
            return {};
        }
    }
    if (position[3].lo <= 0)
    {
        return {};
    }

    double speedLower = 0;
    double accelerationUpper = 0;
    for (int axis = 0; axis < dimension; ++axis)
    {
        // H=(X,w), C=X/w. Differentiate w*C=X twice, retaining the
        // ORIGINAL knot parameter: C'=(X'-w'*C)/w,
        // C''=(X''-2*w'*C'-w''*C)/w. Bezier hulls cover every parameter,
        // including endpoints; this is not a sampled error estimate.
        const Interval coordinate = position[axis] / position[3];
        const Interval velocity = (first[axis] - first[3] * coordinate) / position[3];
        const Interval acceleration = (second[axis] - point(2) * first[3] * velocity - second[3] * coordinate) / position[3];
        if (!coordinate.finite() || !velocity.finite() || !acceleration.finite())
        {
            return {};
        }
        speedLower = std::max(speedLower, std::max(velocity.lo, -velocity.hi));
        accelerationUpper = upper(accelerationUpper + std::max(std::abs(acceleration.lo), std::abs(acceleration.hi)));
    }
    // Infinity norm is a lower bound for speed; 1-norm is an upper bound for
    // acceleration. rho=|C'|^3/|C' x C''| >= |C'|^2/|C''| in both 2D and 3D.
    // Cross-multiply with outward rounding; straight lines need no division by 0.
    const bool speed = speedLower >= tolerance;
    const bool radius = speed && std::isfinite(accelerationUpper) && lower(speedLower * speedLower) >= upper(tolerance * accelerationUpper);
    return { speed, radius };
}

RegularityProof subdivideBezier(const Bezier& poles, int degree, int dimension, Interval length, double tolerance, int depth)
{
    const RegularityProof whole = boundBezier(poles, degree, dimension, length, tolerance);
    if ((whole.speed && whole.radius) || depth == 0)
    {
        return whole;
    }
    // Bounded work: at most eight subintervals. Positive rational Bezier
    // weights remain positive under de Casteljau; every operation is enclosed.
    Bezier work = poles, left{}, right{};
    left[0] = work[0];
    right[degree] = work[degree];
    for (int level = 1; level <= degree; ++level)
    {
        for (int i = 0; i <= degree - level; ++i)
        {
            for (int axis = 0; axis < 4; ++axis)
            {
                work[i][axis] = (work[i][axis] + work[i + 1][axis]) * point(0.5);
            }
        }
        left[level] = work[0];
        right[degree - level] = work[degree - level];
    }
    const Interval halfLength = length * point(0.5);
    const RegularityProof a = subdivideBezier(left, degree, dimension, halfLength, tolerance, depth - 1);
    const RegularityProof b = subdivideBezier(right, degree, dimension, halfLength, tolerance, depth - 1);
    return { whole.speed || (a.speed && b.speed), whole.radius || (a.radius && b.radius) };
}
#    ifdef _MSC_VER
#        pragma float_control(pop)
#    endif
#endif

RegularityProof proveBezierRegularity(const gw_CURVE* curve, ULONG dimension, double tolerance)
{
#if (defined(_M_X64) || defined(__SSE2__)) && !defined(__FAST_MATH__) && !defined(_M_FP_FAST) &&                                                     \
    (!defined(__FINITE_MATH_ONLY__) || __FINITE_MATH_ONLY__ == 0)
    // FTZ/DAZ would invalidate outward rounding near underflow. Do not change
    // caller floating-point state. Other architectures conservatively fall back.
    const unsigned arithmeticState = _mm_getcsr();
    if ((arithmeticState & ((1U << 15) | (1U << 6))) != 0 || (arithmeticState & 0x1F80U) != 0x1F80U || !std::numeric_limits<double>::is_iec559 ||
        !curve || !curve->pol || !curve->knt || dimension < 2 || dimension > 3 || !std::isfinite(tolerance) || tolerance <= 0)
    {
        return {};
    }
    const int degree = curve->p;
    // Accept a bounded chain of already-separated Bezier spans. No knot
    // insertion, normalization, or representation mutation is performed here.
    if (degree < 1 || degree > 3 || curve->pol->n < degree || curve->pol->n > 16 * degree || curve->pol->n % degree != 0 ||
        curve->knt->m != curve->pol->n + degree + 1 || !curve->pol->Pw || !curve->knt->U)
    {
        return {};
    }
    const int lastPole = static_cast<int>(curve->pol->n);
    const auto* knots = curve->knt->U;
    for (int i = 0; i <= degree; ++i)
    {
        if (knots[i] != knots[0] || knots[curve->knt->m - i] != knots[curve->knt->m])
        {
            return {};
        }
    }
    const bool polynomial = curve->pol->Pw[0].w == NL_NOW;
    RegularityProof result{ true, true };
    for (int startPole = 0; startPole < lastPole; startPole += degree)
    {
        const double start = knots[startPole + degree], end = knots[startPole + degree + 1];
        const Interval length = point(end) - point(start);
        if (!length.finite() || length.lo <= 0)
        {
            return {};
        }
        for (int repeat = 1; repeat <= degree; ++repeat)
        {
            if (knots[startPole + degree + repeat] != end)
            {
                return {}; // lower knot multiplicity requires actual Bezier extraction
            }
        }
        Bezier poles{};
        for (int i = 0; i <= degree; ++i)
        {
            const auto& p = curve->pol->Pw[startPole + i];
            // NLib's NL_NOW denotes polynomial XYZ, not a negative rational
            // weight (NL_Geometry.h). Its effective homogeneous weight is one.
            if ((p.w == NL_NOW) != polynomial || (!polynomial && p.w <= 0))
            {
                return {};
            }
            // The dimension tag does not itself project arbitrary stored Z.
            // NLib evaluates non-sentinel Z coordinates even on a curve tagged
            // 2D. Certify only explicit zero/absent Z there; anomalous data must
            // retain the evaluator/GPA path instead of proving a different curve.
            if ((dimension == 2 && p.z != 0 && p.z != NL_NOZ) || (dimension == 3 && p.z == NL_NOZ))
            {
                return {};
            }
            poles[i] = { point(p.x), point(p.y), point(dimension == 2 ? 0 : p.z), point(polynomial ? 1 : p.w) };
            for (Interval coordinate : poles[i])
            {
                if (!coordinate.finite())
                {
                    return {};
                }
            }
        }
        // Bounds include one-sided derivatives at span joins. The existing
        // interior-knot continuity checks above still execute independently.
        const RegularityProof span = subdivideBezier(poles, degree, static_cast<int>(dimension), length, tolerance, 3);
        result.speed &= span.speed;
        result.radius &= span.radius;
        if (!result.speed && !result.radius)
        {
            return {};
        }
    }
    return result;

#else
    SM_REF3(curve, dimension, tolerance);
    return {};
#endif
}
} // namespace

/*******************************************************************//**
PURPOSE: AssertValid() Test reporting static labels

NOTES:
   9 and 10 are unused, as of 4/18/2013.
***********************************************************************/
SmAssertReportLabel sAssertBSplineCurve_list[] =
{
 /*  0 */ {SM_AT_POINTER,   _T("Bad Null Ptr"),        _T("BSplineCurve m_pNurb ptr is NULL") },
 /*  1 */ {SM_AT_KNOTS,     _T("Bad Knot Count"),      _T("BSplineCurve Knot Vector Start and End knots do not have degree+1 multiplicity") },
 /*  2 */ {SM_AT_KNOTS,     _T("Bad Knot Count"),      _T("BSplineCurve Knot Vector Internal knot has multiplicity greater than degree") },
 /*  3 */ {SM_AT_GEOMETRIC, _T("Warn Coincident CPts"), _T("BSplineCurve's 1st two control points are coincident - not an error by itself, but commonly part of a problem.") },
 /*  4 */ {SM_AT_GEOMETRIC, _T("Warn Coincident CPts"), _T("BSplineCurve's last two control points are coincident - not an error by itself, but copmmonly part of a problem.") },

 /*  5 */ {SM_AT_GEOMETRIC, _T("Bad SpeedPt"),   _T("2D BSplineCurve Warning: zero-length derivative at interior multiple knot") },
 /*  6 */ {SM_AT_GEOMETRIC, _T("Bad SpeedPt"),   _T("3D BSplineCurve Warning: zero-length derivative at interior multiple knot") },
 /*  7 */ {SM_AT_GEOMETRIC, _T("Bad CurveKink"), _T("2D BSplineCurve Warning: Kink at interior knot") },
 /*  8 */ {SM_AT_GEOMETRIC, _T("Bad CurveKink"), _T("3D BSplineCurve Warning: Kink at interior knot") },

 /*  9 */ {SM_AT_GEOMETRIC, _T("Bad CurveCusp"), _T("2D BSplineCurve ERROR: Curve kinks back on itself at interior knot") },
 /* 10 */ {SM_AT_GEOMETRIC, _T("Bad CurveCusp"), _T("3D BSplineCurve ERROR: Curve kinks back on itself at interior knot") },
 /* 11 */ {SM_AT_GEOMETRIC, _T("Bad SpeedPt"),   _T("2D BSplineCurve Warning: Interior 1st Deriv too slow, less than AvgSpeed/10000 (often an almost kink or almost singular point - never good practice)") },
 /* 12 */ {SM_AT_GEOMETRIC, _T("Bad SpeedPt"),   _T("3D BSplineCurve Warning: Interior 1st Deriv too slow, less than AvgSpeed/10000 (often an almost kink or almost singular point - never good practice)") },

 /* 13 */ {SM_AT_GEOMETRIC, _T("Bad CurveCusp"),     _T("2D BSplineCurve Warning: Extremely small radius of curvature (often an almost kink point - sometimes what's desired)") },
 /* 14 */ {SM_AT_GEOMETRIC, _T("Bad CurveCusp"),     _T("3D BSplineCurve Warning: Extremely small radius of curvature (often an almost kink point - sometimes what's desired)") },
 /* 15 */ {SM_AT_GEOMETRIC, _T("Bad Repeated CPts"), _T("BSplineCurve has Internal Coincident ControlPoints (causes internal point singularity)") },
 /* 16 */ {SM_AT_KNOTS,     _T("Bad BSpline Rep"),   _T("Number of knots inconsistent with number of ControlPoints and Curve degree") },
} ;

/*******************************************************************//**
PURPOSE: Check the validity of an SmBSplineCurve.

NOTES: Checks made:
 - Make sure m_pNurb is not NULL by calling MakeNurb() when needed.
   (Derived types, like SmLine and SmCircle, delay making the m_pNurb
   until needed.)
 - knots with multiplicity > degree;
 - kinks at multiple knots
 - zero first derivative at multiple knots
 - vanishing first derivatives anywhere
 - tiny radius of curvature anywhere

 Note: can't add an optional argument to this because it's virtual:
       would have to add it to every class's method.

RETURNS ---  TRUE  = OK
             FALSE = Problem
***********************************************************************/
SmBoolean SmBSplineCurve::AssertValid
 (SmAssertArray    * pAList,        // i/o: Accumulating list of failed Asserts, NULL to ignore, default:[NULL]
  SmAssertTestLevel  eTestLevel,    // in : SM_LEVEL_0=fewest tests, SM_LEVEL_1, SM_LEVEL_2=all tests,
                                    //      SM_LEVEL_GIVEN = run tests in order requested in pTestRequests
                                    //      default:[SM_LEVEL_0]
  SmAssertWalking    eWalkTree,     // NotUsed: in : SM_WALK = run AssertValid on any topology graph descendants, SM_NO_WALK=don't, default:[SM_WALK]
  SmTArray<ULONG>  * pTestRequests) // in : when eTestLevel == SM_LEVEL_GIVEN, run these tests in this order, default:[NULL]
 const
{
  SM_REF1(eWalkTree) ;
#ifdef SM_DEBUG_CODE // see also draw block at method exit
SmBoolean bDebugMe = FALSE ;
#endif // no SM_DEBUG_CODE

  // check base class validity
  SmBoolean bRtn = TRUE;

  // call the base class AssertValid
  bRtn &= (  (eTestLevel != SM_LEVEL_GIVEN)
           ? SmCurve::AssertValid(pAList, eTestLevel, SM_NO_WALK, pTestRequests)
           : TRUE ) ;

  // build m_pNurb when not available - this is a side effect!
  if (m_pNurb == NULL)
    {
      SmBSplineCurve *pBSC = SM_CONST_CAST(SmBSplineCurve*,this);
      pBSC->MakeNurb();
    }

  /*  0 */ // check for nonNULL m_pNurb pointer
  bRtn &= SM_ASSERT_BOOLEAN_REPORT(0, SM_LEVEL_0, (m_pNurb != NULL ), _T("") ) ;

  if( m_pNurb == NULL )
    {
      return(bRtn) ;
    }

  // Check knot vector
  ULONG idx ;
  SmTArray<double> sKnots;
  SmTArray<ULONG>  sMults;
  GetKnots( sKnots, &sMults );
  ULONG lNumKnots = sKnots.GetSize();
  SM_ASSERT( lNumKnots == sMults.GetSize() );

  /*  1 */ // check end knots for expected multiplicity count == Degree + 1
  ULONG lDeg = GetDegree();
  bRtn &= SM_ASSERT_BOOLEAN_REPORT(1, SM_LEVEL_0, (sMults[0] == lDeg+1 && sMults[lNumKnots-1] == lDeg+1), _T("") ) ;
  if( bRtn == FALSE )
    {
      return( bRtn );
    }

  /*  2 */ // check internal knots for multiplicty count limit <= Degree
  for ( idx = 1; idx < lNumKnots - 1; idx++ )
    { bRtn &= SM_ASSERT_BOOLEAN_REPORT(2, SM_LEVEL_0, (sMults[idx] <= lDeg), _T("") ) ; }

  if( bRtn == FALSE )
    {
      return( bRtn );
    }

  /* 16 */ // check number of knots consistent with number of ControlPoints and degree
  bRtn &= SM_ASSERT_BOOLEAN_REPORT(16, SM_LEVEL_0, (   m_pNurb == NULL
                                                    || (   (  m_pNurb->knt->m + 1) // number of knots
                                                        == (  m_pNurb->pol->n + 1  // = number of control pts
                                                            + m_pNurb->p + 1))),   // + degree + 1
                                                   _T("") ) ;
  if( bRtn == FALSE )
    {
      return( bRtn );
    }

  SmStatus eStat1, eStat2;

  // check for coincident end control points when the curve is not degenerate
  SmBoolean bDegenerate = SmTol::IsDegenerate(this) ;
  if(!bDegenerate)
    {
      // for now - allow endPair ControlPoint coincidence, but output a warning

      // check for start ControlPoint coincidence
      double dStartCPtDist ;
      SmVector3d sEuclid ;
      N_CPtToPtEuclid(m_pNurb->pol->Pw[0], (NL_POINT *)&sEuclid) ;
      double dStartScaledZero = SM_EFF_ZERO * (1.0 + sEuclid.GetMaxDimension()) ;
      N_DistCptCpt( m_pNurb->pol->Pw[0], m_pNurb->pol->Pw[1], &dStartCPtDist) ;

      if(SM_IS_ZERO_TO_TOL(dStartCPtDist, dStartScaledZero))
        {
          /*  3 */
          bRtn &= SM_ASSERT_VALUE_REPORT(3, SM_LEVEL_0, !SM_IS_ZERO_TO_TOL(dStartCPtDist, dStartScaledZero), dStartScaledZero, dStartCPtDist, _T("")) ;
          WARN(_T("BSplineCurve's 1st two control points are coincident - not an error by itself, but commonly part of a problem.\n")) ;
        }

#ifdef SM_DEBUG_CODE
      if(bDebugMe) // see also draw block at method exit
        {
          this->Dump() ;
        }
#endif // SM_DEBUG_CODE

      // check for end ControlPoint coincidence
      NL_INDEX iN = m_pNurb->pol->n ;
      double dEndCPtDist ;
      N_CPtToPtEuclid(m_pNurb->pol->Pw[iN], (NL_POINT *)&sEuclid) ;
      double dEndScaledZero = SM_EFF_ZERO * (1.0 + sEuclid.GetMaxDimension()) ;
      N_DistCptCpt( m_pNurb->pol->Pw[iN - 1], m_pNurb->pol->Pw[iN], &dEndCPtDist) ;
      if(SM_IS_ZERO_TO_TOL(dEndCPtDist, dEndScaledZero))
        {

#ifdef SM_DEBUG_CODE
          // cull repeated notifications
static    SmTArray<const SmCurve *> sBadCurveList ;
static constexpr ULONG sListLength = 6;
static       ULONG lFoundItem ;
          if(!sBadCurveList.FindElement(this, lFoundItem))
            {
              bRtn &= SM_ASSERT_VALUE_REPORT(4, SM_LEVEL_0, !SM_IS_ZERO_TO_TOL(dStartCPtDist, dStartScaledZero), dStartScaledZero, dStartCPtDist, _T("")) ;
              WARN(_T("BSplineCurve's last two control points are coincident - not an error by itself, but commonly part of a problem.\n")) ;
              if(sBadCurveList.GetSize() == sListLength) { sBadCurveList.RemoveLast() ; }
              sBadCurveList.InsertAt(0, this) ;
            }
#else // no SM_DEBUG_CODE
              bRtn &= SM_ASSERT_VALUE_REPORT(4, SM_LEVEL_0, !SM_IS_ZERO_TO_TOL(dStartCPtDist, dStartScaledZero), dStartScaledZero, dStartCPtDist, _T("")) ;
              WARN(_T("BSplineCurve's last two control points are coincident - not an error, but not good practice.\n")) ;
#endif // no SM_DEBUG_CODE

        }

    } // end not degenerate check

  // Check for kinks.  Check at internal knot multiplicities.
  // We'll flag an error for angles > SM_CONTINUITY_ANGLE.
  // We'll accept degenerate curves

  // skip kink and zero internal 1st derive checks for degenerate curves
  // and for analytic curves.
  SmBoolean bAnalytic = IsAnalytic();

  // for debug - data at failing tests
  double dMinRadius         =  SM_BIG_DOUBLE ;
  double dMinRadiusParam    = -SM_BIG_DOUBLE ;
  double dMin1stDerivParam  = -SM_BIG_DOUBLE ;
#ifdef SM_DEBUG_CODE
  double dMinSinAngRadParam = -SM_BIG_DOUBLE ;
#endif
  double dMaxSinAngRadParam = -SM_BIG_DOUBLE ;
  if(!bDegenerate && !bAnalytic)
    {

      double dCosineTol = 0.9998476951563913; // cosine of SM_CONTINUITY_ANGLE (1.0 degree)
      SmVector3d pV1[2], pV2[2];
      SmVector3d sCross;

      // for non degenerate curves
      for ( idx = 1; idx < lNumKnots - 1 && bRtn ; idx++ )
        {
          // skip knots with low mults that guarantee C1 or better continuity
          if ( sMults[idx] < lDeg )
            { continue; }

          // Lower interval evaluation
          double dParam = sKnots[idx];
          Evaluate( dParam, 1, FALSE, pV1 ); // FALSE = evaluate P in lower interval where P is on the right of the interva
          SmVector3d pV1Dir = pV1[1] ;
          eStat1 = pV1[1].Unitize(FALSE);
          if ( eStat1 != SM_SUCCESS )
            {
              if(GetDim() == 2)
                {
                  /*  5 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(5, SM_LEVEL_0, eStat1 == SM_SUCCESS, SM_EFF_ZERO, SM_UNDEF_DOUBLE, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmVector2d(dParam,0.0)) ;
                  SM_ASSERT_SET_VALUE(pV1Dir.Length()) ;
                  // SM_ASSERT_MSG( FALSE, _T("2D Curve Warning: zero-length derivative at interior multiple knot") );
                }
              if(GetDim() == 3)
                {
                  /*  6 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(6, SM_LEVEL_0, eStat1 == SM_SUCCESS, SM_EFF_ZERO, SM_UNDEF_DOUBLE, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmVector2d(dParam,0.0)) ;
                  SM_ASSERT_SET_VALUE(pV1Dir.Length()) ;
                  // SM_ASSERT_MSG( FALSE, _T("3D Curve Warning: zero-length derivative at interior multiple knot") );
                }
#ifdef SM_DEBUG_CODE
              if(bDebugMe) // see also draw block at method exit
                {
                  this->Dump() ;
                }
#endif // SM_DEBUG_CODE

              // skip further checks - only need to report the 1st problem
              continue;
            } // end Lower Eval 1st Deriv is zero check

          // upper interval eval
          Evaluate( dParam, 1, TRUE,  pV2 );  // TRUE  = evaluate P in upper interval where P is on the left of the interval
          SmVector3d pV2Dir = pV2[1] ;
          eStat2 = pV2[1].Unitize(FALSE);
          if ( eStat2 != SM_SUCCESS )
            {
              if(GetDim() == 2)
                {
                  /*  5 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(5, SM_LEVEL_0, eStat2 == SM_SUCCESS, SM_EFF_ZERO, SM_UNDEF_DOUBLE, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmVector2d(dParam,0.0)) ;
                  SM_ASSERT_SET_VALUE(pV2Dir.Length()) ;
                  // SM_ASSERT_MSG( FALSE, _T("2D Curve Warning: zero-length derivative at interior multiple knot" ));
                }
              if(GetDim() == 3)
                {
                  /*  6 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(6, SM_LEVEL_0, eStat2 == SM_SUCCESS, SM_EFF_ZERO, SM_UNDEF_DOUBLE, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmVector2d(dParam,0.0)) ;
                  SM_ASSERT_SET_VALUE(pV2Dir.Length()) ;
                  // SM_ASSERT_MSG( FALSE, _T("3D Curve Warning: zero-length derivative at interior multiple knot" ));
                }
              bRtn = FALSE;

              // skip further checks - only need to report the 1st problem
              continue;
            } // end upper Eval 1st deriv degenerate check

          // arrive here when lower and upper interval 1st derivatives are NonDegenerate and unitized
          // Check the angle between 1st derivatives
          double dDot = pV1[1].Dot( pV2[1] );

          // when angle between lower and upper ivl 1st Derivs is too big - curve is not G1
          if ( dDot < dCosineTol )
            {
              dMaxSinAngRadParam = dParam ;

              // double dAngTolRad = SM_DEG2RAD( SM_CONTINUITY_ANGLE );
              if(GetDim() == 2)
                {
                  /*  7 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(7, SM_LEVEL_0, dDot > dCosineTol, dCosineTol, dDot, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMaxSinAngRadParam, 0.0)) ;
                  SM_ASSERT_SET_VALUE(dDot) ;
                  // SM_ASSERT_MSG( FALSE, _T("2D Curve Warning: Kink at interior knot" ));
                }
              if(GetDim() == 3)
                {
                  /*  8 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(8, SM_LEVEL_0, dDot > dCosineTol, dCosineTol, dDot, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMaxSinAngRadParam, 0.0)) ;
                  SM_ASSERT_SET_VALUE(dDot) ;
                  // SM_ASSERT_MSG( FALSE, _T("3D Curve Warning: Kink at interior knot" ));
                }

              bRtn = FALSE;

              // skip further checks - only need to report the 1st problem
              continue;
            } // end Curve is not G1 check

        } // end for iter every interior knot

      // Check for nonregular curve: vanishing first derivative.
      // Allow zero 1st derivatives at the NaturalParameter ends of the curve.

      // Get a representive size
      SmExtent1d sDomain = GetNaturalInterval();
      SmExtent3d sBBox;
      CalculateBoundingBox( sDomain, &sBBox );
      double dSizeXYZ = sBBox.GetSize().Length();
      double dSizeT   =  sDomain.GetLength() ;

      // dTol: for finite   curves dTol and dMinSep are proportional to curve length
      //       for infinite curves dTol and dMinSep are set to absolute values
      double dTol    = IsBounded() ? (dSizeXYZ/dSizeT) / 10000        // dTol proportional to average curve speed
                                   : 1e-03 ;                          // else dTol is absolute
      double dMinSep = IsBounded() ? dSizeXYZ / smos_Max(20., dSizeT) // 3dTol for GlobalPropertyAnalysis() to find minimum 1st derivs
                                   : 5.0 ;                            //     (sloppy here is OK and cheaper than tight)

      // gwc:removed old tolerances in favor of tolerances proportional to average curve speed
      //      double dTol    = IsBounded() ? dSizeXYZ / 10000                 // dTol proportional to length
      //                                   : 1e-03 ;                          // else dTol is absolute
      //      double dMinSep = IsBounded() ? dSizeXYZ / smos_Max(20., dSizeT) // min separation between solutions
      //                                   : 5.0 ;                            //

      // Only the concrete B-spline evaluator is certified by its control data;
      // derived curve types may implement different parameter/evaluation rules.
      // SM_COMMON supplies the virtual runtime identity through GetType().
      // GetClassType() is static and always names this declaring class here.
      const RegularityProof proof = GetType() == SmBSplineCurve_TYPE
                                        ? proveBezierRegularity(m_pNurb, GetDim(), dTol)
                                        : RegularityProof{};
      SmSolutionArray rSolutionArray;
      if (!proof.speed)
        {
          this->GlobalPropertyAnalysis( sDomain, SM_CP_MINIMIZE_FIRST_DERIVATIVE,
                                        NULL, NULL, dMinSep, rSolutionArray );
        }


      // Note: the solution value is not necessarily the length of
      // the first derivative, it's the value of whatever function
      // the particular solver was zeroing or minimizing (such as C'.C').
      // So we have to evaluate it at the solution parameter.
      ULONG ii ;
      for(ii=0;ii<rSolutionArray.GetSize();ii++)
        {
          // solution parameter value
          double dParam = rSolutionArray[ii].m_vStart[0];

          // skip solutions on the natural boundary ends - they are not a problem
          if(sDomain.IsValueOnBoundary(dParam, SM_EFF_ZERO * ( 1.0 + sDomain.GetLength())))
            { continue ; }

          // 1stDeriv magnitude
          SmVector3d sPtDer[2];
          this->Evaluate( dParam, 1, FALSE, sPtDer );
          double dDerivLength = sPtDer[1].Length();

          // check 1stDeriv > tolerance
          if ( dDerivLength < dTol )
            {
              // save values for debugging
              dMin1stDerivParam = dParam ;

              if(GetDim() == 2)
                {
                  /* 11 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(11, SM_LEVEL_0, dDerivLength >= dTol, dTol, dDerivLength, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMin1stDerivParam, 0.0)) ;
                  SM_ASSERT_SET_VALUE(dDerivLength) ;
                  // SM_ASSERT_MSG( FALSE, _T("2D Curve Warning: Vanishing first derivative" ));
                }
              if(GetDim() == 3)
                {
                  /* 12 */
                  bRtn &= SM_ASSERT_VALUE_REPORT(12, SM_LEVEL_0, dDerivLength >= dTol, dTol, dDerivLength, _T("")) ;
                  SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMin1stDerivParam, 0.0)) ;
                  SM_ASSERT_SET_VALUE(dDerivLength) ;
                  // SM_ASSERT_MSG( FALSE, _T("3D Curve Warning: Vanishing first derivative" ));
                }

#ifdef SM_DEBUG_CODE
              if(bDebugMe) // see also draw block at method exit
                {
                  this->Dump() ;
                }
#endif // SM_DEBUG_CODE

              // only need to report one problem
              break ;
            } // end DerivLength too short check

          // GWC: it's not clear if solutions are ordered by minimum value or param value - check all solns
          //      // only need to check the 1st non-boundary solution
          //      // since solutions are ordered by with the smallest minimum value first
          //      break ;

       } // end iter all min Deriv solutions

      // Release report macros only compare levels. Gate explicitly so a
      // request for an unrelated test does not acquire a new radius failure.
      if (!proof.radius && SM_ASSERT_TEST_NEED(GetDim() == 2 ? 13 : 14, SM_LEVEL_0))
        {
        // Keep the historical threshold above, including its parameter dependence.
        // A small radius is a validator quality finding, not a claim that the
        // mathematical curve or its owning topology is invalid in every use.
        this->GlobalPropertyAnalysis(sDomain, SM_CP_MINIMIZE_RADIUS_OF_CURVATURE,
                                     NULL, NULL, dMinSep, rSolutionArray);
        // GPA does not promise that candidates are sorted by property value.
        // Consume them before testing the threshold (the old guard tested the
        // unchanged SM_BIG_DOUBLE sentinel, so tests 13/14 could never report).
        for (ULONG solution = 0; solution < rSolutionArray.GetSize(); ++solution)
          {
            const SmSolution& candidate = rSolutionArray[solution];
            if (candidate.m_vStart.m_dSolutionValue < dMinRadius)
              {
                dMinRadius = candidate.m_vStart.m_dSolutionValue;
                dMinRadiusParam = candidate.m_vStart.m_adParameters[0];
              }
          }
        if(dMinRadius < dTol)
          {
            if(GetDim() == 2)
              {
                /* 13 */
                bRtn &= SM_ASSERT_VALUE_REPORT(13, SM_LEVEL_0, dMinRadius >= dTol, dTol, dMinRadius, _T("")) ;
                SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMinRadiusParam, 0.0)) ;
                // SM_ASSERT_MSG( FALSE, _T("2D Curve Warning: Extremely small radius of curvature") );
              }
            if(GetDim() == 3)
              {
                /* 14 */
                bRtn &= SM_ASSERT_VALUE_REPORT(14, SM_LEVEL_0, dMinRadius >= dTol, dTol, dMinRadius, _T("")) ;
                SM_ASSERT_SET_OWNER_PARAM(SmPoint2d(dMinRadiusParam, 0.0)) ;

                // SM_ASSERT_MSG( FALSE, _T("3D Curve Warning: Extremely small radius of curvature") );
              }
          }
        }
    } // end not degnerate curve check

  /* 15 */ // Are Curve Internal ControlPoints Coincident
  double dPoleTol = SM_ZONE_TOL_3D/10.0 ;  // gwc: need a real tolerance here - this one is just made up.
  SmBoolean bHasInternalPole = HasInternalPole(dPoleTol) ; // TRUE = internal 'pole' due coincident Cpts
  bRtn &= SM_ASSERT_VALUE_REPORT(15, SM_LEVEL_0, bHasInternalPole == FALSE, dPoleTol, SM_UNDEF_DOUBLE, _T("")) ;

#ifdef SM_DEBUG_CODE
  // place for a break point
  if(bHasInternalPole)
    { if(bDebugMe)
        { Dump(FALSE) ; }
    }

  // draw
  if(bDebugMe)
    {
      Dump(TRUE) ;

      SmObject  *pObj      = (SmEdge *)GetEdge() ;
      SmEdge    *pEdge     = SM_CAST_PTR(SmEdge, pObj) ;
// Remove Composites
//      SmCEdge   *pCEdge    = SM_CAST_PTR(SmCEdge, pObj) ;
      SmFace    *pFace     = SM_CAST_PTR(SmFace, pObj) ;
      SmEdgeuse *pEdgeuse  = SM_CAST_PTR(SmEdgeuse, pObj) ;
      SmSurface *pSurface  = pEdgeuse && pEdgeuse->GetFace() ? pEdgeuse->GetFace()->GetSurface() : NULL ;
      SmBrep    *pBrep =   pEdge    ? pEdge->GetBrep()
// Remove Composites
//                         : pCEdge   ? pCEdge->GetBrep()
                         : pFace    ? pFace->GetBrep()
                         : pEdgeuse ? pEdgeuse->GetBrep()
                         : NULL ;
      double dFailedParam = smos_4Min(smos_Fabs(dMinRadiusParam),
                                      smos_Fabs(dMin1stDerivParam),
                                      smos_Fabs(dMinSinAngRadParam),
                                      smos_Fabs(dMaxSinAngRadParam)) ;
      if(pEdge)    { pEdge->Dump() ; }
// Remove Composites
//      if(pCEdge)   { pCEdge->Dump() ; }
      if(pFace)    { pFace->Dump() ; }
      if(pEdgeuse) { pEdgeuse->Dump() ; }
      if(pSurface) { pSurface->Dump() ; }

      smgfx_Erase() ;
      smgfx_SetLook(1,2, 0,0,1) ; if(pBrep) pBrep->Draw(TRUE) ; sm_GraphicsLoop() ;
      if(GetDim() == 3) { smgfx_SetLook(3,4, 1,0,0) ; Draw() ; sm_GraphicsLoop() ;
                          smgfx_SetLook(3,4, 1,0,0) ; DrawParams() ; sm_GraphicsLoop() ;
                          smgfx_SetLook(7,8, 1,0,0) ; if(dFailedParam != SM_BIG_DOUBLE) this->DrawAt(dFailedParam) ; sm_GraphicsLoop() ;
                        }
      else              { if(pEdgeuse && pSurface)
                            { SmCrvOnSurf sCrvOnSurf((SmCurve &)*this, *pSurface) ;
                              smgfx_SetLook(3,4, 1,0,0) ; sCrvOnSurf.Draw() ; sm_GraphicsLoop() ;
                              smgfx_SetLook(3,4, 1,0,0) ; sCrvOnSurf.DrawParams() ; sm_GraphicsLoop() ;
                              smgfx_SetLook(7,8, 1,0,0) ; if(dFailedParam != SM_BIG_DOUBLE) sCrvOnSurf.DrawAt(dFailedParam) ; sm_GraphicsLoop() ;
                            }
                        }
      smgfx_SetLook(5,6, 1,0,1) ; if(pEdge) pEdge->Draw() ; sm_GraphicsLoop() ;
      smgfx_SetLook(1,2, 0,1,1) ; if(pSurface) pSurface->DrawUV() ; sm_GraphicsLoop() ;
      smgfx_SetLook(1,2, 0,0,0) ; if(pFace) pFace->Draw(SM_DM_CROSSHATCH) ; sm_GraphicsLoop() ;
      smgfx_SetLook(7,8, 1,0,0) ; if(dFailedParam != SM_BIG_DOUBLE) this->DrawAt(dFailedParam) ; sm_GraphicsLoop() ;
      sm_GraphicsLoop() ;
    }
#endif // SM_DEBUG_CODE

  // all done
  // SM_ASSERT( bRtn );

  return( bRtn );

} // end SmBSplineCurve::AssertValid
