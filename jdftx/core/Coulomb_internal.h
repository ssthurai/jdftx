/*-------------------------------------------------------------------
Copyright 2012 Ravishankar Sundararaman

This file is part of JDFTx.

JDFTx is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

JDFTx is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with JDFTx.  If not, see <http://www.gnu.org/licenses/>.
-------------------------------------------------------------------*/

#ifndef JDFTX_CORE_COULOMB_INTERNAL_H
#define JDFTX_CORE_COULOMB_INTERNAL_H

//! @file Coulomb_internal.h Shared inline functions for anlaytical truncated Coulomb kernels

#include <core/matrix3.h>
#include <core/Bspline.h>
#include <gsl/gsl_integration.h>

//! Periodic coulomb interaction (4 pi/G^2)
struct CoulombPeriodic_calc
{	__hostanddev__ double operator()(const vector3<int>& iG, const matrix3<>& GGT) const
	{	double Gsq = GGT.metric_length_squared(iG);
		return Gsq ? (4*M_PI)/Gsq : 0.;
	}
};

//! Slab-truncated coulomb interaction
struct CoulombSlab_calc
{	int iDir; double hlfL;
	CoulombSlab_calc(int iDir, double hlfL) : iDir(iDir), hlfL(hlfL) {}
	__hostanddev__ double operator()(const vector3<int>& iG, const matrix3<>& GGT) const
	{	double Gsq = GGT.metric_length_squared(iG);
		double Gplane = Gsq - GGT(iDir,iDir) * iG[iDir]*iG[iDir]; //G along the non-truncated directions
		Gplane = Gplane>0. ? sqrt(Gplane) : 0.; //safe sqrt to prevent NaN from roundoff errors
		return (4*M_PI) * (Gsq ? (1. - exp(-Gplane*hlfL) * cos(M_PI*iG[iDir]))/Gsq : -0.5*hlfL*hlfL);
	}
};

//! Sphere-truncated coulomb interaction
struct CoulombSpherical_calc
{	double Rc;
	CoulombSpherical_calc(double Rc) : Rc(Rc) {}
	__hostanddev__ double operator()(const vector3<int>& iG, const matrix3<>& GGT) const
	{	double Gsq = GGT.metric_length_squared(iG);
		return Gsq ? (4*M_PI) * (1. - cos(Rc*sqrt(Gsq)))/Gsq : (2*M_PI)*Rc*Rc;
	}
};

#ifdef GPU_ENABLED
void coulombAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const CoulombPeriodic_calc& calc, complex* data);
void coulombAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const CoulombSlab_calc& calc, complex* data);
void coulombAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const CoulombSpherical_calc& calc, complex* data);
#endif
void coulombAnalytic(vector3<int> S, const matrix3<>& GGT, const CoulombPeriodic_calc& calc, complex* data);
void coulombAnalytic(vector3<int> S, const matrix3<>& GGT, const CoulombSlab_calc& calc, complex* data);
void coulombAnalytic(vector3<int> S, const matrix3<>& GGT, const CoulombSpherical_calc& calc, complex* data);

//! Compute erf(x)/x (with x~0 handled properly)
__hostanddev__ double erf_by_x(double x)
{	double xSq = x*x;
	if(xSq<1e-6) return (1./sqrt(M_PI))*(2. - xSq*(2./3 + 0.2*xSq));
	else return erf(x)/x;
}



//--------------- Special function for cylinder mode ------------
//                 (implemented in CoulombWire.cpp)
//! Compute Cbar_k^sigma - the gaussian convolved cylindrical coulomb kernel - by numerical quadrature
struct Cbar
{	Cbar();
	~Cbar();
	double operator()(double k, double sigma, double rho, double rho0=1.); //!< Compute Cbar_k^sigma(rho)
private:
	static const size_t maxIntervals = 1000; //!< Size of integration workspace
	gsl_integration_workspace* iWS; //!< Integration workspace
	static double integrandSmallRho(double t, void* params); //!< Integrand for rho < sigma
	static double integrandLargeRho(double t, void* params); //!< Integrand for rho > sigma
};



//---------------------- Exchange Kernels --------------------

//In each of the following functions, kSq is the square of the appropriate
//wave vector (includes reciprocal lattice vector and k-point difference),
//and will not be zero (the G=0 term is handled in the calling routine)

//! Radial fourier transform of erfc(omega r)/r (not valid at G=0)
__hostanddev__ double erfcTilde(double Gsq, double omegaSq)
{	return (4*M_PI) * (omegaSq ? (1.-exp(-0.25*Gsq/omegaSq)) : 1.) / Gsq;
}


