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

#include <commands/command.h>
#include <electronic/Everything.h>

struct CommandBasis : public Command
{
	CommandBasis() : Command("basis")
	{
		format = "<kdep>=" + kdepMap.optionList();
		comments = "Basis set at each k-point (default), or single basis set at gamma point";
		hasDefault = true;
	}

	void process(ParamList& pl, Everything& e)
	{	pl.get(e.cntrl.basisKdep, BasisKpointDep, kdepMap, "kdep");
	}

	void printStatus(Everything& e, int iRep)
	{	fputs(kdepMap.getString(e.cntrl.basisKdep), globalLog);
	}
}
commandBasis;