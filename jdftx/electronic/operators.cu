/*-------------------------------------------------------------------
Copyright 2011 Ravishankar Sundararaman

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

#include <core/scalar.h>
#include <core/LoopMacros.h>
#include <core/GpuKernelUtils.h>
#include <electronic/operators_internal.h>


__global__
void D_kernel(int zBlock, const vector3<int> S, const complex* in, complex* out, vector3<> Ge)
{	COMPUTE_halfGindices
	D_calc(i, iG, in, out, Ge);
}
void D_gpu(const vector3<int> S, const complex* in, complex* out, vector3<> Ge)
{	GpuLaunchConfigHalf3D glc(D_kernel, S);
	for(int zBlock=0; zBlock<glc.zBlockMax; zBlock++)
		D_kernel<<<glc.nBlocks,glc.nPerBlock>>>(zBlock, S, in, out, Ge);
	gpuErrorCheck();
}

__global__
void DD_kernel(int zBlock, const vector3<int> S, const complex* in, complex* out, vector3<> Ge1, vector3<> Ge2)
{	COMPUTE_halfGindices
	DD_calc(i, iG, in, out, Ge1, Ge2);
}
void DD_gpu(const vector3<int> S, const complex* in, complex* out, vector3<> Ge1, vector3<> Ge2)
{	GpuLaunchConfigHalf3D glc(DD_kernel, S);
	for(int zBlock=0; zBlock<glc.zBlockMax; zBlock++)
		DD_kernel<<<glc.nBlocks,glc.nPerBlock>>>(zBlock, S, in, out, Ge1, Ge2);
	gpuErrorCheck();
}

__global__
void multiplyBlochPhase_kernel(int zBlock, const vector3<int> S, const vector3<> invS, complex* v, const vector3<> k)
{	COMPUTE_rIndices
	v[i] *= blochPhase_calc(iv, invS, k);
}
void multiplyBlochPhase_gpu(const vector3<int>& S, const vector3<>& invS, complex* v, const vector3<>& k)
{	GpuLaunchConfig3D glc(multiplyBlochPhase_kernel, S);
	for(int zBlock=0; zBlock<glc.zBlockMax; zBlock++)
		 multiplyBlochPhase_kernel<<<glc.nBlocks,glc.nPerBlock>>>(zBlock, S, invS, v, k);
	gpuErrorCheck();
}

template<typename scalar> __global__
void pointGroupGather_kernel(int zBlock, const vector3<int> S, const scalar* in, scalar* out, matrix3<int> mMesh)
{	COMPUTE_rIndices
	pointGroupGather_calc(i, iv, S, in, out, mMesh);
}
template<typename scalar>
void pointGroupGather_gpu(const vector3<int>& S, const scalar* in, scalar* out, const matrix3<int>& mMesh)
{	GpuLaunchConfig3D glc(pointGroupGather_kernel<scalar>, S);
	for(int zBlock=0; zBlock<glc.zBlockMax; zBlock++)
		pointGroupGather_kernel<scalar><<<glc.nBlocks,glc.nPerBlock>>>(zBlock, S, in, out, mMesh);
	gpuErrorCheck();
}
void pointGroupGather_gpu(const vector3<int>& S, const double* in, double* out, const matrix3<int>& mMesh)
{	pointGroupGather_gpu<double>(S, in, out, mMesh);
}
void pointGroupGather_gpu(const vector3<int>& S, const complex* in, complex* out, const matrix3<int>& mMesh)
{	pointGroupGather_gpu<complex>(S, in, out, mMesh);
}


__global__
void radialFunction_kernel(int zBlock, const vector3<int> S, const matrix3<> GGT,
	complex* F, const RadialFunctionG f, vector3<> r0)
{	COMPUTE_halfGindices
	F[i] = radialFunction_calc(iG, GGT, f, r0);
}
void radialFunction_gpu(const vector3<int> S, const matrix3<>& GGT,
	complex* F, const RadialFunctionG& f, vector3<> r0)
{	GpuLaunchConfigHalf3D glc(radialFunction_kernel, S);
	for(int zBlock=0; zBlock<glc.zBlockMax; zBlock++)
		radialFunction_kernel<<<glc.nBlocks,glc.nPerBlock>>>(zBlock, S, GGT, F, f, r0);
	gpuErrorCheck();
}

__global__
void reducedL_kernel(int nbasis, int ncols, const complex* Y, complex* LY,
	const matrix3<> GGT, const vector3<int>* iGarr, const vector3<> k, double detR)
{	int j = kernelIndex1D();
	if(j<nbasis) reducedL_calc(j, nbasis, ncols, Y, LY, GGT, iGarr, k, detR);
}
void reducedL_gpu(int nbasis, int ncols, const complex* Y, complex* LY,
	const matrix3<> GGT, const vector3<int>* iGarr, const vector3<> k, double detR)
{	GpuLaunchConfig1D glc(reducedL_kernel, nbasis);
	reducedL_kernel<<<glc.nBlocks,glc.nPerBlock>>>(nbasis, ncols, Y, LY, GGT, iGarr, k, detR);
	gpuErrorCheck();
}


__global__
void precond_inv_kinetic_kernel(int nbasis, int ncols, const complex* Y, complex* KY, 
	double KErollover, const matrix3<> GGT, const vector3<int>* iGarr, const vector3<> k, double invdetR)
{	int j = kernelIndex1D();
	if(j<nbasis) precond_inv_kinetic_calc(j, nbasis, ncols, Y, KY, KErollover, GGT, iGarr, k, invdetR);
}
void precond_inv_kinetic_gpu(int nbasis, int ncols, const complex* Y, complex* KY, 
	double KErollover, const matrix3<> GGT, const vector3<int>* iGarr, const vector3<> k, double invdetR)
{	GpuLaunchConfig1D glc(precond_inv_kinetic_kernel, nbasis);
	precond_inv_kinetic_kernel<<<glc.nBlocks,glc.nPerBlock>>>(nbasis, ncols, Y, KY, KErollover, GGT, iGarr, k, invdetR);
	gpuErrorCheck();
}

__global__
void translate_kernel(int nbasis, int ncols, complex* Y, const vector3<int>* iGarr, const vector3<> k, const vector3<> dr)
{	int j = kernelIndex1D();
	if(j<nbasis) translate_calc(j, nbasis, ncols, Y, iGarr, k, dr);
}
void translate_gpu(int nbasis, int ncols, complex* Y, const vector3<int>* iGarr, const vector3<>& k, const vector3<>& dr)
{	GpuLaunchConfig1D glc(translate_kernel, nbasis);
	translate_kernel<<<glc.nBlocks,glc.nPerBlock>>>(nbasis, ncols, Y, iGarr, k, dr);
	gpuErrorCheck();
}


__global__
void reducedD_kernel(int nbasis, int ncols, const complex* Y, complex* DY, 
	const vector3<int>* iGarr, double kdotGe, const vector3<> Ge)
{	int j = kernelIndex1D();
	if(j<nbasis) reducedD_calc(j, nbasis, ncols, Y, DY, iGarr, kdotGe, Ge);
}
void reducedD_gpu(int nbasis, int ncols, const complex* Y, complex* DY, 
	const vector3<int>* iGarr, double kdotGe, const vector3<> Ge)
{	GpuLaunchConfig1D glc(reducedD_kernel, nbasis);
	reducedD_kernel<<<glc.nBlocks,glc.nPerBlock>>>(nbasis, ncols, Y, DY, iGarr, kdotGe, Ge);
	gpuErrorCheck();
}