//! Periodic exchange
struct ExchangePeriodic_calc
{	__hostanddev__ double operator()(double kSq) const
	{	return (4*M_PI) / kSq;
	}
};

//! Erfc-screened Periodic exchange
struct ExchangePeriodicScreened_calc
{	double inv4omegaSq; //!< 1/(4 omega^2)
	ExchangePeriodicScreened_calc(double omega) : inv4omegaSq(0.25/(omega*omega)) {}
	
	__hostanddev__ double operator()(double kSq) const
	{	return (4*M_PI) * (1.-exp(-inv4omegaSq*kSq)) / kSq;
	}
};

//! Spherical-truncated exchange
struct ExchangeSpherical_calc
{	double Rc;
	ExchangeSpherical_calc(double Rc) : Rc(Rc) {}
	
	__hostanddev__ double operator()(double kSq) const
	{	return (4*M_PI) * (1. - cos(Rc * sqrt(kSq))) / kSq;
	}
};

//! Erfc-screened Spherical-truncated exchange
struct ExchangeSphericalScreened_calc
{	const double* coeff; //!< quintic spline coefficients
	double dGinv; //!< inverse of coefficient spacing
	size_t nSamples; //!< number of coefficients
	ExchangeSphericalScreened_calc(const double* coeff, double dGinv, size_t nSamples)
	: coeff(coeff), dGinv(dGinv), nSamples(nSamples) {}
	
	__hostanddev__ double operator()(double kSq) const
	{	double t = dGinv * sqrt(kSq);
		if(t >= nSamples) return 0.;
		else return QuinticSpline::value(coeff, t);
	}
};

void exchangeAnalytic(vector3<int> S, const matrix3<>& GGT, const ExchangePeriodic_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic(vector3<int> S, const matrix3<>& GGT, const ExchangePeriodicScreened_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic(vector3<int> S, const matrix3<>& GGT, const ExchangeSpherical_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic(vector3<int> S, const matrix3<>& GGT, const ExchangeSphericalScreened_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
#ifdef GPU_ENABLED
void exchangeAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const ExchangePeriodic_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const ExchangePeriodicScreened_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const ExchangeSpherical_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
void exchangeAnalytic_gpu(vector3<int> S, const matrix3<>& GGT, const ExchangeSphericalScreened_calc& calc, complex* data, const vector3<>& kDiff, double Vzero, double thresholdSq);
#endif

//Multiply a complexDataGptr's data by a RealKernel (real-symmetry reduced)
__hostanddev__ void multRealKernel_calc(size_t i, const vector3<int>& iG,
	const vector3<int>& S, const double* kernel, complex* data)
{	//Compute index on the real kernel:
	vector3<int> iGreal = iG;
	if(iGreal[2]<0) iGreal = -iGreal; //inversion symmetry in G-space for real-kernels
	if(iGreal[1]<0) iGreal[1] += S[1];
	if(iGreal[0]<0) iGreal[0] += S[0];
	size_t iReal = iGreal[2] + size_t(1+S[2]/2) * (iGreal[1] + S[1]*iGreal[0]);
	//Multiply:
	data[i] *= kernel[iReal];
}
void multRealKernel(vector3<int> S, const double* kernel, complex* data);
#ifdef GPU_ENABLED
void multRealKernel_gpu(vector3<int> S, const double* kernel, complex* data);
#endif

//Multiply a complexDataGptr's data by a kernel sampled with offset and rotation by rot
__hostanddev__ void multTransformedKernel_calc(size_t i, const vector3<int>& iG,
	const vector3<int>& S, const double* kernel, complex* data,
	const vector3<int>& offset, const matrix3<int>& rot)
{	//Compute index on the real kernel:
	vector3<int> iGkernel = rot * (iG - offset);
	//Reduce to [0,S-1) in each dimension:
	for(int k=0; k<3; k++)
	{	iGkernel[k] = iGkernel[k] % S[k];
		if(iGkernel[k]<0) iGkernel[k] += S[k];
	}
	size_t iReal = iGkernel[2] + S[2]*size_t(iGkernel[1] + S[1]*iGkernel[0]);
	//Multiply:
	data[i] *= kernel[iReal];
}
void multTransformedKernel(vector3<int> S, const double* kernel, complex* data,
	const vector3<int>& offset, const matrix3<int>& rot);
#ifdef GPU_ENABLED
void multTransformedKernel_gpu(vector3<int> S, const double* kernel, complex* data,
	const vector3<int>& offset, const matrix3<int>& rot);
#endif

#endif // JDFTX_CORE_COULOMB_INTERNAL_H