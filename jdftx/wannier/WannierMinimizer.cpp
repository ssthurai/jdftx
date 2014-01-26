/*-------------------------------------------------------------------
Copyright 2014 Ravishankar Sundararaman

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

#include <wannier/WannierMinimizer.h>
#include <electronic/operators.h>
#include <electronic/SpeciesInfo_internal.h>
#include <core/BlasExtra.h>
#include <core/Random.h>
#include <core/LatticeUtils.h>

//---- linear algebra functions required by Minimizable<WannierGradient> -----

WannierGradient clone(const WannierGradient& grad) { return grad; }
double dot(const WannierGradient& x, const WannierGradient& y)
{	assert(x.size()==y.size());
	double result = 0.;
	for(unsigned i=0; i<x.size(); i++)
		result += dotc(x[i], y[i]).real();
	return result;
}
WannierGradient& operator*=(WannierGradient& x, double alpha)
{	for(unsigned i=0; i<x.size(); i++) x[i] *= alpha;
	return x;
}
void axpy(double alpha, const WannierGradient& x, WannierGradient& y)
{	assert(x.size()==y.size());
	for(unsigned i=0; i<x.size(); i++) axpy(alpha, x[i], y[i]);
}

matrix randomMatrix(int nRows, int nCols)
{	matrix ret(nRows, nCols, false);
	complex* retData = ret.data();
	for(unsigned j=0; j<ret.nData(); j++)
		retData[j] = Random::normalComplex();
	return ret;
}
void randomize(WannierGradient& x)
{	for(unsigned i=0; i<x.size(); i++)
		x[i] = dagger_symmetrize(randomMatrix(x[i].nRows(), x[i].nCols()));
}

//---- energy/gradient functions required by Minimizable<WannierGradient> -----

void WannierMinimizer::step(const WannierGradient& grad, double alpha)
{	assert(grad.size()==kMesh.size());
	for(unsigned i=0; i<kMesh.size(); i++)
		axpy(alpha, grad[i], kMesh[i].B);
}

double WannierMinimizer::compute(WannierGradient* grad)
{	int nCenters = kMesh[0].B.nRows();
	//Compute the unitary matrices:
	for(size_t i=0; i<kMesh.size(); i++)
		kMesh[i].V = cis(kMesh[i].B, &kMesh[i].Bevecs, &kMesh[i].Beigs);
	
	//Compute the expectation values of r and rSq for each center (split over processes)
	rSqExpect.assign(nCenters, 0.);
	rExpect.assign(nCenters, vector3<>(0,0,0));
	for(size_t i=ikStart; i<ikStop; i++)
		for(EdgeFD& edge: kMesh[i].edge)
		{	unsigned j = edge.ik;
			const matrix M = dagger(kMesh[i].V) * edge.M0 * kMesh[j].V;
			const complex* Mdata = M.data();
			for(int n=0; n<nCenters; n++)
			{	complex Mnn = Mdata[M.index(n,n)];
				double argMnn = atan2(Mnn.imag(), Mnn.real());
				rExpect[n] -= (wk * edge.wb * argMnn) * edge.b;
				rSqExpect[n] += wk * edge.wb * (argMnn*argMnn + 1. - Mnn.norm());
			}
		}
	mpiUtil->allReduce(rSqExpect.data(), nCenters, MPIUtil::ReduceSum);
	mpiUtil->allReduce((double*)rExpect.data(), 3*nCenters, MPIUtil::ReduceSum);
	
	//Compute the mean variance of the Wannier centers
	double rVariance = 0.;
	for(int n=0; n<nCenters; n++)
		rVariance += (1./nCenters) * (rSqExpect[n] - rExpect[n].length_squared());
	
	//Compute the gradients of the mean variance (if required)
	if(grad)
	{	//Allocate and initialize all gradients to zero:
		grad->resize(kMesh.size());
		for(size_t i=0; i<kMesh.size(); i++)
			(*grad)[i] = zeroes(nCenters, nCenters);
		//Accumulate gradients from each edge (split over processes):
		for(size_t i=ikStart; i<ikStop; i++)
			for(EdgeFD& edge: kMesh[i].edge)
			{	unsigned j = edge.ik;
				const matrix M = dagger(kMesh[i].V) * edge.M0 * kMesh[j].V;
				//Compute drVariance/dM:
				matrix rVariance_M = zeroes(nCenters, nCenters);
				const complex* Mdata = M.data();
				complex* rVariance_Mdata = rVariance_M.data();
				for(int n=0; n<nCenters; n++)
				{	complex Mnn = Mdata[M.index(n,n)];
					double argMnn = atan2(Mnn.imag(), Mnn.real());
					rVariance_Mdata[rVariance_M.index(n,n)] =
						(2./nCenters) * wk * edge.wb
						* ((argMnn + dot(rExpect[n],edge.b))*complex(0,-1)/Mnn - Mnn.conj());
				}
				//Propagate to drVariance/dBi and drVariance/dBj:
				matrix F0 = kMesh[j].V * rVariance_M * dagger(kMesh[i].V);
				(*grad)[i] -= dagger_symmetrize(cis_grad(edge.M0 * F0, kMesh[i].Bevecs, kMesh[i].Beigs));
				(*grad)[j] += dagger_symmetrize(cis_grad(F0 * edge.M0, kMesh[j].Bevecs, kMesh[j].Beigs));
			}
		for(size_t i=0; i<kMesh.size(); i++) (*grad)[i].allReduce(MPIUtil::ReduceSum);
	}
	return rVariance;
}

//---------------- kpoint and wavefunction handling -------------------

bool WannierMinimizer::Kpoint::operator<(const WannierMinimizer::Kpoint& other) const
{	if(q!=other.q) return q<other.q;
	if(iRot!=other.iRot) return iRot<other.iRot;
	if(invert!=other.invert) return invert<other.invert;
	if(!(offset==other.offset)) return offset<other.offset;
	return false; //all equal
}

bool WannierMinimizer::Kpoint::operator==(const WannierMinimizer::Kpoint& other) const
{	if(q!=other.q) return false;
	if(iRot!=other.iRot) return false;
	if(invert!=other.invert) return false;
	if(!(offset==other.offset)) return false;
	return true;
}


WannierMinimizer::Index::Index(int nIndices, bool needSuper) : nIndices(nIndices), dataPref(0), dataSuperPref(0)
{	data = new int[nIndices];
	dataSuper = needSuper ? new int[nIndices] : 0;
	#ifdef GPU_ENABLED
	dataGpu = 0;
	dataSuperGpu = 0;
	#endif
}
WannierMinimizer::Index::~Index()
{	delete[] data;
	if(dataSuper) delete[] dataSuper;
	#ifdef GPU_ENABLED
	if(dataGpu) cudaFree(dataGpu);
	if(dataSuperGpu) cudaFree(dataSuperGpu);
	#endif
}
void WannierMinimizer::Index::set()
{
	#ifdef GPU_ENABLED
	cudaMalloc(&dataGpu, sizeof(int)*nIndices); gpuErrorCheck();
	cudaMemcpy(dataGpu, data, sizeof(int)*nIndices, cudaMemcpyHostToDevice); gpuErrorCheck();
	dataPref = dataGpu;
	if(dataSuper)
	{	cudaMalloc(&dataSuperGpu, sizeof(int)*nIndices); gpuErrorCheck();
		cudaMemcpy(dataSuperGpu, dataSuper, sizeof(int)*nIndices, cudaMemcpyHostToDevice); gpuErrorCheck();
	}
	else dataSuperGpu = 0;
	dataSuperPref = dataSuperGpu;
	#else
	dataPref = data;
	dataSuperPref = dataSuper;
	#endif
}

void WannierMinimizer::addIndex(const WannierMinimizer::Kpoint& kpoint)
{	if(indexMap.find(kpoint)!=indexMap.end()) return; //previously computed
	//Determine integer offset due to k-point in supercell basis:
	const matrix3<int>& super = e.coulombParams.supercell->super;
	vector3<int> ksuper; //integer version of above
	if(wannier.saveWfns)
	{	vector3<> ksuperTemp = kpoint.k * super - qnumSuper.k; //note reciprocal lattice vectors transform on the right (or on the left by the transpose)
		for(int l=0; l<3; l++)
		{	ksuper[l] = int(round(ksuperTemp[l]));
			assert(fabs(ksuper[l]-ksuperTemp[l]) < symmThreshold);
		}
	}
	//Compute transformed index array (mapping to full G-space)
	const Basis& basis = e.basis[kpoint.q];
	std::shared_ptr<Index> index(new Index(basis.nbasis, wannier.saveWfns));
	const matrix3<int> mRot = (~sym[kpoint.iRot]) * kpoint.invert;
	for(int j=0; j<index->nIndices; j++)
	{	vector3<int> iGrot = mRot * basis.iGarr[j] - kpoint.offset;
		index->data[j] = e.gInfo.fullGindex(iGrot);
		if(wannier.saveWfns)
			index->dataSuper[j] = gInfoSuper.fullGindex(ksuper + iGrot*super);
	}
	//Save to map:
	indexMap[kpoint] = index;
}

ColumnBundle WannierMinimizer::getWfns(const WannierMinimizer::Kpoint& kpoint, int iSpin, bool super) const
{	const Index& index = *(indexMap.find(kpoint)->second);
	const int* indexData = super ? index.dataSuperPref : index.dataPref;
	const Basis& basis = super ? this->basisSuper : this->basis;
	ColumnBundle ret(nCenters, basis.nbasis, &basis, 0, isGpuEnabled());
	ret.zero();
	//Pick required bands, and scatter from reduced basis to common basis with transformations:
	int q = kpoint.q + iSpin*qCount;
	const ColumnBundle& C = e.eInfo.isMine(q) ? e.eVars.C[q] : Cother[q];
	assert(C);
	for(int c=0; c<nCenters; c++)
		callPref(eblas_scatter_zdaxpy)(index.nIndices, 1., indexData,
			C.dataPref()+C.index(wannier.bStart+c,0), ret.dataPref()+ret.index(c,0));
	//Complex conjugate if inversion symmetry employed:
	if(kpoint.invert < 0)
		callPref(eblas_dscal)(ret.nData(), -1., ((double*)ret.dataPref())+1, 2); //negate the imaginary parts
	return ret;
}

//Fourier transform of hydrogenic orbitals
inline double hydrogenicTilde(double G, double a, int nIn, int l, double normPrefac)
{	int n = nIn+1 + l; //conventional principal quantum number
	double nG = n*G*a/(l+1), nGsq = nG*nG;
	double prefac = normPrefac / pow(1.+nGsq, n+1);
	switch(l)
	{	case 0:
			switch(n)
			{	case 1: return prefac;
				case 2: return prefac*8.*(-1.+nGsq);
				case 3: return prefac*9.*(3.+nGsq*(-10.+nGsq*3.));
				case 4: return prefac*64.*(-1.+nGsq*(7.+nGsq*(-7.+nGsq)));
			}
		case 1:
			switch(n)
			{	case 2: return prefac*16.*nG;
				case 3: return prefac*144.*nG*(-1.+nGsq);
				case 4: return prefac*128.*nG*(5.+nGsq*(-14.+nGsq*5.));
			}
		case 2:
			switch(n)
			{	case 3: return prefac*288.*nGsq;
				case 4: return prefac*3072.*nGsq*(-1.+nGsq);
			}
		case 3:
			switch(n)
			{	case 4: return prefac*6144.*nG*nGsq;
			}
	}
	return 0.;
}

ColumnBundle WannierMinimizer::trialWfns(const WannierMinimizer::Kpoint& kpoint) const
{	ColumnBundle ret(nCenters, basis.nbasis, &basis, 0, isGpuEnabled());
	#ifdef GPU_ENABLED
	vector3<>* pos; cudaMalloc(&pos, sizeof(vector3<>));
	#endif
	complex* retData = ret.dataPref();
	for(auto c: wannier.centers)
	{	const DOS::Weight::OrbitalDesc& od = c.orbitalDesc;
		//--- Copy the center to GPU if necessary:
		#ifdef GPU_ENABLED
		cudaMemcpy(pos, &c.r, sizeof(vector3<>), cudaMemcpyHostToDevice);
		#else
		const vector3<>* pos = &c.r;
		#endif
		//--- Create the radial part:
		RadialFunctionG atRadial;
		double normPrefac = pow((od.l+1)/c.a,3);
		for(unsigned p=od.n+1; p<=od.n+1+2*od.l; p++)
			normPrefac *= p;
		normPrefac = 16*M_PI/(e.gInfo.detR * sqrt(normPrefac));
		atRadial.init(od.l, 0.02, e.gInfo.GmaxSphere, hydrogenicTilde, c.a, od.n, od.l, normPrefac);
		//--- Initialize the projector:
		callPref(Vnl)(basis.nbasis, basis.nbasis, 1, od.l, od.m, kpoint.k, basis.iGarrPref, e.gInfo.G, pos, atRadial, retData);
		callPref(eblas_zscal)(basis.nbasis, cis(0.5*M_PI*od.l), retData,1); //ensures odd l projectors are real
		retData += basis.nbasis;
	}
	#ifdef GPU_ENABLED
	cudaFree(pos);
	#endif
	return ret;
}