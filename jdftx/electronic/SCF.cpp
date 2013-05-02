/*-------------------------------------------------------------------
Copyright 2013 Deniz Gunceler

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

#include <electronic/SCF.h>

SCF::SCF(Everything& e): e(e), overlap(e.residualMinimizerParams.history, e.residualMinimizerParams.history, false)
{
}

#define ifTau(command) if(e.exCorr.needsKEdensity()) command;

#define overlapResiduals(r1, r2) \
	(integral(r1[0]*r2[0]) + (r1.size() == 2 ? integral(r1[1]*r2[1]) : 0.))		

void SCF::minimize()
{
	ElecInfo& eInfo = e.eInfo;
	ElecVars& eVars = e.eVars;
	ResidualMinimizerParams rp = e.residualMinimizerParams;
	
	// Compute energy for the initial guess
	e.cntrl.fixed_n = false;
	e.ener = Energies();
	e.iInfo.update(e.ener);
	eVars.elecEnergyAndGrad(e.ener, 0, 0, 0);	
	e.ener = Energies(); 
	e.cntrl.fixed_n = true;
	
	// Initialize the variable that defines the single particle (Kohn-Sham) Hamiltonian
	// It can be either the densities (electronic and KE) or the potentials (Vscloc and Vtau)
	DataRptrCollection& variable_n = (rp.mixedVariable == density ? eVars.n : eVars.Vscloc);
	DataRptrCollection& variable_tau = (rp.mixedVariable == density ? eVars.tau : eVars.Vtau);
	logPrintf("\nWill mix electronic and kinetic potential %s at each iteration.\n", (rp.mixedVariable==density ? "density" : "potential"));
	
	// Set up variable history for vector extrapolation
	std::vector<DataRptrCollection> pastVariables_n, pastVariables_tau, pastResiduals;
	
	double Eprev = 0, E;
	
	logPrintf("\n------------------- SCF Cycle ---------------------\n");
	for(int scfCounter=0; scfCounter<e.residualMinimizerParams.nIterations; scfCounter++)
	{	
		// Clear history if full
		if((pastResiduals.size() >= rp.history) or (pastVariables_n.size() >= rp.history))
		{	pastVariables_n.clear();
			ifTau(pastVariables_tau.clear())
			if(rp.vectorExtrapolationMethod == DIIS)
				pastResiduals.clear();
		}
		
		// Cache the old energy and variables
		Eprev = E;
		pastVariables_n.push_back(clone(variable_n));
		ifTau(pastVariables_tau.push_back(clone(variable_tau)))
		
		// Solve at fixed hamiltonian
		e.cntrl.fixed_n = true; e.ener = Energies();
		logSuspend();
		for(int q = 0; q < eInfo.nStates; q++)
		{	
			BandMinimizer bmin(e, q, true);
			bmin.minimize(e.elecMinParams);
		}
		logResume();
		e.cntrl.fixed_n = false; e.ener = Energies();
		
		// Compute new density and energy
		e.iInfo.update(e.ener);
		E = eVars.elecEnergyAndGrad(e.ener, 0, 0, 0);
		
		logPrintf("SCF Iter: %i\tEprev: %f\tdE: %.2e\tEtot: %f\n\n", scfCounter, Eprev, E-Eprev, E);
		
		// Check for convergence, mix density or potential if otherwise
		if(fabs(E-Eprev) < rp.energyDiffThreshold)
		{	logPrintf("Residual Minimization Converged (|Delta E|<%le).\n", rp.energyDiffThreshold);
			break;
		}
		else
		{	
			if((rp.vectorExtrapolationMethod == plainMixing))
				mixPlain(variable_n, variable_tau, pastVariables_n.back(), pastVariables_tau.back());
			else if(rp.vectorExtrapolationMethod == DIIS)
			{			
				mixDIIS(variable_n, variable_tau, pastVariables_n, pastVariables_tau, pastResiduals);
			}
		
			// Recompute Vscloc if mixing density
			if(e.residualMinimizerParams.mixedVariable == density)
				e.eVars.EdensityAndVscloc(e.ener);
		}
	}
	
}

void SCF::mixPlain(DataRptrCollection& variable_n, DataRptrCollection& variable_tau, 
						 DataRptrCollection& prevVariable_n, DataRptrCollection& prevVariable_tau, double mixFraction)
{		// Mix old and new variable
		for(size_t s=0; s<e.eVars.n.size(); s++)
		{	variable_n[s] = mixFraction*variable_n[s] + (1.-mixFraction)*prevVariable_n[s];
			if(e.exCorr.needsKEdensity())
				e.eVars.tau[s] = mixFraction*variable_tau[s] + (1.-mixFraction)*prevVariable_tau[s];
		}
}

void SCF::mixDIIS(DataRptrCollection& variable_n, DataRptrCollection& variable_tau, 
				  std::vector< DataRptrCollection >& pastVariables_n, std::vector< DataRptrCollection >& pastVariables_tau, 
				  std::vector< DataRptrCollection >& pastResiduals)
{
	
	logPrintf("\nWARNING: DIIS is still very experimental.  Exercise extreme caution when using it.\n");
	
	// Cache the current residual
	DataRptrCollection temp = clone(variable_n);
	axpy(-1., pastVariables_n.back(), temp);
	pastResiduals.push_back(clone(temp));
	
	// dimension of the subspace over which minimization is done
	size_t ndim = pastResiduals.size();
	
	// Compute the overlap of the new residual with the older ones
	for(size_t j=0; j<ndim; j++)
	{	double tempOverlap = overlapResiduals(pastResiduals[j], pastResiduals.back());
		overlap.set(j, ndim-1, tempOverlap);
		if(j != ndim-1) overlap.set(ndim-1, j, tempOverlap);
	}
	
	// if only 1 previous density exists, then does plain mixing
	if(ndim != e.residualMinimizerParams.history)
	{	mixPlain(variable_n, variable_tau, pastVariables_n.back(), pastVariables_tau.back());
		return;
	}
	
	// diagonalize the residual overlap matrix to get the minimum of residual
	matrix thisOverlap = overlap(0, ndim, 0, ndim);
	matrix overlapEvecs(ndim, ndim); diagMatrix overlapEigs(ndim);
	thisOverlap.diagonalize(overlapEvecs, overlapEigs);
	
/*	logPrintf("Overlap Matrix:\n");
	thisOverlap.print_real(globalLog);
	logPrintf("\nOverlap Eigs:\n");
	overlapEigs.print(globalLog);
	logPrintf("\nOverlap Evecs:\n");
	overlapEvecs.print_real(globalLog);
	logPrintf("\n");
*/

	// normalizes the coefs of the eigenvector so that sum(x_i) = 1.
	double norm = 0.;
	for(size_t j=0; j<ndim; j++)
		norm += overlapEvecs.data()[overlapEvecs.index(j, 0)].real();
	logPrintf("\n\tNorm: %f\n", norm);
	
	// updates variables
	DataRptrCollection residual = clone(pastResiduals.back());
	for(size_t s=0; s<e.eVars.n.size(); s++)
	{	variable_n[s] *= 0.;
		residual[s] *= 0.;
		ifTau(variable_tau[s] *= 0.)
	}
	for(size_t j=0; j<ndim; j++)
	{	double weight = overlapEvecs.data()[overlapEvecs.index(j, 0)].real()/norm;
		for(size_t s=0; s<e.eVars.n.size(); s++)
		{	variable_n[s] += weight*pastVariables_n[j][s];
			residual[s] += weight*pastResiduals[j][s];
			ifTau(variable_tau[s] += weight*pastVariables_tau[j][s])
		}
	}
	
	logPrintf("\n\tTotal electron check: %f\n\n", integral(e.eVars.n[0]));
	logPrintf("\n\tThis residual: %f \t New residual: %f\n\n", overlapResiduals(pastResiduals.back(), pastResiduals.back()), overlapResiduals(residual, residual));
}