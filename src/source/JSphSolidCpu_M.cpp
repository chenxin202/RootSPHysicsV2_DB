//HEAD_DSPH
/*
<DUALSPHYSICS>  Copyright (c) 2017 by Dr Jose M. Dominguez et al. (see http://dual.sphysics.org/index.php/developers/).

EPHYSLAB Environmental Physics Laboratory, Universidade de Vigo, Ourense, Spain.
School of Mechanical, Aerospace and Civil Engineering, University of Manchester, Manchester, U.K..

This file is part of DualSPHysics.

DualSPHysics is free software: you can redistribute it and/or modify it under the terms of the GNU Lesser General Public License
as published by the Free Software Foundation; either version 2.1 of the License, or (at your option) any later version.

DualSPHysics is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more details.

You should have received a copy of the GNU Lesser General Public License along with DualSPHysics. If not, see <http://www.gnu.org/licenses/>.
*/

/// \file JSphSolidCpu.cpp \brief Implements the class \ref JSphSolidCpu.

#include "JSphSolidCpu_M.h"
#include "JCellDivCpu.h"
#include "JPartFloatBi4.h"
#include "Functions.h"
#include "JSphMotion.h"
#include "JArraysCpu.h"
#include "JSphDtFixed.h"
#include "JWaveGen.h"
#include "JDamping.h"
#include "JXml.h"
#include "JSaveDt.h"
#include "JTimeOut.h"
#include "JSphAccInput.h"
#include "JGaugeSystem.h"
#include "TypesDef.h"

#include <climits>

using namespace std;

//==============================================================================
/// Constructor.
//==============================================================================
JSphSolidCpu::JSphSolidCpu(bool withmpi) :JSph(true, withmpi) {
	ClassName = "JSphSolidCpu";
	CellDiv = NULL;
	ArraysCpu = new JArraysCpu;
	InitVars();
	TmcCreation(Timers, false);
}

//==============================================================================
/// Destructor.
//==============================================================================
JSphSolidCpu::~JSphSolidCpu() {
	DestructorActive = true;
	FreeCpuMemoryParticles();
	FreeCpuMemoryFixed();
	delete ArraysCpu;
	TmcDestruction(Timers);
}

//=============================================================================
/// Initialisation of variables.
//==============================================================================
void JSphSolidCpu::InitVars() {
	RunMode = "";
	OmpThreads = 1;

	Np = Npb = NpbOk = 0;
	NpbPer = NpfPer = 0;
	WithFloating = false;

	Idpc = NULL; Codec = NULL; Dcellc = NULL; Posc = NULL; Velrhopc = NULL;
	VelrhopM1c = NULL;                //-Verlet
	PosPrec = NULL; VelrhopPrec = NULL; //-Symplectic
	PsPosc = NULL;                    //-Interaccion Pos-Single.
	SpsTauc = NULL; SpsGradvelc = NULL; //-Laminar+SPS. 
										// Matthias
	//JauTauc_M = NULL; JauGradvelc_M = NULL; // Jaumann Solid
	Tauc_M = NULL; StrainDotc_M = NULL; // Jaumann Solid
	TauM1c_M = NULL;
	MassM1c_M = NULL;
	TauDotc_M = NULL; Spinc_M = NULL; // Jaumann Solid

	Arc = NULL; Acec = NULL; Deltac = NULL;
	ShiftPosc = NULL; ShiftDetectc = NULL; //-Shifting.
	Pressc = NULL;
	Press3Dc_M = NULL;

	// Matthias
	Porec_M = NULL;
	Massc_M = NULL;
	Divisionc_M = NULL;
	QuadFormc_M = NULL;	QuadFormM1c_M = NULL;
	//Voluc_M = NULL;
	L_M = NULL;

	RidpMove = NULL;
	FtRidp = NULL;
	FtoForces = NULL;
	FtoForcesRes = NULL;
	FreeCpuMemoryParticles();
	FreeCpuMemoryFixed();
}

//==============================================================================
/// Deallocate fixed memory on CPU for moving and floating bodies.
/// Libera memoria fija en cpu para moving y floating.
//==============================================================================
void JSphSolidCpu::FreeCpuMemoryFixed() {
	MemCpuFixed = 0;
	delete[] RidpMove;     RidpMove = NULL;
	delete[] FtRidp;       FtRidp = NULL;
	delete[] FtoForces;    FtoForces = NULL;
	delete[] FtoForcesRes; FtoForcesRes = NULL;
}

//==============================================================================
/// Allocates memory for arrays with fixed size (motion and floating bodies).
//==============================================================================
void JSphSolidCpu::AllocCpuMemoryFixed() {
	MemCpuFixed = 0;
	try {
		//-Allocates memory for moving objects.
		if (CaseNmoving) {
			RidpMove = new unsigned[CaseNmoving];  MemCpuFixed += (sizeof(unsigned)*CaseNmoving);
		}
		//-Allocates memory for floating bodies.
		if (CaseNfloat) {
			FtRidp = new unsigned[CaseNfloat];     MemCpuFixed += (sizeof(unsigned)*CaseNfloat);
			FtoForces = new StFtoForces[FtCount];     MemCpuFixed += (sizeof(StFtoForces)*FtCount);
			FtoForcesRes = new StFtoForcesRes[FtCount];  MemCpuFixed += (sizeof(StFtoForcesRes)*FtCount);
		}
	}
	catch (const std::bad_alloc) {
		RunException("AllocMemoryFixed", "Could not allocate the requested memory.");
	}
}

//==============================================================================
/// Deallocate memory in CPU for particles.
/// Libera memoria en cpu para particulas.
//==============================================================================
void JSphSolidCpu::FreeCpuMemoryParticles() {
	CpuParticlesSize = 0;
	MemCpuParticles = 0;
	ArraysCpu->Reset();
}

//==============================================================================
/// Allocte memory on CPU for the particles. 
/// Reserva memoria en Cpu para las particulas. 
//==============================================================================
void JSphSolidCpu::AllocCpuMemoryParticles(unsigned np, float over) {
	const char* met = "AllocCpuMemoryParticles";
	printf("AllocCpuMemPart\n");
	//-Calculate number of partices with reserved memory | Calcula numero de particulas para las que se reserva memoria.
	const unsigned np2 = (over>0 ? unsigned(over*np) : np);
	CpuParticlesSize = np2 + PARTICLES_OVERMEMORY_MIN;
	//-Define number or arrays to use. | Establece numero de arrays a usar.
	ArraysCpu->SetArraySize(CpuParticlesSize);
#ifdef CODE_SIZE4
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 2);  //-code,code2
#else
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_2B, 2);  //-code,code2
#endif
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 5);  //-idp,ar,viscdt,dcell,prrhop
	if (TDeltaSph == DELTA_DynamicExt){
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 1);
	}  //-delta
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_12B, 1); //-ace
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_16B, 1); //-velrhop
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_24B, 2); //-pos
	if (Psingle){
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_12B, 1); //-pspos
	}
	if (TStep == STEP_Verlet) {
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_16B, 1); //-velrhopm1
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_24B, 2); //-JauTauM12, QuadFormM1
	}
	else if (TStep == STEP_Symplectic) {
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_24B, 1); //-pospre
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_16B, 1); //-velrhoppre
	}
	if (TVisco == VISCO_LaminarSPS) {
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_24B, 1); //-SpsTau,SpsGradvel
	}
	if (TShifting != SHIFT_None) {
		ArraysCpu->AddArrayCount(JArraysCpu::SIZE_12B, 1); //-shiftpos
	}

	// Matthias
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_1B, 1);  //division
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 1); // Pore
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 1); // Mass
	//ArraysCpu->AddArrayCount(JArraysCpu::SIZE_36B, 1); //-JauGradvel, JauTau
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_24B, 4); //-JauGradvel, JauTau2, Omega and Taudot, QuadForm
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_4B, 4); // SaveFields
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_36B, 1); // Matrix3f L_M
	ArraysCpu->AddArrayCount(JArraysCpu::SIZE_12B, 1); // Press3D

	//-Shows the allocated memory.
	MemCpuParticles = ArraysCpu->GetAllocMemoryCpu();
	PrintSizeNp(CpuParticlesSize, MemCpuParticles);
}

//==============================================================================
/// Resizes space in CPU memory for particles.
//==============================================================================
void JSphSolidCpu::ResizeCpuMemoryParticles(unsigned npnew) {
	npnew = npnew + PARTICLES_OVERMEMORY_MIN;
	//-Saves current data from CPU.
	unsigned    *idp = SaveArrayCpu(Np, Idpc);
	typecode    *code = SaveArrayCpu(Np, Codec);
	unsigned    *dcell = SaveArrayCpu(Np, Dcellc);
	tdouble3    *pos = SaveArrayCpu(Np, Posc);
	tfloat4     *velrhop = SaveArrayCpu(Np, Velrhopc);
	tfloat4     *velrhopm1 = SaveArrayCpu(Np, VelrhopM1c);
	tdouble3    *pospre = SaveArrayCpu(Np, PosPrec);
	tfloat4     *velrhoppre = SaveArrayCpu(Np, VelrhopPrec);
	tsymatrix3f *spstau = SaveArrayCpu(Np, SpsTauc);
	// Matthias
	bool		  *division = SaveArrayCpu(Np, Divisionc_M);
	float		  *pore = SaveArrayCpu(Np, Porec_M);
	float		  *mass = SaveArrayCpu(Np, Massc_M);
	float		  *massm1 = SaveArrayCpu(Np, MassM1c_M);
	//float		  *volu = SaveArrayCpu(Np, Voluc_M);	
	//tmatrix3f *jautau = SaveArrayCpu(Np, JauTauc_M);
	tsymatrix3f *jautau2 = SaveArrayCpu(Np, Tauc_M);
	tsymatrix3f *jautaum12 = SaveArrayCpu(Np, TauM1c_M);
	tsymatrix3f *quadform = SaveArrayCpu(Np, QuadFormc_M);
	tsymatrix3f *quadformm1 = SaveArrayCpu(Np, QuadFormM1c_M);

	//-Frees pointers.
	ArraysCpu->Free(Idpc);
	ArraysCpu->Free(Codec);
	ArraysCpu->Free(Dcellc);
	ArraysCpu->Free(Posc);
	ArraysCpu->Free(Velrhopc);
	ArraysCpu->Free(VelrhopM1c);
	ArraysCpu->Free(PosPrec);
	ArraysCpu->Free(VelrhopPrec);
	ArraysCpu->Free(SpsTauc);
	// Matthias
	ArraysCpu->Free(Divisionc_M);
	ArraysCpu->Free(Porec_M);
	ArraysCpu->Free(Massc_M);
	ArraysCpu->Free(MassM1c_M);
	//ArraysCpu->Free(Voluc_M);
	//ArraysCpu->Free(JauTauc_M);
	ArraysCpu->Free(Tauc_M);
	ArraysCpu->Free(TauM1c_M);
	ArraysCpu->Free(QuadFormc_M);
	ArraysCpu->Free(QuadFormM1c_M);

	//-Resizes CPU memory allocation.
	const double mbparticle = (double(MemCpuParticles) / (1024 * 1024)) / CpuParticlesSize; //-MB por particula.
	Log->Printf("**JSphSolidCpu: Requesting cpu memory for %u particles: %.1f MB.", npnew, mbparticle*npnew);
	ArraysCpu->SetArraySize(npnew);

	//-Reserve pointers.
	Idpc = ArraysCpu->ReserveUint();
	Codec = ArraysCpu->ReserveTypeCode();
	Dcellc = ArraysCpu->ReserveUint();
	Posc = ArraysCpu->ReserveDouble3();
	Velrhopc = ArraysCpu->ReserveFloat4();
	if (velrhopm1) VelrhopM1c = ArraysCpu->ReserveFloat4();
	if (pospre)    PosPrec = ArraysCpu->ReserveDouble3();
	if (velrhoppre)VelrhopPrec = ArraysCpu->ReserveFloat4();
	if (spstau)    SpsTauc = ArraysCpu->ReserveSymatrix3f();
	// Matthias
	Divisionc_M = ArraysCpu->ReserveBool();
	Porec_M = ArraysCpu->ReserveFloat();
	Massc_M = ArraysCpu->ReserveFloat();
	if (massm1) MassM1c_M = ArraysCpu->ReserveFloat();
	//Voluc_M = ArraysCpu->ReserveFloat();
	//JauTauc_M = ArraysCpu->ReserveMatrix3f_M();
	Tauc_M = ArraysCpu->ReserveSymatrix3f();
	if (jautaum12) TauM1c_M = ArraysCpu->ReserveSymatrix3f();
	QuadFormc_M = ArraysCpu->ReserveSymatrix3f();
	if (quadformm1) QuadFormM1c_M = ArraysCpu->ReserveSymatrix3f();

	//-Restore data in CPU memory.
	RestoreArrayCpu(Np, idp, Idpc);
	RestoreArrayCpu(Np, code, Codec);
	RestoreArrayCpu(Np, dcell, Dcellc);
	RestoreArrayCpu(Np, pos, Posc);
	RestoreArrayCpu(Np, velrhop, Velrhopc);
	RestoreArrayCpu(Np, velrhopm1, VelrhopM1c);
	RestoreArrayCpu(Np, pospre, PosPrec);
	RestoreArrayCpu(Np, velrhoppre, VelrhopPrec);
	RestoreArrayCpu(Np, spstau, SpsTauc);
	// Matthias
	RestoreArrayCpu(Np, division, Divisionc_M);
	RestoreArrayCpu(Np, pore, Porec_M);
	RestoreArrayCpu(Np, mass, Massc_M);
	RestoreArrayCpu(Np, massm1, MassM1c_M);
	//RestoreArrayCpu(Np, volu, Porec_M);
	//RestoreArrayCpu(Np, jautau, JauTauc_M);
	RestoreArrayCpu(Np, jautau2, Tauc_M);
	RestoreArrayCpu(Np, jautaum12, TauM1c_M);
	RestoreArrayCpu(Np, quadform, QuadFormc_M);
	RestoreArrayCpu(Np, quadformm1, QuadFormM1c_M);
	//-Updates values.
	CpuParticlesSize = npnew;
	MemCpuParticles = ArraysCpu->GetAllocMemoryCpu();
}

//==============================================================================
/// Saves a CPU array in CPU memory. 
//==============================================================================
template<class T> T* JSphSolidCpu::TSaveArrayCpu(unsigned np, const T *datasrc)const {
	T *data = NULL;
	if (datasrc) {
		try {
			data = new T[np];
		}
		catch (const std::bad_alloc) {
			RunException("TSaveArrayCpu", "Could not allocate the requested memory.");
		}
		memcpy(data, datasrc, sizeof(T)*np);
	}
	return(data);
}

//==============================================================================
/// Restores an array (generic) from CPU memory. 
//==============================================================================
template<class T> void JSphSolidCpu::TRestoreArrayCpu(unsigned np, T *data, T *datanew)const {
	if (data&&datanew)memcpy(datanew, data, sizeof(T)*np);
	delete[] data;
}

//==============================================================================
/// Arrays for basic particle data. 
/// Arrays para datos basicos de las particulas. 
//==============================================================================
void JSphSolidCpu::ReserveBasicArraysCpu() {
	Idpc = ArraysCpu->ReserveUint();
	Codec = ArraysCpu->ReserveTypeCode();
	Dcellc = ArraysCpu->ReserveUint();
	Posc = ArraysCpu->ReserveDouble3();
	Velrhopc = ArraysCpu->ReserveFloat4();
	if (TStep == STEP_Verlet) {
		VelrhopM1c = ArraysCpu->ReserveFloat4();
		MassM1c_M = ArraysCpu->ReserveFloat();
		TauM1c_M = ArraysCpu->ReserveSymatrix3f();
		QuadFormM1c_M = ArraysCpu->ReserveSymatrix3f();
	}
	if (TVisco == VISCO_LaminarSPS)SpsTauc = ArraysCpu->ReserveSymatrix3f();

	// Matthias
	Divisionc_M = ArraysCpu->ReserveBool();
	Porec_M = ArraysCpu->ReserveFloat();
	Massc_M = ArraysCpu->ReserveFloat();
	//JauTauc_M = ArraysCpu->ReserveMatrix3f_M();
	Tauc_M = ArraysCpu->ReserveSymatrix3f();
	QuadFormc_M = ArraysCpu->ReserveSymatrix3f();
}

//==============================================================================
/// Return memory reserved on CPU.
/// Devuelve la memoria reservada en cpu.
//==============================================================================
llong JSphSolidCpu::GetAllocMemoryCpu()const {
	llong s = JSph::GetAllocMemoryCpu();
	//-Reserved in AllocCpuMemoryParticles().
	s += MemCpuParticles;
	//-Reserved in AllocCpuMemoryFixed().
	s += MemCpuFixed;
	//-Reserved in other objects.
	return(s);
}

//==============================================================================
/// Visualize the reserved memory.
/// Visualiza la memoria reservada.
//==============================================================================
void JSphSolidCpu::PrintAllocMemory(llong mcpu)const {
	Log->Printf("Allocated memory in CPU: %lld (%.2f MB)", mcpu, double(mcpu) / (1024 * 1024));
}

//==============================================================================
/// Collect data from a range of particles and return the number of particles that 
/// will be less than n and eliminate the periodic ones
/// - cellorderdecode: Reorder components of position (pos) and velocity (vel) according to CellOrder.
/// - onlynormal: Only keep the normal ones and eliminate the periodic particles.
///
/// Recupera datos de un rango de particulas y devuelve el numero de particulas que
/// sera menor que n si se eliminaron las periodicas.
/// - cellorderdecode: Reordena componentes de pos y vel segun CellOrder.
/// - onlynormal: Solo se queda con las normales, elimina las particulas periodicas.
//==============================================================================
unsigned JSphSolidCpu::GetParticlesData(unsigned n, unsigned pini, bool cellorderdecode, bool onlynormal
	, unsigned *idp, tdouble3 *pos, tfloat3 *vel, float *rhop, typecode *code)
{
	const char met[] = "GetParticlesData";
	unsigned num = n;
	//-Copy selected values.
	if (code)memcpy(code, Codec + pini, sizeof(typecode)*n);
	if (idp)memcpy(idp, Idpc + pini, sizeof(unsigned)*n);
	if (pos)memcpy(pos, Posc + pini, sizeof(tdouble3)*n);
	if (vel && rhop) {
		for (unsigned p = 0; p<n; p++) {
			tfloat4 vr = Velrhopc[p + pini];
			vel[p] = TFloat3(vr.x, vr.y, vr.z);
			rhop[p] = vr.w;
		}
	}
	else {
		if (vel) for (unsigned p = 0; p<n; p++) { tfloat4 vr = Velrhopc[p + pini]; vel[p] = TFloat3(vr.x, vr.y, vr.z); }
		if (rhop)for (unsigned p = 0; p<n; p++)rhop[p] = Velrhopc[p + pini].w;
	}
	//-Eliminate non-normal particles (periodic & others). | Elimina particulas no normales (periodicas y otras).
	if (onlynormal) {
		if (!idp || !pos || !vel || !rhop)RunException(met, "Pointers without data.");
		typecode *code2 = code;
		if (!code2) {
			code2 = ArraysCpu->ReserveTypeCode();
			memcpy(code2, Codec + pini, sizeof(typecode)*n);
		}
		unsigned ndel = 0;
		for (unsigned p = 0; p<n; p++) {
			bool normal = CODE_IsNormal(code2[p]);
			if (ndel && normal) {
				const unsigned pdel = p - ndel;
				idp[pdel] = idp[p];
				pos[pdel] = pos[p];
				vel[pdel] = vel[p];
				rhop[pdel] = rhop[p];
				code2[pdel] = code2[p];
			}
			if (!normal)ndel++;
		}
		num -= ndel;
		if (!code)ArraysCpu->Free(code2);
	}
	//-Reorder components in their original order. | Reordena componentes en su orden original.
	if (cellorderdecode)DecodeCellOrder(n, pos, vel);
	return(num);
}

//==============================================================================
/// Collect data from a range of particles and return the number of particles that 
/// will be less than n and eliminate the periodic ones
/// - cellorderdecode: Reorder components of position (pos) and velocity (vel) according to CellOrder.
/// - onlynormal: Only keep the normal ones and eliminate the periodic particles.
///
/// Recupera datos de un rango de particulas y devuelve el numero de particulas que
/// sera menor que n si se eliminaron las periodicas.
/// - cellorderdecode: Reordena componentes de pos y vel segun CellOrder.
/// - onlynormal: Solo se queda con las normales, elimina las particulas periodicas.
//==============================================================================
unsigned JSphSolidCpu::GetParticlesData_M(unsigned n, unsigned pini, bool cellorderdecode, bool onlynormal
	, unsigned *idp, tdouble3 *pos, tfloat3 *vel, float *rhop, float *pore, tfloat3 *press, float* mass, tsymatrix3f *tau, typecode *code)
{
	const char met[] = "GetParticlesData";
	unsigned num = n;
	//-Copy selected values.
	if (code)memcpy(code, Codec + pini, sizeof(typecode)*n);
	if (idp)memcpy(idp, Idpc + pini, sizeof(unsigned)*n);
	if (pos)memcpy(pos, Posc + pini, sizeof(tdouble3)*n);
	if (vel && rhop) {
		for (unsigned p = 0; p<n; p++) {
			tfloat4 vr = Velrhopc[p + pini];
			vel[p] = TFloat3(vr.x, vr.y, vr.z);
			rhop[p] = vr.w;
		}
	}
	else {
		if (vel) for (unsigned p = 0; p<n; p++) { tfloat4 vr = Velrhopc[p + pini]; vel[p] = TFloat3(vr.x, vr.y, vr.z); }
		if (rhop)for (unsigned p = 0; p<n; p++)rhop[p] = Velrhopc[p + pini].w;
	}

	// Matthias
	if (pore)memcpy(pore, Porec_M + pini, sizeof(float)*n); 
	/*if (press) {
		for (unsigned p = 0; p<n; p++) {
			//tfloat3 pre = AnisotropyK_M * TFloat3(CteB * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f));
			tfloat3 pre =CteB3D * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f);
			press[p] = TFloat3(pre.x, pre.y, pre.z);
		}
	}*/
	//if (press)memcpy(press, Press3Dc + pini, sizeof(tfloat3)*n); // Not used, but Pressure seems to be recorded anyway, as well for tau
	if (mass)memcpy(mass, Massc_M + pini, sizeof(float)*n);
	if (tau)memcpy(tau, Tauc_M + pini, sizeof(tsymatrix3f)*n);

	//-Eliminate non-normal particles (periodic & others). | Elimina particulas no normales (periodicas y otras).
	if (onlynormal) {
		if (!idp || !pos || !vel || !rhop)RunException(met, "Pointers without data.");
		typecode *code2 = code;
		if (!code2) {
			code2 = ArraysCpu->ReserveTypeCode();
			memcpy(code2, Codec + pini, sizeof(typecode)*n);
		}
		unsigned ndel = 0;
		for (unsigned p = 0; p<n; p++) {
			bool normal = CODE_IsNormal(code2[p]);
			if (ndel && normal) {
				const unsigned pdel = p - ndel;
				idp[pdel] = idp[p];
				pos[pdel] = pos[p];
				vel[pdel] = vel[p];
				rhop[pdel] = rhop[p];
				code2[pdel] = code2[p];
			}
			if (!normal)ndel++;
		}
		num -= ndel;
		if (!code)ArraysCpu->Free(code2);
	}
	//-Reorder components in their original order. | Reordena componentes en su orden original.
	if (cellorderdecode)DecodeCellOrder(n, pos, vel);
	return(num);
}

unsigned JSphSolidCpu::GetParticlesData_M(unsigned n, unsigned pini, bool cellorderdecode, bool onlynormal
	, unsigned *idp, tdouble3 *pos, tfloat3 *vel, float *rhop, float *pore, tfloat3 *press, float* mass, tsymatrix3f *gradvel, tsymatrix3f *tau, typecode *code)
{
	const char met[] = "GetParticlesData";
	unsigned num = n;
	//-Copy selected values.
	if (code)memcpy(code, Codec + pini, sizeof(typecode)*n);
	if (idp)memcpy(idp, Idpc + pini, sizeof(unsigned)*n);
	if (pos)memcpy(pos, Posc + pini, sizeof(tdouble3)*n);
	if (vel && rhop) {
		for (unsigned p = 0; p<n; p++) {
			tfloat4 vr = Velrhopc[p + pini];
			vel[p] = TFloat3(vr.x, vr.y, vr.z);
			rhop[p] = vr.w;
		}
	}
	else {
		if (vel) for (unsigned p = 0; p<n; p++) { tfloat4 vr = Velrhopc[p + pini]; vel[p] = TFloat3(vr.x, vr.y, vr.z); }
		if (rhop)for (unsigned p = 0; p<n; p++)rhop[p] = Velrhopc[p + pini].w;
	}

	// Matthias
	if (pore)memcpy(pore, Porec_M + pini, sizeof(float)*n);
	if (press) {
		for (unsigned p = 0; p<n; p++) {
			//tfloat3 pre = AnisotropyK_M * TFloat3(CteB * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f));
			tfloat3 pre = CteB_M * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f);
			press[p] = TFloat3(pre.x, pre.y, pre.z);
		}
	}
	//if (press)memcpy(press, Press3Dc + pini, sizeof(tfloat3)*n); // Not used, but Pressure seems to be recorded anyway, as well for tau
	if (mass)memcpy(mass, Massc_M + pini, sizeof(float)*n);
	if (tau)memcpy(tau, Tauc_M + pini, sizeof(tsymatrix3f)*n);
	//if (gradvel)memcpy(tau, Tauc_M + pini, sizeof(tsymatrix3f)*n);
	//if (gradvel)memcpy(gradvel, StrainDot_M + pini, sizeof(tsymatrix3f)*n);

	//-Eliminate non-normal particles (periodic & others). | Elimina particulas no normales (periodicas y otras).
	if (onlynormal) {
		if (!idp || !pos || !vel || !rhop)RunException(met, "Pointers without data.");
		typecode *code2 = code;
		if (!code2) {
			code2 = ArraysCpu->ReserveTypeCode();
			memcpy(code2, Codec + pini, sizeof(typecode)*n);
		}
		unsigned ndel = 0;
		for (unsigned p = 0; p<n; p++) {
			bool normal = CODE_IsNormal(code2[p]);
			if (ndel && normal) {
				const unsigned pdel = p - ndel;
				idp[pdel] = idp[p];
				pos[pdel] = pos[p];
				vel[pdel] = vel[p];
				rhop[pdel] = rhop[p];
				code2[pdel] = code2[p];
			}
			if (!normal)ndel++;
		}
		num -= ndel;
		if (!code)ArraysCpu->Free(code2);
	}
	//-Reorder components in their original order. | Reordena componentes en su orden original.
	if (cellorderdecode)DecodeCellOrder(n, pos, vel);
	return(num);
}

//////////////////////////////////////
// Surcharge of GetParticlesData_M with Qf -- Matthias
//////////////////////////////////////
unsigned JSphSolidCpu::GetParticlesData_M(unsigned n, unsigned pini, bool cellorderdecode, bool onlynormal
	, unsigned *idp, tdouble3 *pos, tfloat3 *vel, float *rhop, float *pore, float *press, float* mass, tsymatrix3f *qf, typecode *code)
{
	const char met[] = "GetParticlesData";
	unsigned num = n;
	//-Copy selected values.
	if (code)memcpy(code, Codec + pini, sizeof(typecode)*n);
	if (idp)memcpy(idp, Idpc + pini, sizeof(unsigned)*n);
	if (pos)memcpy(pos, Posc + pini, sizeof(tdouble3)*n);
	if (vel && rhop) {
		for (unsigned p = 0; p<n; p++) {
			tfloat4 vr = Velrhopc[p + pini];
			vel[p] = TFloat3(vr.x, vr.y, vr.z);
			rhop[p] = vr.w;
		}
	}
	else {
		if (vel) for (unsigned p = 0; p<n; p++) { tfloat4 vr = Velrhopc[p + pini]; vel[p] = TFloat3(vr.x, vr.y, vr.z); }
		if (rhop)for (unsigned p = 0; p<n; p++)rhop[p] = Velrhopc[p + pini].w;
	}

	// Matthias
	if (pore)memcpy(pore, Porec_M + pini, sizeof(float)*n);
	if (press) {
		for (unsigned p = 0; p<n; p++) {
			press[p] = CteB * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f);
		}
	}
	if (mass)memcpy(mass, Massc_M + pini, sizeof(float)*n);
	if (qf)memcpy(qf, QuadFormc_M + pini, sizeof(tsymatrix3f)*n);

	//-Eliminate non-normal particles (periodic & others). | Elimina particulas no normales (periodicas y otras).
	if (onlynormal) {
		printf("NonNormalPart_SavePart\n");
		if (!idp || !pos || !vel || !rhop)RunException(met, "Pointers without data.");
		typecode *code2 = code;
		if (!code2) {
			code2 = ArraysCpu->ReserveTypeCode();
			memcpy(code2, Codec + pini, sizeof(typecode)*n);
		}
		unsigned ndel = 0;
		for (unsigned p = 0; p<n; p++) {
			bool normal = CODE_IsNormal(code2[p]);
			if (ndel && normal) {
				const unsigned pdel = p - ndel;
				idp[pdel] = idp[p];
				pos[pdel] = pos[p];
				vel[pdel] = vel[p];
				rhop[pdel] = rhop[p];
				code2[pdel] = code2[p];
			}
			if (!normal)ndel++;
		}
		num -= ndel;
		if (!code)ArraysCpu->Free(code2);
	}
	//-Reorder components in their original order. | Reordena componentes en su orden original.
	if (cellorderdecode)DecodeCellOrder(n, pos, vel);
	return(num);
}


//==============================================================================
/// ParticleData_M including Ellipsoid quadratic Form -- Matthias
//==============================================================================
unsigned JSphSolidCpu::GetParticlesData_M(unsigned n, unsigned pini, bool cellorderdecode, bool onlynormal
	, unsigned *idp, tdouble3 *pos, tfloat3 *vel, float *rhop, float *pore, tfloat3 *press, float* mass, tsymatrix3f *gradvel, tsymatrix3f *tau, tsymatrix3f *qf, typecode *code)
{
	const char met[] = "GetParticlesData";
	unsigned num = n;
	//-Copy selected values.
	if (code)memcpy(code, Codec + pini, sizeof(typecode)*n);
	if (idp)memcpy(idp, Idpc + pini, sizeof(unsigned)*n);
	if (pos)memcpy(pos, Posc + pini, sizeof(tdouble3)*n);
	if (vel && rhop) {
		for (unsigned p = 0; p<n; p++) {
			tfloat4 vr = Velrhopc[p + pini];
			vel[p] = TFloat3(vr.x, vr.y, vr.z);
			rhop[p] = vr.w;
		}
	}
	else {
		if (vel) for (unsigned p = 0; p<n; p++) { tfloat4 vr = Velrhopc[p + pini]; vel[p] = TFloat3(vr.x, vr.y, vr.z); }
		if (rhop)for (unsigned p = 0; p<n; p++)rhop[p] = Velrhopc[p + pini].w;
	}

	// Matthias
	if (pore)memcpy(pore, Porec_M + pini, sizeof(float)*n);
	if (press) {
		for (unsigned p = 0; p<n; p++) {
			//tfloat3 pre = AnisotropyK_M * TFloat3(CteB * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f));
			tfloat3 pre = CteB_M * (pow(Velrhopc[p + pini].w / RhopZero, Gamma) - 1.0f);
			press[p] = TFloat3(pre.x, pre.y, pre.z);
		}
	}
	//if (press)memcpy(press, Press3Dc + pini, sizeof(tfloat3)*n); // Not used, but Pressure seems to be recorded anyway, as well for tau
	if (mass)memcpy(mass, Massc_M + pini, sizeof(float)*n);
	if (tau)memcpy(tau, Tauc_M + pini, sizeof(tsymatrix3f)*n);
	//if (gradvel)memcpy(tau, StrainDotc_M + pini, sizeof(tsymatrix3f)*n);
	if (qf)memcpy(qf, QuadFormc_M + pini, sizeof(tsymatrix3f)*n);
	//if (gradvel)memcpy(gradvel, StrainDot_M + pini, sizeof(tsymatrix3f)*n);

	//-Eliminate non-normal particles (periodic & others). | Elimina particulas no normales (periodicas y otras).
	if (onlynormal) {
		if (!idp || !pos || !vel || !rhop)RunException(met, "Pointers without data.");
		typecode *code2 = code;
		if (!code2) {
			code2 = ArraysCpu->ReserveTypeCode();
			memcpy(code2, Codec + pini, sizeof(typecode)*n);
		}
		unsigned ndel = 0;
		for (unsigned p = 0; p<n; p++) {
			bool normal = CODE_IsNormal(code2[p]);
			if (ndel && normal) {
				const unsigned pdel = p - ndel;
				idp[pdel] = idp[p];
				pos[pdel] = pos[p];
				vel[pdel] = vel[p];
				rhop[pdel] = rhop[p];
				code2[pdel] = code2[p];
			}
			if (!normal)ndel++;
		}
		num -= ndel;
		if (!code)ArraysCpu->Free(code2);
	}
	//-Reorder components in their original order. | Reordena componentes en su orden original.
	if (cellorderdecode)DecodeCellOrder(n, pos, vel);
	return(num);
}

//==============================================================================
/// Load the execution configuration with OpenMP.
/// Carga la configuracion de ejecucion con OpenMP.
//==============================================================================
void JSphSolidCpu::ConfigOmp(const JCfgRun *cfg) {
#ifdef OMP_USE
	//-Determine number of threads for host with OpenMP. | Determina numero de threads por host con OpenMP.
	if (Cpu && cfg->OmpThreads != 1) {
		OmpThreads = cfg->OmpThreads;
		if (OmpThreads <= 0)OmpThreads = max(omp_get_num_procs(), 1);
		if (OmpThreads>OMP_MAXTHREADS)OmpThreads = OMP_MAXTHREADS;
		omp_set_num_threads(OmpThreads);
		Log->Printf("Threads by host for parallel execution: %d", omp_get_max_threads());
	}
	else {
		OmpThreads = 1;
		omp_set_num_threads(OmpThreads);
	}
#else
	OmpThreads = 1;
#endif
}

//==============================================================================
/// Configures execution mode in CPU.
/// Configura modo de ejecucion en CPU.
//==============================================================================
void JSphSolidCpu::ConfigRunMode(const JCfgRun *cfg, std::string preinfo) {
	//#ifndef WIN32  //-Error compilation when gcc5 is used.
	//  const int len=128; char hname[len];
	//  gethostname(hname,len);
	//  if(!preinfo.empty())preinfo=preinfo+", ";
	//  preinfo=preinfo+"HostName:"+hname;
	//#endif
	Hardware = "Cpu";
	if (OmpThreads == 1)RunMode = "Single core";
	else RunMode = string("OpenMP(Threads:") + fun::IntStr(OmpThreads) + ")";
	if (!preinfo.empty())RunMode = preinfo + " - " + RunMode;
	if (Stable)RunMode = string("Stable - ") + RunMode;
	if (Psingle)RunMode = string("Pos-Single - ") + RunMode;
	else RunMode = string("Pos-Double - ") + RunMode;
	Log->Print(" ");
	Log->Print(fun::VarStr("RunMode", RunMode));
	Log->Print(" ");
}

//==============================================================================
/// Initialisation of arrays and variables for execution.
/// Inicializa vectores y variables para la ejecucion.
//==============================================================================
void JSphSolidCpu::InitRun() {
	const char met[] = "InitRun";
	WithFloating = (CaseNfloat>0);
	if (TStep == STEP_Verlet) {
		memcpy(VelrhopM1c, Velrhopc, sizeof(tfloat4)*Np);
		memset(TauM1c_M, 0, sizeof(tsymatrix3f)*Np);
		VerletStep = 0;
		for (unsigned p = 0; p < Np; p++) {
			MassM1c_M[p] = MassFluid;
			QuadFormM1c_M[p] = TSymatrix3f(4 / float(pow(Dp, 2)), 0, 0, 4 / float(pow(Dp, 2)), 0, 4 / float(pow(Dp, 2)));
		}
	}
	else if (TStep == STEP_Symplectic)DtPre = DtIni;
	if (TVisco == VISCO_LaminarSPS)memset(SpsTauc, 0, sizeof(tsymatrix3f)*Np);

	// Matthias
	memset(Tauc_M, 0, sizeof(tsymatrix3f)*Np);
	memset(Divisionc_M, 0, sizeof(bool)*Np);
	for (unsigned p = 0; p < Np; p++) {
		Massc_M[p] = MassFluid;	
		QuadFormc_M[p] = TSymatrix3f(4 / float(pow(Dp, 2)), 0, 0, 4 / float(pow(Dp, 2)), 0, 4 / float(pow(Dp, 2)));
	}
	  
	if (UseDEM)DemDtForce = DtIni; //(DEM)
	if (CaseNfloat)InitFloating();

	//-Adjust paramaters to start.
	PartIni = PartBeginFirst;
	TimeStepIni = (!PartIni ? 0 : PartBeginTimeStep);
	//-Adjust motion for the instant of the loaded PART.
	if (CaseNmoving) {
		MotionTimeMod = (!PartIni ? PartBeginTimeStep : 0);
		Motion->ProcesTime(JSphMotion::MOMT_Simple, 0, TimeStepIni + MotionTimeMod);
	}

	//-Uses Inlet information from PART read.
	if (PartBeginTimeStep && PartBeginTotalNp) {
		TotalNp = PartBeginTotalNp;
		IdMax = unsigned(TotalNp - 1);
	}

	//-Shows Initialize configuration.
	if (InitializeInfo.size()) {
		Log->Print("Initialization configuration:");
		Log->Print(InitializeInfo);
		Log->Print(" ");
	}

	//-Process Special configurations in XML.
	JXml xml; xml.LoadFile(FileXml);

	//-Configuration of GaugeSystem.
	GaugeSystem->Config(Simulate2D, Simulate2DPosY, TimeMax, TimePart, Dp, DomPosMin, DomPosMax, Scell, Hdiv, H, MassFluid);
	if (xml.GetNode("case.execution.special.gauges", false))GaugeSystem->LoadXml(&xml, "case.execution.special.gauges");

	//-Prepares WaveGen configuration.
	if (WaveGen) {
		Log->Print("Wave paddles configuration:");
		WaveGen->Init(GaugeSystem, MkInfo, TimeMax, Gravity);
		WaveGen->VisuConfig("", " ");
	}

	//-Prepares Damping configuration.
	if (Damping) {
		Damping->Config(CellOrder);
		Damping->VisuConfig("Damping configuration:", " ");
	}

	//-Prepares AccInput configuration.
	if (AccInput) {
		Log->Print("AccInput configuration:");
		AccInput->Init(TimeMax);
		AccInput->VisuConfig("", " ");
	}

	//-Configuration of SaveDt.
	if (xml.GetNode("case.execution.special.savedt", false)) {
		SaveDt = new JSaveDt(Log);
		SaveDt->Config(&xml, "case.execution.special.savedt", TimeMax, TimePart);
		SaveDt->VisuConfig("SaveDt configuration:", " ");
	}

	//-Shows configuration of JGaugeSystem.
	if (GaugeSystem->GetCount())GaugeSystem->VisuConfig("GaugeSystem configuration:", " ");

	//-Shows configuration of JTimeOut.
	if (TimeOut->UseSpecialConfig())TimeOut->VisuConfig(Log, "TimeOut configuration:", " ");

	Part = PartIni; Nstep = 0; PartNstep = 0; PartOut = 0;
	TimeStep = TimeStepIni; TimeStepM1 = TimeStep;
	if (DtFixed)DtIni = DtFixed->GetDt(TimeStep, DtIni);
	TimePartNext = TimeOut->GetNextTime(TimeStep);
}

//==============================================================================
/// Initialisation of arrays and variables for execution.
/// Inicializa vectores y variables para la ejecucion.
//==============================================================================
void JSphSolidCpu::InitRun_T(JPartsLoad4 *pl) {
	const char met[] = "InitRun";

	WithFloating = (CaseNfloat>0);
	if (TStep == STEP_Verlet) {
		memcpy(VelrhopM1c, Velrhopc, sizeof(tfloat4)*Np);
		memset(TauM1c_M, 0, sizeof(tsymatrix3f)*Np);
		memset(QuadFormM1c_M, 0, sizeof(tsymatrix3f)*Np);
		VerletStep = 0;
		for (unsigned p = 0; p < Np; p++) {
			MassM1c_M[p] = MassFluid;
			QuadFormM1c_M[p] = TSymatrix3f(4 / float(pow(Dp, 2)), 0, 0, 4 / float(pow(Dp, 2)), 0, 4 / float(pow(Dp, 2)));
		}
	}
	else if (TStep == STEP_Symplectic)DtPre = DtIni;
	if (TVisco == VISCO_LaminarSPS)memset(SpsTauc, 0, sizeof(tsymatrix3f)*Np);

	// Matthias
	memset(Tauc_M, 0, sizeof(tsymatrix3f)*Np);
	memset(Divisionc_M, 0, sizeof(bool)*Np);
	for (unsigned p = 0; p < Np; p++) {
		Massc_M[p] = MassFluid;
		QuadFormc_M[p] = TSymatrix3f(4 / float(pow(Dp, 2)), 0, 0, 4 / float(pow(Dp, 2)), 0, 4 / float(pow(Dp, 2)));
	}

	if (UseDEM)DemDtForce = DtIni; //(DEM)
	if (CaseNfloat)InitFloating();

	//-Adjust paramaters to start.
	PartIni = PartBeginFirst;
	TimeStepIni = (!PartIni ? 0 : PartBeginTimeStep);
	//-Adjust motion for the instant of the loaded PART.
	if (CaseNmoving) {
		MotionTimeMod = (!PartIni ? PartBeginTimeStep : 0);
		Motion->ProcesTime(JSphMotion::MOMT_Simple, 0, TimeStepIni + MotionTimeMod);
	}

	//-Uses Inlet information from PART read.
	if (PartBeginTimeStep && PartBeginTotalNp) {
		TotalNp = PartBeginTotalNp;
		IdMax = unsigned(TotalNp - 1);
	}

	//-Shows Initialize configuration.
	if (InitializeInfo.size()) {
		Log->Print("Initialization configuration:");
		Log->Print(InitializeInfo);
		Log->Print(" ");
	}

	//-Process Special configurations in XML.
	JXml xml; xml.LoadFile(FileXml);

	//-Configuration of GaugeSystem.
	GaugeSystem->Config(Simulate2D, Simulate2DPosY, TimeMax, TimePart, Dp, DomPosMin, DomPosMax, Scell, Hdiv, H, MassFluid);
	if (xml.GetNode("case.execution.special.gauges", false))GaugeSystem->LoadXml(&xml, "case.execution.special.gauges");

	//-Prepares WaveGen configuration.
	if (WaveGen) {
		Log->Print("Wave paddles configuration:");
		WaveGen->Init(GaugeSystem, MkInfo, TimeMax, Gravity);
		WaveGen->VisuConfig("", " ");
	}

	//-Prepares Damping configuration.
	if (Damping) {
		Damping->Config(CellOrder);
		Damping->VisuConfig("Damping configuration:", " ");
	}

	//-Prepares AccInput configuration.
	if (AccInput) {
		Log->Print("AccInput configuration:");
		AccInput->Init(TimeMax);
		AccInput->VisuConfig("", " ");
	}

	//-Configuration of SaveDt.
	if (xml.GetNode("case.execution.special.savedt", false)) {
		SaveDt = new JSaveDt(Log);
		SaveDt->Config(&xml, "case.execution.special.savedt", TimeMax, TimePart);
		SaveDt->VisuConfig("SaveDt configuration:", " ");
	}

	//-Shows configuration of JGaugeSystem.
	if (GaugeSystem->GetCount())GaugeSystem->VisuConfig("GaugeSystem configuration:", " ");

	//-Shows configuration of JTimeOut.
	if (TimeOut->UseSpecialConfig())TimeOut->VisuConfig(Log, "TimeOut configuration:", " ");

	Part = PartIni; Nstep = 0; PartNstep = 0; PartOut = 0;
	TimeStep = TimeStepIni; TimeStepM1 = TimeStep;
	if (DtFixed)DtIni = DtFixed->GetDt(TimeStep, DtIni);
	TimePartNext = TimeOut->GetNextTime(TimeStep);
}

//==============================================================================
/// Adds variable acceleration from input files.
//==============================================================================
void JSphSolidCpu::AddAccInput() {
	for (unsigned c = 0; c<AccInput->GetCount(); c++) {
		unsigned mkfluid;
		tdouble3 acclin, accang, centre, velang, vellin;
		bool setgravity;
		AccInput->GetAccValues(c, TimeStep, mkfluid, acclin, accang, centre, velang, vellin, setgravity);
		const bool withaccang = (accang.x != 0 || accang.y != 0 || accang.z != 0);
		const typecode codesel = typecode(mkfluid);
		const int npb = int(Npb), np = int(Np);
#ifdef OMP_USE
#pragma omp parallel for schedule (static)
#endif
		for (int p = npb; p<np; p++) {//-Iterates through the fluid particles.
									  //-Checks if the current particle is part of the particle set by its MK.
			if (CODE_GetTypeValue(Codec[p]) == codesel) {
				tdouble3 acc = ToTDouble3(Acec[p]);
				acc = acc + acclin;                             //-Adds linear acceleration.
				if (!setgravity)acc = acc - ToTDouble3(Gravity); //-Subtract global gravity from the acceleration if it is set in the input file
				if (withaccang) {                             //-Adds angular acceleration.
					const tdouble3 dc = Posc[p] - centre;
					const tdouble3 vel = TDouble3(Velrhopc[p].x, Velrhopc[p].y, Velrhopc[p].z);//-Get the current particle's velocity

																							   //-Calculate angular acceleration ((Dw/Dt) x (r_i - r)) + (w x (w x (r_i - r))) + (2w x (v_i - v))
																							   //(Dw/Dt) x (r_i - r) (term1)
					acc.x += (accang.y*dc.z) - (accang.z*dc.y);
					acc.y += (accang.z*dc.x) - (accang.x*dc.z);
					acc.z += (accang.x*dc.y) - (accang.y*dc.x);

					//-Centripetal acceleration (term2)
					//-First find w x (r_i - r))
					const double innerx = (velang.y*dc.z) - (velang.z*dc.y);
					const double innery = (velang.z*dc.x) - (velang.x*dc.z);
					const double innerz = (velang.x*dc.y) - (velang.y*dc.x);
					//-Find w x inner.
					acc.x += (velang.y*innerz) - (velang.z*innery);
					acc.y += (velang.z*innerx) - (velang.x*innerz);
					acc.z += (velang.x*innery) - (velang.y*innerx);

					//-Coriolis acceleration 2w x (v_i - v) (term3)
					acc.x += ((2.0*velang.y)*vel.z) - ((2.0*velang.z)*(vel.y - vellin.y));
					acc.y += ((2.0*velang.z)*vel.x) - ((2.0*velang.x)*(vel.z - vellin.z));
					acc.z += ((2.0*velang.x)*vel.y) - ((2.0*velang.y)*(vel.x - vellin.x));
				}
				//-Stores the new acceleration value.
				Acec[p] = ToTFloat3(acc);
			}
		}
	}
}

//==============================================================================
/// Prepare variables for interaction functions "INTER_Forces" or "INTER_ForcesCorr".
/// Prepara variables para interaccion "INTER_Forces" o "INTER_ForcesCorr".
//==============================================================================
void JSphSolidCpu::PreInteractionVars_Forces(TpInter tinter, unsigned np, unsigned npb) {
	//-Initialize Arrays.
	const unsigned npf = np - npb;
	memset(Arc, 0, sizeof(float)*np);                                    //Arc[]=0
	if (Deltac)memset(Deltac, 0, sizeof(float)*np);                       //Deltac[]=0
	if (ShiftPosc)memset(ShiftPosc, 0, sizeof(tfloat3)*np);               //ShiftPosc[]=0
	if (ShiftDetectc)memset(ShiftDetectc, 0, sizeof(float)*np);           //ShiftDetectc[]=0
	memset(Acec, 0, sizeof(tfloat3)*npb);                                //Acec[]=(0,0,0) for bound / para bound
	for (unsigned p = npb; p<np; p++)Acec[p] = Gravity;                       //Acec[]=Gravity for fluid / para fluid
	if (SpsGradvelc)memset(SpsGradvelc + npb, 0, sizeof(tsymatrix3f)*npf);  //SpsGradvelc[]=(0,0,0,0,0,0).
		
	// Matthias													
	//memset(JauGradvelc_M + npb, 0, sizeof(tmatrix3f)*npf);  //JauGradvelc[]=(0,0,0,0,0,0).													
	memset(StrainDotc_M + npb, 0, sizeof(tsymatrix3f)*npf);  													
	memset(TauDotc_M + npb, 0, sizeof(tsymatrix3f)*npf);  												
	memset(Spinc_M + npb, 0, sizeof(tsymatrix3f)*npf); 
	memset(L_M + npb, 0, sizeof(tmatrix3f)*npf); 
																			  //-Apply the extra forces to the correct particle sets.
	if (AccInput)AddAccInput();

	//-Prepare values of rhop for interaction. | Prepara datos derivados de rhop para interaccion.
	const int n = int(np);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(n>OMP_LIMIT_COMPUTELIGHT)
#endif
	for (int p = 0; p<n; p++) {
		const float rhop = Velrhopc[p].w, rhop_r0 = rhop / RhopZero;
		Pressc[p] = CteB * (pow(rhop_r0, Gamma) - 1.0f);
		// Matthias
		Press3Dc_M[p] = CteB_M * (pow(rhop_r0, Gamma) - 1.0f);
		/*if ((abs(Posc[p].x) < 0.5f) && (abs(Posc[p].y) < 0.5f) && (abs(Posc[p].z) < 0.5f)) {
			Porec_M[p] = PoreZero;
		}
		else {
			Porec_M[p] = 0.0f;
		}*/
		Porec_M[p] = PoreZero;
		//printf("Pore = %.8f\n", Porec_M[p]);
		
		//Porec_M[p] = PoreZero * pow(1 - Massc_M[p] / MassFluid, 3);
		//Porec_M[p] = PoreZero * Posc[p].x * exp(-(pow(Posc[p].x - LocDiv_M.x, 2) + pow(Posc[p].y - LocDiv_M.y, 2) + pow(Posc[p].z - LocDiv_M.z, 2)) / Spread_M);
		//tfloat3 distance2 = ToTFloat3(Posc[p] - LocDiv_M);
		//Porec_M[p] = PoreZero / (1 + exp(-(TimeStep-2))) * exp(-(pow(distance2.x,2) + pow(distance2.y, 2) + pow(distance2.z, 2)) / Spread_M);
		// Porec_M[p] = PoreZero  / sqrt(2*Spread_M*PI) * exp(-(pow(distance2.x,2) + pow(distance2.y, 2) + pow(distance2.z, 2)) / Spread_M);
		//Porec_M[p] = PoreZero  / sqrt(2*Spread_M*PI) * exp(-(pow(distance2.x,2)) / Spread_M);
		
	}
}

//==============================================================================
/// Prepare variables for interaction functions "INTER_Forces" or "INTER_ForcesCorr".
/// Prepara variables para interaccion "INTER_Forces" o "INTER_ForcesCorr".
//==============================================================================
void JSphSolidCpu::PreInteraction_Forces(TpInter tinter) {
	TmcStart(Timers, TMC_CfPreForces);
	//-Assign memory.
	Arc = ArraysCpu->ReserveFloat();
	Acec = ArraysCpu->ReserveFloat3();
	if (TDeltaSph == DELTA_DynamicExt)Deltac = ArraysCpu->ReserveFloat();
	if (TShifting != SHIFT_None) {
		ShiftPosc = ArraysCpu->ReserveFloat3();
		if (ShiftTFS)ShiftDetectc = ArraysCpu->ReserveFloat();
	}
	Pressc = ArraysCpu->ReserveFloat();
	Press3Dc_M = ArraysCpu->ReserveFloat3();
	// Matthias
	//Porec_M = ArraysCpu->ReserveFloat();
	//Amassc_M = ArraysCpu->ReserveFloat();

	if (TVisco == VISCO_LaminarSPS)SpsGradvelc = ArraysCpu->ReserveSymatrix3f();

	// Matthias
	//JauGradvelc_M = ArraysCpu->ReserveMatrix3f_M();
	StrainDotc_M = ArraysCpu->ReserveSymatrix3f();
	TauDotc_M = ArraysCpu->ReserveSymatrix3f();
	Spinc_M = ArraysCpu->ReserveSymatrix3f();
	L_M = ArraysCpu->ReserveMatrix3f_M(); // A verifier

	//-Prepare values for interaction Pos-Simpe.
	if (Psingle) {
		PsPosc = ArraysCpu->ReserveFloat3();
		const int np = int(Np);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(np>OMP_LIMIT_COMPUTELIGHT)
#endif
		for (int p = 0; p<np; p++) { PsPosc[p] = ToTFloat3(Posc[p]); }
	}
	//-Initialize Arrays
	PreInteractionVars_Forces(tinter, Np, Npb);

	//-Calculate VelMax: Floating object particles are included and do not affect use of periodic condition.
	//-Calcula VelMax: Se incluyen las particulas floatings y no afecta el uso de condiciones periodicas.
	const unsigned pini = (DtAllParticles ? 0 : Npb);
	VelMax = CalcVelMaxOmp(Np - pini, Velrhopc + pini);
	//printf("Velmax: %.3f\n", VelMax);
	ViscDtMax = 0;
	TmcStop(Timers, TMC_CfPreForces);
}

//==============================================================================
/// Returns maximum velocity from an array tfloat4.
/// Devuelve la velociad maxima de un array tfloat4.
//==============================================================================
float JSphSolidCpu::CalcVelMaxSeq(unsigned np, const tfloat4* velrhop)const {
	float velmax = 0;
	for (unsigned p = 0; p<np; p++) {
		const tfloat4 v = velrhop[p];
		const float v2 = v.x*v.x + v.y*v.y + v.z*v.z;
		velmax = max(velmax, v2);
	}
	return(sqrt(velmax));
}

//==============================================================================
/// Returns maximum velocity from an array tfloat4 using OpenMP.
/// Devuelve la velociad maxima de un array tfloat4 usando OpenMP.
//==============================================================================
float JSphSolidCpu::CalcVelMaxOmp(unsigned np, const tfloat4* velrhop)const {
	const char met[] = "CalcVelMax";
	float velmax = 0;
#ifdef OMP_USE
	if (np>OMP_LIMIT_COMPUTELIGHT) {
		const int n = int(np);
		if (n<0)RunException(met, "Number of values is too big.");
		float vmax = 0;
#pragma omp parallel 
		{
			float vmax2 = 0;
#pragma omp for nowait
			for (int c = 0; c<n; ++c) {
				const tfloat4 v = velrhop[c];
				const float v2 = v.x*v.x + v.y*v.y + v.z*v.z;
				if (vmax2<v2)vmax2 = v2;
			}
#pragma omp critical 
			{
				if (vmax<vmax2)vmax = vmax2;
			}
		}
		//-Saves result.
		velmax = sqrt(vmax);
	}
	else if (np)velmax = CalcVelMaxSeq(np, velrhop);
#else
	if (np)velmax = CalcVelMaxSeq(np, velrhop);
#endif
	return(velmax);
}

//==============================================================================
/// Free memory assigned to ArraysCpu.
/// Libera memoria asignada de ArraysCpu.
//==============================================================================
void JSphSolidCpu::PosInteraction_Forces() {
	//-Free memory assigned in PreInteraction_Forces(). | Libera memoria asignada en PreInteraction_Forces().
	ArraysCpu->Free(Arc);          Arc = NULL;
	ArraysCpu->Free(Acec);         Acec = NULL;
	ArraysCpu->Free(Deltac);       Deltac = NULL;
	ArraysCpu->Free(ShiftPosc);    ShiftPosc = NULL;
	ArraysCpu->Free(ShiftDetectc); ShiftDetectc = NULL;
	ArraysCpu->Free(Pressc);       Pressc = NULL;
	ArraysCpu->Free(Press3Dc_M);   Press3Dc_M = NULL;
	ArraysCpu->Free(PsPosc);       PsPosc = NULL;
	ArraysCpu->Free(SpsGradvelc);  SpsGradvelc = NULL;
	// Matthias
	//ArraysCpu->Free(Porec_M);       Porec_M = NULL; // Pending suppression of this line - Matthias
	//ArraysCpu->Free(JauGradvelc_M);  JauGradvelc_M = NULL;
	ArraysCpu->Free(StrainDotc_M); StrainDotc_M = NULL;
	ArraysCpu->Free(TauDotc_M);    TauDotc_M = NULL;
	ArraysCpu->Free(Spinc_M);	   Spinc_M = NULL;
	ArraysCpu->Free(L_M);		   L_M = NULL;

}

//==============================================================================
/// Returns values of kernel Wendland, gradients: frx, fry and frz.
/// Devuelve valores de kernel Wendland, gradients: frx, fry y frz.
//==============================================================================
void JSphSolidCpu::GetKernelWendland(float rr2, float drx, float dry, float drz
	, float &frx, float &fry, float &frz)const
{
	const float rad = sqrt(rr2);
	const float qq = rad / H;
	//-Wendland kernel.
	const float wqq1 = 1.f - 0.5f*qq;
	const float fac = Bwen * qq*wqq1*wqq1*wqq1 / rad;
	frx = fac * drx; fry = fac * dry; frz = fac * drz;
}

//==============================================================================
/// Returns values of kernel Gaussian, gradients: frx, fry and frz.
/// Devuelve valores de kernel Gaussian, gradients: frx, fry y frz.
//==============================================================================
void JSphSolidCpu::GetKernelGaussian(float rr2, float drx, float dry, float drz
	, float &frx, float &fry, float &frz)const
{
	const float rad = sqrt(rr2);
	const float qq = rad / H;
	//-Gaussian kernel.
	const float qqexp = -4.0f*qq*qq;
	//const float wab=Agau*expf(qqexp);
	const float fac = Bgau * qq*expf(qqexp) / rad;
	frx = fac * drx; fry = fac * dry; frz = fac * drz;
}

//==============================================================================
/// Return values of kernel Cubic without tensil correction, gradients: frx, fry and frz.
/// Devuelve valores de kernel Cubic sin correccion tensil, gradients: frx, fry y frz.
//==============================================================================
void JSphSolidCpu::GetKernelCubic(float rr2, float drx, float dry, float drz
	, float &frx, float &fry, float &frz)const
{
	const float rad = sqrt(rr2);
	const float qq = rad / H;
	//-Cubic Spline kernel.
	float fac;
	if (rad>H) {
		float wqq1 = 2.0f - qq;
		float wqq2 = wqq1 * wqq1;
		fac = CubicCte.c2*wqq2 / rad;
	}
	else {
		float wqq2 = qq * qq;
		fac = (CubicCte.c1*qq + CubicCte.d1*wqq2) / rad;
	}
	//-Gradients.
	frx = fac * drx; fry = fac * dry; frz = fac * drz;
}

//==============================================================================
/// Return tensil correction for kernel Cubic.
/// Devuelve correccion tensil para kernel Cubic.
//==============================================================================
float JSphSolidCpu::GetKernelCubicTensil(float rr2, float rhopp1, float pressp1, float rhopp2, float pressp2)const {
	const float rad = sqrt(rr2);
	const float qq = rad / H;
	//-Cubic Spline kernel.
	float wab;
	if (rad>H) {
		float wqq1 = 2.0f - qq;
		float wqq2 = wqq1 * wqq1;
		wab = CubicCte.a24*(wqq2*wqq1);
	}
	else {
		float wqq2 = qq * qq;
		float wqq3 = wqq2 * qq;
		wab = CubicCte.a2*(1.0f - 1.5f*wqq2 + 0.75f*wqq3);
	}
	//-Tensile correction.
	float fab = wab * CubicCte.od_wdeltap;
	fab *= fab; fab *= fab; //fab=fab^4
	const float tensilp1 = (pressp1 / (rhopp1*rhopp1))*(pressp1>0 ? 0.01f : -0.2f);
	const float tensilp2 = (pressp2 / (rhopp2*rhopp2))*(pressp2>0 ? 0.01f : -0.2f);
	return(fab*(tensilp1 + tensilp2));
}

//==============================================================================
/// Return cell limits for interaction starting from cell coordinates.
/// Devuelve limites de celdas para interaccion a partir de coordenadas de celda.
//==============================================================================
void JSphSolidCpu::GetInteractionCells(unsigned rcell
	, int hdiv, const tint4 &nc, const tint3 &cellzero
	, int &cxini, int &cxfin, int &yini, int &yfin, int &zini, int &zfin)const
{
	//-Get interaction limits. | Obtiene limites de interaccion.
	const int cx = PC__Cellx(DomCellCode, rcell) - cellzero.x;
	const int cy = PC__Celly(DomCellCode, rcell) - cellzero.y;
	const int cz = PC__Cellz(DomCellCode, rcell) - cellzero.z;
	//-Code for hdiv 1 or 2 but not zero. | Codigo para hdiv 1 o 2 pero no cero.
	cxini = cx - min(cx, hdiv);
	cxfin = cx + min(nc.x - cx - 1, hdiv) + 1;
	yini = cy - min(cy, hdiv);
	yfin = cy + min(nc.y - cy - 1, hdiv) + 1;
	zini = cz - min(cz, hdiv);
	zfin = cz + min(nc.z - cz - 1, hdiv) + 1;
}

//==============================================================================
/// Perform interaction between particles. Bound-Fluid/Float
/// Realiza interaccion entre particulas. Bound-Fluid/Float
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode> void JSphSolidCpu::InteractionForcesBound
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, float &viscdt, float *ar)const
{
	//-Initialize viscth to calculate max viscdt with OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Starts execution using OpenMP.
	const int pfin = int(pinit + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif
	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0;

		//-Load data of particle p1. | Carga datos de particula p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);

		//-Obtain limits of interaction. | Obtiene limites de interaccion.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

		//-Search for neighbours in adjacent cells. | Busqueda de vecinos en celdas adyacentes.
		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid cells. | Le suma donde empiezan las celdas de fluido.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				//-Interaction of boundary with type Fluid/Float | Interaccion de Bound con varias Fluid/Float.
				//---------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz;
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = MassFluid; //-Contains particle mass of incorrect fluid. | Contiene masa de particula por defecto fluid.
						bool compute = true;      //-Deactivate when using DEM and/or bound-float. | Se desactiva cuando se usa DEM y es bound-float.
						if (USE_FLOATING) {
							bool ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
							compute = !(USE_DEM && ftp2); //-Deactivate when using DEM and/or bound-float. | Se desactiva cuando se usa DEM y es bound-float.
						}

						if (compute) {
							//-Density derivative.
							const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
							if (compute)arp1 += massp2 * (dvx*frx + dvy * fry + dvz * frz);

							{//-Viscosity.
								const float dot = drx * dvx + dry * dvy + drz * dvz;
								const float dot_rr2 = dot / (rr2 + Eta2);
								visc = max(dot_rr2, visc);
							}
						}
					}
				}
			}
		}
		//-Sum results together. | Almacena resultados.
		if (arp1 || visc) {
			ar[p1] += arp1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;
		}
	}
	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}

//==============================================================================
/// Perform interaction between particles: Fluid/Float-Fluid/Float or Fluid/Float-Bound
/// Realiza interaccion entre particulas: Fluid/Float-Fluid/Float or Fluid/Float-Bound
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::InteractionForcesFluid
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial, float visco
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tsymatrix3f* tau, tsymatrix3f* gradvel
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press
	, float &viscdt, float *ar, tfloat3 *ace, float *delta
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const bool boundp2 = (!cellinitial); //-Interaction with type boundary (Bound). | Interaccion con Bound.
										 //-Initialize viscth to calculate viscdt maximo con OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP.
	const int pfin = int(pinit + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif

	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0, deltap1 = 0;
		tfloat3 acep1 = TFloat3(0);
		tsymatrix3f gradvelp1 = { 0,0,0,0,0,0 };
		tfloat3 shiftposp1 = TFloat3(0);
		float shiftdetectp1 = 0;

		//-Obtain data of particle p1 in case of floating objects. | Obtiene datos de particula p1 en caso de existir floatings.
		bool ftp1 = false;     //-Indicate if it is floating. | Indica si es floating.
		float ftmassp1 = 1.f;  //-Contains floating particle mass or 1.0f if it is fluid. | Contiene masa de particula floating o 1.0f si es fluid.
		if (USE_FLOATING) {
			ftp1 = CODE_IsFloating(code[p1]);
			if (ftp1)ftmassp1 = FtObjs[CODE_GetTypeValue(code[p1])].massp;
			if (ftp1 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
			if (ftp1 && shift)shiftposp1.x = FLT_MAX;  //-For floating objects do not calculate shifting. | Para floatings no se calcula shifting.
		}

		//-Obtain data of particle p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const float rhopp1 = velrhop[p1].w;
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
		const float pressp1 = press[p1];
		const tsymatrix3f taup1 = (lamsps ? tau[p1] : gradvelp1);

		//-Obtain interaction limits.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

		//-Search for neighbours in adjacent cells.
		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				//-Interaction of Fluid with type Fluid or Bound. | Interaccion de Fluid con varias Fluid o Bound.
				//------------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz;
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = (boundp2 ? MassBound : MassFluid); //-Contiene masa de particula segun sea bound o fluid.
						bool ftp2 = false;    //-Indicate if it is floating | Indica si es floating.
						bool compute = true;  //-Deactivate when using DEM and if it is of type float-float or float-bound | Se desactiva cuando se usa DEM y es float-float o float-bound.
						if (USE_FLOATING) {
							ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
#ifdef DELTA_HEAVYFLOATING
							if (ftp2 && massp2 <= (MassFluid*1.2f) && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#else
							if (ftp2 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#endif
							if (ftp2 && shift && tshifting == SHIFT_NoBound)shiftposp1.x = FLT_MAX; //-With floating objects do not use shifting. | Con floatings anula shifting.
							compute = !(USE_DEM && ftp1 && (boundp2 || ftp2)); //-Deactivate when using DEM and if it is of type float-float or float-bound. | Se desactiva cuando se usa DEM y es float-float o float-bound.
						}

						//===== Acceleration ===== 
						if (compute) {
							const float prs = (pressp1 + press[p2]) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0);
							const float p_vpm = -prs * massp2*ftmassp1;
							acep1.x += p_vpm * frx; acep1.y += p_vpm * fry; acep1.z += p_vpm * frz;
						}

						//-Density derivative.
						const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
						if (compute)arp1 += massp2 * (dvx*frx + dvy * fry + dvz * frz);

						const float cbar = (float)Cs0;
						//-Density derivative (DeltaSPH Molteni).
						if ((tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt) && deltap1 != FLT_MAX) {
							const float rhop1over2 = rhopp1 / velrhop[p2].w;
							const float visc_densi = Delta2H * cbar*(rhop1over2 - 1.f) / (rr2 + Eta2);
							const float dot3 = (drx*frx + dry * fry + drz * frz);
							const float delta = visc_densi * dot3*massp2;
							deltap1 = (boundp2 ? FLT_MAX : deltap1 + delta);
						}

						//-Shifting correction.
						if (shift && shiftposp1.x != FLT_MAX) {
							const float massrhop = massp2 / velrhop[p2].w;
							const bool noshift = (boundp2 && (tshifting == SHIFT_NoBound || (tshifting == SHIFT_NoFixed && CODE_IsFixed(code[p2]))));
							shiftposp1.x = (noshift ? FLT_MAX : shiftposp1.x + massrhop * frx); //-For boundary do not use shifting. | Con boundary anula shifting.
							shiftposp1.y += massrhop * fry;
							shiftposp1.z += massrhop * frz;
							shiftdetectp1 -= massrhop * (drx*frx + dry * fry + drz * frz);
						}

						//===== Viscosity ===== 
						if (compute) {
							const float dot = drx * dvx + dry * dvy + drz * dvz;
							const float dot_rr2 = dot / (rr2 + Eta2);
							visc = max(dot_rr2, visc);
							if (!lamsps) {//-Artificial viscosity.
								if (dot<0) {
									const float amubar = H * dot_rr2;  //amubar=CTE.h*dot/(rr2+CTE.eta2);
									const float robar = (rhopp1 + velrhop[p2].w)*0.5f;
									const float pi_visc = (-visco * cbar*amubar / robar)*massp2*ftmassp1;
									acep1.x -= pi_visc * frx; acep1.y -= pi_visc * fry; acep1.z -= pi_visc * frz;
								}
							}
							else {//-Laminar+SPS viscosity. 
								{//-Laminar contribution.
									const float robar2 = (rhopp1 + velrhop[p2].w);
									const float temp = 4.f*visco / ((rr2 + Eta2)*robar2);  //-Simplification of: temp=2.0f*visco/((rr2+CTE.eta2)*robar); robar=(rhopp1+velrhop2.w)*0.5f;
									const float vtemp = massp2 * temp*(drx*frx + dry * fry + drz * frz);
									acep1.x += vtemp * dvx; acep1.y += vtemp * dvy; acep1.z += vtemp * dvz;
								}
								//-SPS turbulence model.
								float tau_xx = taup1.xx, tau_xy = taup1.xy, tau_xz = taup1.xz; //-taup1 is always zero when p1 is not a fluid particle. | taup1 siempre es cero cuando p1 no es fluid.
								float tau_yy = taup1.yy, tau_yz = taup1.yz, tau_zz = taup1.zz;
								if (!boundp2 && !ftp2) {//-When p2 is a fluid particle. 
									tau_xx += tau[p2].xx; tau_xy += tau[p2].xy; tau_xz += tau[p2].xz;
									tau_yy += tau[p2].yy; tau_yz += tau[p2].yz; tau_zz += tau[p2].zz;
								}
								acep1.x += massp2 * ftmassp1*(tau_xx*frx + tau_xy * fry + tau_xz * frz);
								acep1.y += massp2 * ftmassp1*(tau_xy*frx + tau_yy * fry + tau_yz * frz);
								acep1.z += massp2 * ftmassp1*(tau_xz*frx + tau_yz * fry + tau_zz * frz);
								//-Velocity gradients.
								if (!ftp1) {//-When p1 is a fluid particle. 
									const float volp2 = -massp2 / velrhop[p2].w;
									float dv = dvx * volp2; gradvelp1.xx += dv * frx; gradvelp1.xy += dv * fry; gradvelp1.xz += dv * frz;
									dv = dvy * volp2; gradvelp1.xy += dv * frx; gradvelp1.yy += dv * fry; gradvelp1.yz += dv * frz;
									dv = dvz * volp2; gradvelp1.xz += dv * frx; gradvelp1.yz += dv * fry; gradvelp1.zz += dv * frz;
									//-To compute tau terms we assume that gradvel.xy=gradvel.dudy+gradvel.dvdx, gradvel.xz=gradvel.dudz+gradvel.dwdx, gradvel.yz=gradvel.dvdz+gradvel.dwdy
									//-so only 6 elements are needed instead of 3x3.
								}
							}
						}
					}
				}
			}
		}
		//-Sum results together. | Almacena resultados.
		if (shift || arp1 || acep1.x || acep1.y || acep1.z || visc) {
			if (tdelta == DELTA_Dynamic && deltap1 != FLT_MAX)arp1 += deltap1;
			if (tdelta == DELTA_DynamicExt)delta[p1] = (delta[p1] == FLT_MAX || deltap1 == FLT_MAX ? FLT_MAX : delta[p1] + deltap1);
			ar[p1] += arp1;
			ace[p1] = ace[p1] + acep1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;
			if (lamsps) {
				gradvel[p1].xx += gradvelp1.xx;
				gradvel[p1].xy += gradvelp1.xy;
				gradvel[p1].xz += gradvelp1.xz;
				gradvel[p1].yy += gradvelp1.yy;
				gradvel[p1].yz += gradvelp1.yz;
				gradvel[p1].zz += gradvelp1.zz;
			}

			if (shift && shiftpos[p1].x != FLT_MAX) {
				shiftpos[p1] = (shiftposp1.x == FLT_MAX ? TFloat3(FLT_MAX, 0, 0) : shiftpos[p1] + shiftposp1);
				if (shiftdetect)shiftdetect[p1] += shiftdetectp1;
			}
		}
	}

	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}

//==============================================================================
/// Perform interaction between particles: Solid - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::InteractionForcesSolid
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial, float visco
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tsymatrix3f* tau, tsymatrix3f* gradvel, tsymatrix3f* omega
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore
	, float &viscdt, float *ar, tfloat3 *ace, float *delta
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const bool boundp2 = (!cellinitial); //-Interaction with type boundary (Bound). | Interaccion con Bound.
										 //-Initialize viscth to calculate viscdt maximo con OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP.
	const int pfin = int(pinit + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif

	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0, deltap1 = 0;
		tfloat3 acep1 = TFloat3(0);
		// tfloat3 ddpp1 = TFloat3(0);
		// Matthias
		tsymatrix3f gradvelp1 = { 0, 0, 0, 0, 0, 0 };
		tsymatrix3f omegap1 = { 0, 0, 0, 0, 0, 0 };
		tfloat3 shiftposp1 = TFloat3(0);
		float shiftdetectp1 = 0;

		//-Obtain data of particle p1 in case of floating objects. | Obtiene datos de particula p1 en caso de existir floatings.
		bool ftp1 = false;     //-Indicate if it is floating. | Indica si es floating.
		float ftmassp1 = 1.f;  //-Contains floating particle mass or 1.0f if it is fluid. | Contiene masa de particula floating o 1.0f si es fluid.
		if (USE_FLOATING) {
			ftp1 = CODE_IsFloating(code[p1]);
			if (ftp1)ftmassp1 = FtObjs[CODE_GetTypeValue(code[p1])].massp;
			if (ftp1 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
			if (ftp1 && shift)shiftposp1.x = FLT_MAX;  //-For floating objects do not calculate shifting. | Para floatings no se calcula shifting.
		}

		//-Obtain data of particle p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const float rhopp1 = velrhop[p1].w;
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
		const float pressp1 = press[p1];
		// Matthias
		const tsymatrix3f taup1 = tau[p1];
		const float porep1 = pore[p1];

		//-Obtain interaction limits.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

		//-Search for neighbours in adjacent cells.
		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				// NSPH computation of Lp1 - Matthias


				//-Interaction of Fluid with type Fluid or Bound. | Interaccion de Fluid con varias Fluid o Bound.
				//------------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz; // Here will be put Fac (for diffusion)
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = (boundp2 ? MassBound : MassFluid); //-Contiene masa de particula segun sea bound o fluid.
						bool ftp2 = false;    //-Indicate if it is floating | Indica si es floating.
						bool compute = true;  //-Deactivate when using DEM and if it is of type float-float or float-bound | Se desactiva cuando se usa DEM y es float-float o float-bound.
						if (USE_FLOATING) {
							ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
#ifdef DELTA_HEAVYFLOATING
							if (ftp2 && massp2 <= (MassFluid*1.2f) && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#else
							if (ftp2 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#endif
							if (ftp2 && shift && tshifting == SHIFT_NoBound)shiftposp1.x = FLT_MAX; //-With floating objects do not use shifting. | Con floatings anula shifting.
							compute = !(USE_DEM && ftp1 && (boundp2 || ftp2)); //-Deactivate when using DEM and if it is of type float-float or float-bound. | Se desactiva cuando se usa DEM y es float-float o float-bound.
						}

						//===== Acceleration ===== 
						/*if (compute) {
							const float prs = (pressp1 + press[p2]) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0);
							const float p_vpm = -prs * massp2*ftmassp1;
							acep1.x += p_vpm * frx; acep1.y += p_vpm * fry; acep1.z += p_vpm * frz;
						}*/
						if (compute) {
							// Will include pore pressure
							const tsymatrix3f prs = {
								(pressp1 + porep1 - taup1.xx + press[p2] + pore[p2] - tau[p2].xx) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.xy + tau[p2].xy) / (rhopp1*velrhop[p2].w),
								-(taup1.xz + tau[p2].xz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.yy + press[p2] + pore[p2] - tau[p2].yy) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.yz + tau[p2].yz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.zz + press[p2] + pore[p2] - tau[p2].zz) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0)
							};
							const tsymatrix3f p_vpm3 = {
								-prs.xx*massp2*ftmassp1, -prs.xy*massp2*ftmassp1, -prs.xz*massp2*ftmassp1,
								-prs.yy*massp2*ftmassp1, -prs.yz*massp2*ftmassp1, -prs.zz*massp2*ftmassp1
							};

							acep1.x += p_vpm3.xx*frx + p_vpm3.xy*fry + p_vpm3.xz*frz;
							acep1.y += p_vpm3.xy*frx + p_vpm3.yy*fry + p_vpm3.yz*frz;
							acep1.z += p_vpm3.xz*frx + p_vpm3.yz*fry + p_vpm3.zz*frz;
						}

						//-Density derivative.
						const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
						if (compute)arp1 += massp2 * (dvx*frx + dvy * fry + dvz * frz);

						const float cbar = (float)Cs0;
						//-Density derivative (DeltaSPH Molteni).
						if ((tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt) && deltap1 != FLT_MAX) {
							const float rhop1over2 = rhopp1 / velrhop[p2].w;
							const float visc_densi = Delta2H * cbar*(rhop1over2 - 1.f) / (rr2 + Eta2);
							const float dot3 = (drx*frx + dry * fry + drz * frz);
							const float delta = visc_densi * dot3*massp2;
							deltap1 = (boundp2 ? FLT_MAX : deltap1 + delta);
						}

						//-Shifting correction.
						if (shift && shiftposp1.x != FLT_MAX) {
							const float massrhop = massp2 / velrhop[p2].w;
							const bool noshift = (boundp2 && (tshifting == SHIFT_NoBound || (tshifting == SHIFT_NoFixed && CODE_IsFixed(code[p2]))));
							shiftposp1.x = (noshift ? FLT_MAX : shiftposp1.x + massrhop * frx); //-For boundary do not use shifting. | Con boundary anula shifting.
							shiftposp1.y += massrhop * fry;
							shiftposp1.z += massrhop * frz;
							shiftdetectp1 -= massrhop * (drx*frx + dry * fry + drz * frz);
						}

						//===== Viscosity ===== 
						if (compute) {
							const float dot = drx * dvx + dry * dvy + drz * dvz;
							const float dot_rr2 = dot / (rr2 + Eta2);
							visc = max(dot_rr2, visc);
							if (!lamsps) {//-Artificial viscosity.
								if (dot<0) {
									const float amubar = H * dot_rr2;  //amubar=CTE.h*dot/(rr2+CTE.eta2);
									const float robar = (rhopp1 + velrhop[p2].w)*0.5f;
									const float pi_visc = (-visco * cbar*amubar / robar)*massp2*ftmassp1;
									acep1.x -= pi_visc * frx; acep1.y -= pi_visc * fry; acep1.z -= pi_visc * frz;
								}
							}
							/*else {//-Laminar+SPS viscosity. 
								{//-Laminar contribution.
									const float robar2 = (rhopp1 + velrhop[p2].w);
									const float temp = 4.f*visco / ((rr2 + Eta2)*robar2);  //-Simplification of: temp=2.0f*visco/((rr2+CTE.eta2)*robar); robar=(rhopp1+velrhop2.w)*0.5f;
									const float vtemp = massp2 * temp*(drx*frx + dry * fry + drz * frz);
									acep1.x += vtemp * dvx; acep1.y += vtemp * dvy; acep1.z += vtemp * dvz;
								}
								//-SPS turbulence model.
								float tau_xx = taup1.xx, tau_xy = taup1.xy, tau_xz = taup1.xz; //-taup1 is always zero when p1 is not a fluid particle. | taup1 siempre es cero cuando p1 no es fluid.
								float tau_yy = taup1.yy, tau_yz = taup1.yz, tau_zz = taup1.zz;
								if (!boundp2 && !ftp2) {//-When p2 is a fluid particle. 
									tau_xx += tau[p2].xx; tau_xy += tau[p2].xy; tau_xz += tau[p2].xz;
									tau_yy += tau[p2].yy; tau_yz += tau[p2].yz; tau_zz += tau[p2].zz;
								}
								acep1.x += massp2 * ftmassp1*(tau_xx*frx + tau_xy * fry + tau_xz * frz);
								acep1.y += massp2 * ftmassp1*(tau_xy*frx + tau_yy * fry + tau_yz * frz);
								acep1.z += massp2 * ftmassp1*(tau_xz*frx + tau_yz * fry + tau_zz * frz);
								//-Velocity gradients.
								if (!ftp1) {//-When p1 is a fluid particle. 
									const float volp2 = -massp2 / velrhop[p2].w;
									float dv = dvx * volp2; gradvelp1.xx += dv * frx; gradvelp1.xy += dv * fry; gradvelp1.xz += dv * frz;
									dv = dvy * volp2; gradvelp1.xy += dv * frx; gradvelp1.yy += dv * fry; gradvelp1.yz += dv * frz;
									dv = dvz * volp2; gradvelp1.xz += dv * frx; gradvelp1.yz += dv * fry; gradvelp1.zz += dv * frz;
									//-To compute tau terms we assume that gradvel.xy=gradvel.dudy+gradvel.dvdx, gradvel.xz=gradvel.dudz+gradvel.dwdx, gradvel.yz=gradvel.dvdz+gradvel.dwdy
									//-so only 6 elements are needed instead of 3x3.
								}
							}*/
						}
						
						//===== Velocity gradients ===== 
						if (compute) {
							if (!ftp1) {//-When p1 is a fluid particle / Cuando p1 es fluido. 
								const float volp2 = -massp2 / velrhop[p2].w;
								float dv = dvx * volp2;
								gradvelp1.xx += dv * frx; gradvelp1.xy += 0.5f*dv*fry; gradvelp1.xz += 0.5f*dv*frz;
								omegap1.xy += 0.5f*dv*fry; omegap1.xz += 0.5f*dv*frz;

								dv = dvy * volp2;
								gradvelp1.xy += 0.5f*dv*frx; gradvelp1.yy += dv * fry; gradvelp1.yz += 0.5f*dv*frz;
								omegap1.xy -= 0.5f*dv*frx; omegap1.yz += 0.5f*dv*frz;

								dv = dvz * volp2;
								gradvelp1.xz += 0.5f*dv*frx; gradvelp1.yz += 0.5f*dv*fry; gradvelp1.zz += dv * frz;
								omegap1.xz -= 0.5f*dv*frx; omegap1.yz -= 0.5f*dv*fry;
							}
						}
					}
				}
			}
		}
		//-Sum results together. | Almacena resultados.
		if (shift || arp1 || acep1.x || acep1.y || acep1.z || visc) {
			if (tdelta == DELTA_Dynamic && deltap1 != FLT_MAX)arp1 += deltap1;
			if (tdelta == DELTA_DynamicExt)delta[p1] = (delta[p1] == FLT_MAX || deltap1 == FLT_MAX ? FLT_MAX : delta[p1] + deltap1);
			ar[p1] += arp1;
			ace[p1] = ace[p1] + acep1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;			

			if (shift && shiftpos[p1].x != FLT_MAX) {
				shiftpos[p1] = (shiftposp1.x == FLT_MAX ? TFloat3(FLT_MAX, 0, 0) : shiftpos[p1] + shiftposp1);
				if (shiftdetect)shiftdetect[p1] += shiftdetectp1;
			}

			// Gradvel and rotation tensor
			gradvel[p1].xx += gradvelp1.xx;
			gradvel[p1].xy += gradvelp1.xy;
			gradvel[p1].xz += gradvelp1.xz;
			gradvel[p1].yy += gradvelp1.yy;
			gradvel[p1].yz += gradvelp1.yz;
			gradvel[p1].zz += gradvelp1.zz;

			omega[p1].xx += omegap1.xx;
			omega[p1].xy += omegap1.xy;
			omega[p1].xz += omegap1.xz;
			omega[p1].yy += omegap1.yy;
			omega[p1].yz += omegap1.yz;
			omega[p1].zz += omegap1.zz;
		}
	}

	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}



//==============================================================================
/// Interaction particles with Solid, pore and mass variation - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::InteractionForcesSolMass_M
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial, float visco
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tsymatrix3f* tau, tsymatrix3f* gradvel, tsymatrix3f* omega
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float *ar, tfloat3 *ace, float *delta
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const bool boundp2 = (!cellinitial); //-Interaction with type boundary (Bound). | Interaccion con Bound.
										 //-Initialize viscth to calculate viscdt maximo con OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP..
	const int pfin = int(pinit + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif

	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0, deltap1 = 0;
		tfloat3 acep1 = TFloat3(0);
		// tfloat3 ddpp1 = TFloat3(0);
		// Matthias
		tsymatrix3f gradvelp1 = { 0, 0, 0, 0, 0, 0 };
		tsymatrix3f omegap1 = { 0, 0, 0, 0, 0, 0 };
		tfloat3 shiftposp1 = TFloat3(0);
		float shiftdetectp1 = 0;

		//-Obtain data of particle p1 in case of floating objects. | Obtiene datos de particula p1 en caso de existir floatings.
		bool ftp1 = false;     //-Indicate if it is floating. | Indica si es floating.
		float ftmassp1 = 1.f;  //-Contains floating particle mass or 1.0f if it is fluid. | Contiene masa de particula floating o 1.0f si es fluid..
		if (USE_FLOATING) {
			ftp1 = CODE_IsFloating(code[p1]);
			if (ftp1)ftmassp1 = FtObjs[CODE_GetTypeValue(code[p1])].massp;
			if (ftp1 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
			if (ftp1 && shift)shiftposp1.x = FLT_MAX;  //-For floating objects do not calculate shifting. | Para floatings no se calcula shifting.
		}

		//-Obtain data of particle p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const float rhopp1 = velrhop[p1].w;
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
		const float pressp1 = press[p1];
		// Matthias
		const tsymatrix3f taup1 = tau[p1];
		const float porep1 = pore[p1];

		//-Obtain interaction limits.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

		//-Search for neighbours in adjacent cells.
		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				//-Interaction of Fluid with type Fluid or Bound. | Interaccion de Fluid con varias Fluid o Bound.
				//------------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz; // Here will be put Fac (for diffusion)
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = (boundp2 ? MassBound : mass[p2]); //-Contiene masa de particula segun sea bound o fluid.
						bool ftp2 = false;    //-Indicate if it is floating | Indica si es floating.
						bool compute = true;  //-Deactivate when using DEM and if it is of type float-float or float-bound | Se desactiva cuando se usa DEM y es float-float o float-bound.
						if (USE_FLOATING) {
						
							ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
#ifdef DELTA_HEAVYFLOATING
							if (ftp2 && massp2 <= (MassFluid*1.2f) && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#else
							if (ftp2 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#endif
							if (ftp2 && shift && tshifting == SHIFT_NoBound)shiftposp1.x = FLT_MAX; //-With floating objects do not use shifting. | Con floatings anula shifting.
							compute = !(USE_DEM && ftp1 && (boundp2 || ftp2)); //-Deactivate when using DEM and if it is of type float-float or float-bound. | Se desactiva cuando se usa DEM y es float-float o float-bound.
						}

						//===== Acceleration ===== 
						/*if (compute) {
						const float prs = (pressp1 + press[p2]) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0);
						const float p_vpm = -prs * massp2*ftmassp1;
						acep1.x += p_vpm * frx; acep1.y += p_vpm * fry; acep1.z += p_vpm * frz;
						}*/
						if (compute) {
							// Will include pore pressure
							const tsymatrix3f prs = {
								(pressp1 + porep1 - taup1.xx + press[p2] + pore[p2] - tau[p2].xx) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.xy + tau[p2].xy) / (rhopp1*velrhop[p2].w),
								-(taup1.xz + tau[p2].xz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.yy + press[p2] + pore[p2] - tau[p2].yy) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.yz + tau[p2].yz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.zz + press[p2] + pore[p2] - tau[p2].zz) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0)
							};
							const tsymatrix3f p_vpm3 = {
								-prs.xx*massp2*ftmassp1, -prs.xy*massp2*ftmassp1, -prs.xz*massp2*ftmassp1,
								-prs.yy*massp2*ftmassp1, -prs.yz*massp2*ftmassp1, -prs.zz*massp2*ftmassp1
							};

							acep1.x += p_vpm3.xx*frx + p_vpm3.xy*fry + p_vpm3.xz*frz;
							acep1.y += p_vpm3.xy*frx + p_vpm3.yy*fry + p_vpm3.yz*frz;
							acep1.z += p_vpm3.xz*frx + p_vpm3.yz*fry + p_vpm3.zz*frz;
						}

						//-Density derivative.
						const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
						if (compute)arp1 += massp2 * (dvx*frx + dvy * fry + dvz * frz);

						const float cbar = (float)Cs0;
						//-Density derivative (DeltaSPH Molteni).
						if ((tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt) && deltap1 != FLT_MAX) {
							const float rhop1over2 = rhopp1 / velrhop[p2].w;
							const float visc_densi = Delta2H * cbar*(rhop1over2 - 1.f) / (rr2 + Eta2);
							const float dot3 = (drx*frx + dry * fry + drz * frz);
							const float delta = visc_densi * dot3*massp2;
							deltap1 = (boundp2 ? FLT_MAX : deltap1 + delta);
						}

						//-Shifting correction.
						if (shift && shiftposp1.x != FLT_MAX) {
							const float massrhop = massp2 / velrhop[p2].w;
							const bool noshift = (boundp2 && (tshifting == SHIFT_NoBound || (tshifting == SHIFT_NoFixed && CODE_IsFixed(code[p2]))));
							shiftposp1.x = (noshift ? FLT_MAX : shiftposp1.x + massrhop * frx); //-For boundary do not use shifting. | Con boundary anula shifting.
							shiftposp1.y += massrhop * fry;
							shiftposp1.z += massrhop * frz;
							shiftdetectp1 -= massrhop * (drx*frx + dry * fry + drz * frz);
						}

						//===== Viscosity ======
						if (compute) {
							const float dot = drx * dvx + dry * dvy + drz * dvz;
							const float dot_rr2 = dot / (rr2 + Eta2);
							visc = max(dot_rr2, visc);
							if (!lamsps) {//-Artificial viscosity.
								if (dot<0) {
									const float amubar = H * dot_rr2;  //amubar=CTE.h*dot/(rr2+CTE.eta2);
									const float robar = (rhopp1 + velrhop[p2].w)*0.5f;
									const float pi_visc = (-visco * cbar*amubar / robar)*massp2*ftmassp1;
									acep1.x -= pi_visc * frx; acep1.y -= pi_visc * fry; acep1.z -= pi_visc * frz;
								}
							}
						}

						//===== Velocity gradients ===== 
						if (compute) {
							if (!ftp1) {//-When p1 is a fluid particle / Cuando p1 es fluido. 
								const float volp2 = -massp2 / velrhop[p2].w;
								float dv = dvx * volp2;
								gradvelp1.xx += dv * frx; gradvelp1.xy += 0.5f*dv*fry; gradvelp1.xz += 0.5f*dv*frz;
								omegap1.xy += 0.5f*dv*fry; omegap1.xz += 0.5f*dv*frz;

								dv = dvy * volp2;
								gradvelp1.xy += 0.5f*dv*frx; gradvelp1.yy += dv * fry; gradvelp1.yz += 0.5f*dv*frz;
								omegap1.xy -= 0.5f*dv*frx; omegap1.yz += 0.5f*dv*frz;

								dv = dvz * volp2;
								gradvelp1.xz += 0.5f*dv*frx; gradvelp1.yz += 0.5f*dv*fry; gradvelp1.zz += dv * frz;
								omegap1.xz -= 0.5f*dv*frx; omegap1.yz -= 0.5f*dv*fry;
							}
						}
					}
				}
			}
		}
		//-Sum results together.
		if (shift || arp1 || acep1.x || acep1.y || acep1.z || visc) {
			if (tdelta == DELTA_Dynamic && deltap1 != FLT_MAX)arp1 += deltap1;
			if (tdelta == DELTA_DynamicExt)delta[p1] = (delta[p1] == FLT_MAX || deltap1 == FLT_MAX ? FLT_MAX : delta[p1] + deltap1);
			ar[p1] += arp1;
			ace[p1] = ace[p1] + acep1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;

			if (shift && shiftpos[p1].x != FLT_MAX) {
				shiftpos[p1] = (shiftposp1.x == FLT_MAX ? TFloat3(FLT_MAX, 0, 0) : shiftpos[p1] + shiftposp1);
				if (shiftdetect)shiftdetect[p1] += shiftdetectp1;
			}
			// Gradvel and rotation tensor .
			gradvel[p1].xx += gradvelp1.xx;
			gradvel[p1].xy += gradvelp1.xy;
			gradvel[p1].xz += gradvelp1.xz;
			gradvel[p1].yy += gradvelp1.yy;
			gradvel[p1].yz += gradvelp1.yz;
			gradvel[p1].zz += gradvelp1.zz;

			omega[p1].xx += omegap1.xx;
			omega[p1].xy += omegap1.xy;
			omega[p1].xz += omegap1.xz;
			omega[p1].yy += omegap1.yy;
			omega[p1].yz += omegap1.yz;
			omega[p1].zz += omegap1.zz;

		}
	}

	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}


//==============================================================================
/// Interaction particles with SMQ with NSPH correction - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::InteractionForcesNSPH_M
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial, float visco
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tsymatrix3f* tau, tsymatrix3f* gradvel, tsymatrix3f* omega
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore, const float *mass
	, tmatrix3f *L
	, float &viscdt, float *ar, tfloat3 *ace, float *delta
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const bool boundp2 = (!cellinitial); //-Interaction with type boundary (Bound). | Interaccion con Bound.
										 //-Initialize viscth to calculate viscdt maximo con OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP..
	const int pfin = int(pinit + n);
	
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif

	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0, deltap1 = 0;
		tfloat3 acep1 = TFloat3(0);
		// tfloat3 ddpp1 = TFloat3(0);
		// Matthias
		tsymatrix3f gradvelp1 = { 0, 0, 0, 0, 0, 0 };
		tsymatrix3f omegap1 = { 0, 0, 0, 0, 0, 0 };
		tfloat3 shiftposp1 = TFloat3(0);
		float shiftdetectp1 = 0;

		//-Obtain data of particle p1 in case of floating objects. | Obtiene datos de particula p1 en caso de existir floatings.
		bool ftp1 = false;     //-Indicate if it is floating. | Indica si es floating.
		float ftmassp1 = 1.f;  //-Contains floating particle mass or 1.0f if it is fluid. | Contiene masa de particula floating o 1.0f si es fluid..
		if (USE_FLOATING) {
			ftp1 = CODE_IsFloating(code[p1]);
			if (ftp1)ftmassp1 = FtObjs[CODE_GetTypeValue(code[p1])].massp;
			if (ftp1 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
			if (ftp1 && shift)shiftposp1.x = FLT_MAX;  //-For floating objects do not calculate shifting. | Para floatings no se calcula shifting.
		}

		//-Obtain data of particle p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const float rhopp1 = velrhop[p1].w;
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
		const float pressp1 = press[p1];
		// Matthias
		const tsymatrix3f taup1 = tau[p1];
		const float porep1 = pore[p1];
		tmatrix3f Mp1 = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

		//-Obtain interaction limits.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

		//-Search for neighbours in adjacent cells.
		for (int z = zini; z < zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y < yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				// Mp1 reinitialisation
				//Mp1 = { 0, 0, 0, 0, 0, 0, 0, 0, 0 };

				// Computation of Lp1
				for (unsigned p2 = pini; p2 < pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					float massp2 = mass[p2]; //-Contiene masa de particula segun sea bound o fluid.

					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						float frx, fry, frz;
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						if (true) {
							if (!ftp1) {//-When p1 is a fluid particle / Cuando p1 es fluido. 
								const float volp2 = -massp2 / velrhop[p2].w;
								Mp1.a11 += volp2 * drx * frx;
								Mp1.a12 += volp2 * drx * fry;
								Mp1.a13 += volp2 * drx * frz;
								Mp1.a21 += volp2 * dry * frx;
								Mp1.a22 += volp2 * dry * fry;
								Mp1.a23 += volp2 * dry * frz;
								Mp1.a31 += volp2 * drz * frx;
								Mp1.a32 += volp2 * drz * fry;
								Mp1.a33 += volp2 * drz * frz;
							}
						}
					}
				}
			}
		}
		/*if (idp[p1] == 0)   printf("M(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Mp1.a11, Mp1.a12, Mp1.a13, Mp1.a21, Mp1.a22, Mp1.a23, Mp1.a31, Mp1.a32, Mp1.a33);

		if (idp[p1] == 600) printf("M(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Mp1.a11, Mp1.a12, Mp1.a13, Mp1.a21, Mp1.a22, Mp1.a23, Mp1.a31, Mp1.a32, Mp1.a33);

		if (idp[p1] == 786) printf("M(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Mp1.a11, Mp1.a12, Mp1.a13, Mp1.a21, Mp1.a22, Mp1.a23, Mp1.a31, Mp1.a32, Mp1.a33);*/

		//tmatrix3f Imp1 = Inv3f(Mp1);
		L[p1] = Inv3f(Mp1);
		/*if (idp[p1] == 0)   printf("M-1(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Imp1.a11, Imp1.a12, Imp1.a13, Imp1.a21, Imp1.a22, Imp1.a23, Imp1.a31, Imp1.a32, Imp1.a33);

		if (idp[p1] == 600) printf("M-1(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Imp1.a11, Imp1.a12, Imp1.a13, Imp1.a21, Imp1.a22, Imp1.a23, Imp1.a31, Imp1.a32, Imp1.a33);

		if (idp[p1] == 786) printf("M-1(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Imp1.a11, Imp1.a12, Imp1.a13, Imp1.a21, Imp1.a22, Imp1.a23, Imp1.a31, Imp1.a32, Imp1.a33);
*/


		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				//-Interaction of Fluid with type Fluid or Bound. | Interaccion de Fluid con varias Fluid o Bound.
				//------------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz; // Here will be put Fac (for diffusion)
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = (boundp2 ? MassBound : mass[p2]); //-Contiene masa de particula segun sea bound o fluid.
						bool ftp2 = false;    //-Indicate if it is floating | Indica si es floating.
						bool compute = true;  //-Deactivate when using DEM and if it is of type float-float or float-bound | Se desactiva cuando se usa DEM y es float-float o float-bound.
						if (USE_FLOATING) {

							ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
#ifdef DELTA_HEAVYFLOATING
							if (ftp2 && massp2 <= (MassFluid*1.2f) && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#else
							if (ftp2 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#endif
							if (ftp2 && shift && tshifting == SHIFT_NoBound)shiftposp1.x = FLT_MAX; //-With floating objects do not use shifting. | Con floatings anula shifting.
							compute = !(USE_DEM && ftp1 && (boundp2 || ftp2)); //-Deactivate when using DEM and if it is of type float-float or float-bound. | Se desactiva cuando se usa DEM y es float-float o float-bound.
						}

						//===== Acceleration ===== 
						/*if (compute) {
						const float prs = (pressp1 + press[p2]) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0);
						const float p_vpm = -prs * massp2*ftmassp1;
						acep1.x += p_vpm * frx; acep1.y += p_vpm * fry; acep1.z += p_vpm * frz;
						}*/
						if (compute) {
							// Will include pore pressure
							const tsymatrix3f prs = {
								(pressp1 + porep1 - taup1.xx + press[p2] + pore[p2] - tau[p2].xx) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.xy + tau[p2].xy) / (rhopp1*velrhop[p2].w),
								-(taup1.xz + tau[p2].xz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.yy + press[p2] + pore[p2] - tau[p2].yy) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0),
								-(taup1.yz + tau[p2].yz) / (rhopp1*velrhop[p2].w),
								(pressp1 + porep1 - taup1.zz + press[p2] + pore[p2] - tau[p2].zz) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0)
							};
							const tsymatrix3f p_vpm3 = {
								-prs.xx*massp2*ftmassp1, -prs.xy*massp2*ftmassp1, -prs.xz*massp2*ftmassp1,
								-prs.yy*massp2*ftmassp1, -prs.yz*massp2*ftmassp1, -prs.zz*massp2*ftmassp1
							};

							/*acep1.x += p_vpm3.xx*frx + p_vpm3.xy*fry + p_vpm3.xz*frz;
							acep1.y += p_vpm3.xy*frx + p_vpm3.yy*fry + p_vpm3.yz*frz;
							acep1.z += p_vpm3.xz*frx + p_vpm3.yz*fry + p_vpm3.zz*frz;*/

							// Velocity NSPH
							acep1.x += p_vpm3.xx*frx*L[p1].a11 + p_vpm3.xy*fry*L[p1].a12 + p_vpm3.xz*frz*L[p1].a13;
							acep1.y += p_vpm3.xy*frx*L[p1].a21 + p_vpm3.yy*fry*L[p1].a22 + p_vpm3.yz*frz*L[p1].a23;
							acep1.z += p_vpm3.xz*frx*L[p1].a31 + p_vpm3.yz*fry*L[p1].a32 + p_vpm3.zz*frz*L[p1].a33;
						}

						//-Density derivative.
						const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
						if (compute)arp1 += massp2 * (dvx*frx*L[p1].a11 + dvy * fry*L[p1].a22 + dvz * frz*L[p1].a33);

						const float cbar = (float)Cs0;
						//-Density derivative (DeltaSPH Molteni).
						if ((tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt) && deltap1 != FLT_MAX) {
							const float rhop1over2 = rhopp1 / velrhop[p2].w;
							const float visc_densi = Delta2H * cbar*(rhop1over2 - 1.f) / (rr2 + Eta2);
							const float dot3 = (drx*frx + dry * fry + drz * frz);
							const float delta = visc_densi * dot3*massp2;
							deltap1 = (boundp2 ? FLT_MAX : deltap1 + delta);
						}

						//-Shifting correction.
						if (shift && shiftposp1.x != FLT_MAX) {
							const float massrhop = massp2 / velrhop[p2].w;
							const bool noshift = (boundp2 && (tshifting == SHIFT_NoBound || (tshifting == SHIFT_NoFixed && CODE_IsFixed(code[p2]))));
							shiftposp1.x = (noshift ? FLT_MAX : shiftposp1.x + massrhop * frx); //-For boundary do not use shifting. | Con boundary anula shifting.
							shiftposp1.y += massrhop * fry;
							shiftposp1.z += massrhop * frz;
							shiftdetectp1 -= massrhop * (drx*frx + dry * fry + drz * frz);
						}

						//===== Viscosity ======
						if (compute) {
							const float dot = drx * dvx + dry * dvy + drz * dvz;
							const float dot_rr2 = dot / (rr2 + Eta2);
							visc = max(dot_rr2, visc);
							if (!lamsps) {//-Artificial viscosity.
								if (dot<0) {
									const float amubar = H * dot_rr2;  //amubar=CTE.h*dot/(rr2+CTE.eta2);
									const float robar = (rhopp1 + velrhop[p2].w)*0.5f;
									const float pi_visc = (-visco * cbar*amubar / robar)*massp2*ftmassp1;
									acep1.x -= pi_visc * frx; acep1.y -= pi_visc * fry; acep1.z -= pi_visc * frz;
								}
							}
						}

						//===== Velocity gradients ===== 
						if (compute) {
							if (!ftp1) {//-When p1 is a fluid particle / Cuando p1 es fluido. 
								const float volp2 = -massp2 / velrhop[p2].w;
								/*float dv = dvx * volp2;
								gradvelp1.xx += dv * frx; gradvelp1.xy += 0.5f*dv*fry; gradvelp1.xz += 0.5f*dv*frz;
								omegap1.xy += 0.5f*dv*fry; omegap1.xz += 0.5f*dv*frz;

								dv = dvy * volp2;
								gradvelp1.xy += 0.5f*dv*frx; gradvelp1.yy += dv * fry; gradvelp1.yz += 0.5f*dv*frz;
								omegap1.xy -= 0.5f*dv*frx; omegap1.yz += 0.5f*dv*frz;

								dv = dvz * volp2;
								gradvelp1.xz += 0.5f*dv*frx; gradvelp1.yz += 0.5f*dv*fry; gradvelp1.zz += dv * frz;
								omegap1.xz -= 0.5f*dv*frx; omegap1.yz -= 0.5f*dv*fry;*/

								// Velocity gradient NSPH
								float dv = dvx * volp2;
								gradvelp1.xx += dv * frx*L[p1].a11; gradvelp1.xy += 0.5f*dv*fry*L[p1].a12; gradvelp1.xz += 0.5f*dv*frz*L[p1].a13;
								omegap1.xy += 0.5f*dv*fry*L[p1].a12; omegap1.xz += 0.5f*dv*frz*L[p1].a13;

								dv = dvy * volp2;
								gradvelp1.xy += 0.5f*dv*frx*L[p1].a21; gradvelp1.yy += dv * fry*L[p1].a22; gradvelp1.yz += 0.5f*dv*frz*L[p1].a23;
								omegap1.xy -= 0.5f*dv*frx*L[p1].a21; omegap1.yz += 0.5f*dv*frz*L[p1].a23;

								dv = dvz * volp2;
								gradvelp1.xz += 0.5f*dv*frx*L[p1].a31; gradvelp1.yz += 0.5f*dv*fry*L[p1].a32; gradvelp1.zz += dv * frz*L[p1].a33;
								omegap1.xz -= 0.5f*dv*frx*L[p1].a31; omegap1.yz -= 0.5f*dv*fry*L[p1].a32;
							}
						}
					}
				}
			}
		}
		
		//-Sum results together.
		if (shift || arp1 || acep1.x || acep1.y || acep1.z || visc) {
			if (tdelta == DELTA_Dynamic && deltap1 != FLT_MAX)arp1 += deltap1;
			if (tdelta == DELTA_DynamicExt)delta[p1] = (delta[p1] == FLT_MAX || deltap1 == FLT_MAX ? FLT_MAX : delta[p1] + deltap1);
			ar[p1] += arp1;
			ace[p1] = ace[p1] + acep1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;

			if (shift && shiftpos[p1].x != FLT_MAX) {
				shiftpos[p1] = (shiftposp1.x == FLT_MAX ? TFloat3(FLT_MAX, 0, 0) : shiftpos[p1] + shiftposp1);
				if (shiftdetect)shiftdetect[p1] += shiftdetectp1;
			}

			// Gradvel and rotation tensor .
			gradvel[p1].xx += gradvelp1.xx;
			gradvel[p1].xy += gradvelp1.xy;
			gradvel[p1].xz += gradvelp1.xz;
			gradvel[p1].yy += gradvelp1.yy;
			gradvel[p1].yz += gradvelp1.yz;
			gradvel[p1].zz += gradvelp1.zz;

			omega[p1].xx += omegap1.xx;
			omega[p1].xy += omegap1.xy;
			omega[p1].xz += omegap1.xz;
			omega[p1].yy += omegap1.yy;
			omega[p1].yz += omegap1.yz;
			omega[p1].zz += omegap1.zz;

		}

		//if (idp[p1] == 786) printf("M(%d) = (%.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f, %.4f)\n", idp[p1], Mp1.a11, Mp1.a12, Mp1.a13, Mp1.a21, Mp1.a22, Mp1.a23, Mp1.a31, Mp1.a32, Mp1.a33);

		//printf("Mp1 = (%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)\n"
		//	, L[p1].a11, L[p1].a12, L[p1].a13, L[p1].a21, L[p1].a22, L[p1].a23, L[p1].a31, L[p1].a32, L[p1].a33);
	}

	/*printf("Mp1 = (%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)\n"
		, L[int(pinit)].a11, L[int(pinit)].a12, L[int(pinit)].a13, L[int(pinit)].a21, L[int(pinit)].a22, L[int(pinit)].a23, L[int(pinit)].a31, L[int(pinit)].a32, L[int(pinit)].a33);
	printf("MpN = (%.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f, %.3f)\n"
		, L[int(pfin - 1)].a11, L[int(pfin-1)].a12, L[int(pfin - 1)].a13, L[int(pfin - 1)].a21, L[int(pfin - 1)].a22, L[int(pfin - 1)].a23, L[int(pfin - 1)].a31, L[int(pfin - 1)].a32, L[int(pfin - 1)].a33);
*/
	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}


//==============================================================================
/// Interaction particles with SPM and Press3D - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::InteractionForcesSolMass_M
(unsigned n, unsigned pinit, tint4 nc, int hdiv, unsigned cellinitial, float visco
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const tsymatrix3f* tau, tsymatrix3f* gradvel, tsymatrix3f* omega
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const tfloat3 *press, const float *pore, const float *mass
	, float &viscdt, float *ar, tfloat3 *ace, float *delta
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const bool boundp2 = (!cellinitial); //-Interaction with type boundary (Bound). | Interaccion con Bound.
										 //-Initialize viscth to calculate viscdt maximo con OpenMP. | Inicializa viscth para calcular visdt maximo con OpenMP.
	float viscth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)viscth[th*OMP_STRIDE] = 0;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP..
	const int pfin = int(pinit + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif

	for (int p1 = int(pinit); p1<pfin; p1++) {
		float visc = 0, arp1 = 0, deltap1 = 0;
		tfloat3 acep1 = TFloat3(0);
		// tfloat3 ddpp1 = TFloat3(0);
		// Matthias
		tsymatrix3f gradvelp1 = { 0, 0, 0, 0, 0, 0 };
		tsymatrix3f omegap1 = { 0, 0, 0, 0, 0, 0 };
		tfloat3 shiftposp1 = TFloat3(0);
		float shiftdetectp1 = 0;

		//-Obtain data of particle p1 in case of floating objects. | Obtiene datos de particula p1 en caso de existir floatings.
		bool ftp1 = false;     //-Indicate if it is floating. | Indica si es floating.
		float ftmassp1 = 1.f;  //-Contains floating particle mass or 1.0f if it is fluid. | Contiene masa de particula floating o 1.0f si es fluid..
		if (USE_FLOATING) {
			ftp1 = CODE_IsFloating(code[p1]);
			if (ftp1)ftmassp1 = FtObjs[CODE_GetTypeValue(code[p1])].massp;
			if (ftp1 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
			if (ftp1 && shift)shiftposp1.x = FLT_MAX;  //-For floating objects do not calculate shifting. | Para floatings no se calcula shifting.
		}

		//-Obtain data of particle p1.
		const tfloat3 velp1 = TFloat3(velrhop[p1].x, velrhop[p1].y, velrhop[p1].z);
		const float rhopp1 = velrhop[p1].w;
		const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
		const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
		const tfloat3 pressp1 = press[p1];
		// Matthias
		const tsymatrix3f taup1 = tau[p1];
		const float porep1 = pore[p1];

		//-Obtain interaction limits.
		int cxini, cxfin, yini, yfin, zini, zfin;
		GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);


		//-Search for neighbours in adjacent cells.
		for (int z = zini; z<zfin; z++) {
			const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
			for (int y = yini; y<yfin; y++) {
				int ymod = zmod + nc.x*y;
				const unsigned pini = beginendcell[cxini + ymod];
				const unsigned pfin = beginendcell[cxfin + ymod];

				//-Interaction of Fluid with type Fluid or Bound. | Interaccion de Fluid con varias Fluid o Bound.
				//------------------------------------------------------------------------------------------------
				for (unsigned p2 = pini; p2<pfin; p2++) {
					const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
					const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
					const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
					const float rr2 = drx * drx + dry * dry + drz * drz;
					if (rr2 <= Fourh2 && rr2 >= ALMOSTZERO) {
						//-Cubic Spline, Wendland or Gaussian kernel.
						float frx, fry, frz; // Here will be put Fac (for diffusion)
						if (tker == KERNEL_Wendland)GetKernelWendland(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Gaussian)GetKernelGaussian(rr2, drx, dry, drz, frx, fry, frz);
						else if (tker == KERNEL_Cubic)GetKernelCubic(rr2, drx, dry, drz, frx, fry, frz);

						//===== Get mass of particle p2 ===== 
						float massp2 = (boundp2 ? MassBound : mass[p2]); //-Contiene masa de particula segun sea bound o fluid.
						bool ftp2 = false;    //-Indicate if it is floating | Indica si es floating.
						bool compute = true;  //-Deactivate when using DEM and if it is of type float-float or float-bound | Se desactiva cuando se usa DEM y es float-float o float-bound.
						if (USE_FLOATING) {

							ftp2 = CODE_IsFloating(code[p2]);
							if (ftp2)massp2 = FtObjs[CODE_GetTypeValue(code[p2])].massp;
#ifdef DELTA_HEAVYFLOATING
							if (ftp2 && massp2 <= (MassFluid*1.2f) && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#else
							if (ftp2 && (tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt))deltap1 = FLT_MAX;
#endif
							if (ftp2 && shift && tshifting == SHIFT_NoBound)shiftposp1.x = FLT_MAX; //-With floating objects do not use shifting. | Con floatings anula shifting.
							compute = !(USE_DEM && ftp1 && (boundp2 || ftp2)); //-Deactivate when using DEM and if it is of type float-float or float-bound. | Se desactiva cuando se usa DEM y es float-float o float-bound.
						}

						//===== Acceleration ===== 
						/*if (compute) {
						const float prs = (pressp1 + press[p2]) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1, velrhop[p2].w, press[p2]) : 0);
						const float p_vpm = -prs * massp2*ftmassp1;
						acep1.x += p_vpm * frx; acep1.y += p_vpm * fry; acep1.z += p_vpm * frz;
						}*/
						if (compute) {
							// Will include pore pressure
							const tsymatrix3f prs = {
								(pressp1.x + porep1 - taup1.xx + press[p2].x + pore[p2] - tau[p2].xx) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1.x, velrhop[p2].w, press[p2].x) : 0),
								-(taup1.xy + tau[p2].xy) / (rhopp1*velrhop[p2].w),
								-(taup1.xz + tau[p2].xz) / (rhopp1*velrhop[p2].w),
								(pressp1.y + porep1 - taup1.yy + press[p2].y + pore[p2] - tau[p2].yy) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1.y, velrhop[p2].w, press[p2].y) : 0),
								-(taup1.yz + tau[p2].yz) / (rhopp1*velrhop[p2].w),
								(pressp1.z + porep1 - taup1.zz + press[p2].z + pore[p2] - tau[p2].zz) / (rhopp1*velrhop[p2].w) + (tker == KERNEL_Cubic ? GetKernelCubicTensil(rr2, rhopp1, pressp1.z, velrhop[p2].w, press[p2].z) : 0)
							};

							const tsymatrix3f p_vpm3 = {
								-prs.xx*massp2*ftmassp1, -prs.xy*massp2*ftmassp1, -prs.xz*massp2*ftmassp1,
								-prs.yy*massp2*ftmassp1, -prs.yz*massp2*ftmassp1, -prs.zz*massp2*ftmassp1
							};

							acep1.x += p_vpm3.xx*frx + p_vpm3.xy*fry + p_vpm3.xz*frz;
							acep1.y += p_vpm3.xy*frx + p_vpm3.yy*fry + p_vpm3.yz*frz;
							acep1.z += p_vpm3.xz*frx + p_vpm3.yz*fry + p_vpm3.zz*frz;
						}

						//-Density derivative.
						const float dvx = velp1.x - velrhop[p2].x, dvy = velp1.y - velrhop[p2].y, dvz = velp1.z - velrhop[p2].z;
						if (compute)arp1 += massp2 * (dvx*frx + dvy * fry + dvz * frz);

						const float cbar = (float)Cs0;
						//-Density derivative (DeltaSPH Molteni).
						if ((tdelta == DELTA_Dynamic || tdelta == DELTA_DynamicExt) && deltap1 != FLT_MAX) {
							const float rhop1over2 = rhopp1 / velrhop[p2].w;
							const float visc_densi = Delta2H * cbar*(rhop1over2 - 1.f) / (rr2 + Eta2);
							const float dot3 = (drx*frx + dry * fry + drz * frz);
							const float delta = visc_densi * dot3*massp2;
							deltap1 = (boundp2 ? FLT_MAX : deltap1 + delta);
						}

						//-Shifting correction.
						if (shift && shiftposp1.x != FLT_MAX) {
							const float massrhop = massp2 / velrhop[p2].w;
							const bool noshift = (boundp2 && (tshifting == SHIFT_NoBound || (tshifting == SHIFT_NoFixed && CODE_IsFixed(code[p2]))));
							shiftposp1.x = (noshift ? FLT_MAX : shiftposp1.x + massrhop * frx); //-For boundary do not use shifting. | Con boundary anula shifting.
							shiftposp1.y += massrhop * fry;
							shiftposp1.z += massrhop * frz;
							shiftdetectp1 -= massrhop * (drx*frx + dry * fry + drz * frz);
						}

						//===== Viscosity ======
						if (compute) {
							const float dot = drx * dvx + dry * dvy + drz * dvz;
							const float dot_rr2 = dot / (rr2 + Eta2);
							visc = max(dot_rr2, visc);
							if (!lamsps) {//-Artificial viscosity.
								if (dot<0) {
									const float amubar = H * dot_rr2;  //amubar=CTE.h*dot/(rr2+CTE.eta2);
									const float robar = (rhopp1 + velrhop[p2].w)*0.5f;
									const float pi_visc = (-visco * cbar*amubar / robar)*massp2*ftmassp1;
									acep1.x -= pi_visc * frx; acep1.y -= pi_visc * fry; acep1.z -= pi_visc * frz;
								}
							}
						}

						//===== Velocity gradients ===== 
						if (compute) {
							if (!ftp1) {//-When p1 is a fluid particle / Cuando p1 es fluido. 
								const float volp2 = -massp2 / velrhop[p2].w;
								float dv = dvx * volp2;
								gradvelp1.xx += dv * frx; gradvelp1.xy += 0.5f*dv*fry; gradvelp1.xz += 0.5f*dv*frz;
								omegap1.xy += 0.5f*dv*fry; omegap1.xz += 0.5f*dv*frz;

								dv = dvy * volp2;
								gradvelp1.xy += 0.5f*dv*frx; gradvelp1.yy += dv * fry; gradvelp1.yz += 0.5f*dv*frz;
								omegap1.xy -= 0.5f*dv*frx; omegap1.yz += 0.5f*dv*frz;

								dv = dvz * volp2;
								gradvelp1.xz += 0.5f*dv*frx; gradvelp1.yz += 0.5f*dv*fry; gradvelp1.zz += dv * frz;
								omegap1.xz -= 0.5f*dv*frx; omegap1.yz -= 0.5f*dv*fry;
							}
						}
					}
				}
			}
		}
		//-Sum results together.
		//printf("1Ax = %.8f, Ay = %.8f, Az = %.8f\n", acep1.x, acep1.y, acep1.z);
		//printf("2Ax = %.8f, Ay = %.8f, Az = %.8f\n", ace[p1].x, ace[p1].y, ace[p1].z);
		if (shift || arp1 || acep1.x || acep1.y || acep1.z || visc) {
			if (tdelta == DELTA_Dynamic && deltap1 != FLT_MAX)arp1 += deltap1;
			if (tdelta == DELTA_DynamicExt)delta[p1] = (delta[p1] == FLT_MAX || deltap1 == FLT_MAX ? FLT_MAX : delta[p1] + deltap1);
			ar[p1] += arp1;
			ace[p1] = ace[p1] + acep1;
			const int th = omp_get_thread_num();
			if (visc>viscth[th*OMP_STRIDE])viscth[th*OMP_STRIDE] = visc;

			if (shift && shiftpos[p1].x != FLT_MAX) {
				shiftpos[p1] = (shiftposp1.x == FLT_MAX ? TFloat3(FLT_MAX, 0, 0) : shiftpos[p1] + shiftposp1);
				if (shiftdetect)shiftdetect[p1] += shiftdetectp1;
			}
			// Gradvel and rotation tensor .
			gradvel[p1].xx += gradvelp1.xx;
			gradvel[p1].xy += gradvelp1.xy;
			gradvel[p1].xz += gradvelp1.xz;
			gradvel[p1].yy += gradvelp1.yy;
			gradvel[p1].yz += gradvelp1.yz;
			gradvel[p1].zz += gradvelp1.zz;

			omega[p1].xx += omegap1.xx;
			omega[p1].xy += omegap1.xy;
			omega[p1].xz += omegap1.xz;
			omega[p1].yy += omegap1.yy;
			omega[p1].yz += omegap1.yz;
			omega[p1].zz += omegap1.zz;

		}
		//printf("3Ax = %.8f, Ay = %.8f, Az = %.8f\n\n", ace[p1].x, ace[p1].y, ace[p1].z);
	}

	//-Keep max value in viscdt. | Guarda en viscdt el valor maximo.
	for (int th = 0; th<OmpThreads; th++)if (viscdt<viscth[th*OMP_STRIDE])viscdt = viscth[th*OMP_STRIDE];
}



//==============================================================================
/// Perform DEM interaction between particles Floating-Bound & Floating-Floating //(DEM)
/// Realiza interaccion DEM entre particulas Floating-Bound & Floating-Floating //(DEM)
//==============================================================================
template<bool psingle> void JSphSolidCpu::InteractionForcesDEM
(unsigned nfloat, tint4 nc, int hdiv, unsigned cellfluid
	, const unsigned *beginendcell, tint3 cellzero, const unsigned *dcell
	, const unsigned *ftridp, const StDemData* demdata
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, float &viscdt, tfloat3 *ace)const
{
	//-Initialise demdtth to calculate max demdt with OpenMP. | Inicializa demdtth para calcular demdt maximo con OpenMP.
	float demdtth[OMP_MAXTHREADS*OMP_STRIDE];
	for (int th = 0; th<OmpThreads; th++)demdtth[th*OMP_STRIDE] = -FLT_MAX;
	//-Initialise execution with OpenMP. | Inicia ejecucion con OpenMP.
	const int nft = int(nfloat);
#ifdef OMP_USE
#pragma omp parallel for schedule (guided)
#endif
	for (int cf = 0; cf<nft; cf++) {
		const unsigned p1 = ftridp[cf];
		if (p1 != UINT_MAX) {
			float demdtp1 = 0;
			tfloat3 acep1 = TFloat3(0);

			//-Get data of particle p1.
			const tfloat3 psposp1 = (psingle ? pspos[p1] : TFloat3(0));
			const tdouble3 posp1 = (psingle ? TDouble3(0) : pos[p1]);
			const typecode tavp1 = CODE_GetTypeAndValue(code[p1]);
			const float masstotp1 = demdata[tavp1].mass;
			const float taup1 = demdata[tavp1].tau;
			const float kfricp1 = demdata[tavp1].kfric;
			const float restitup1 = demdata[tavp1].restitu;

			//-Get interaction limits.
			int cxini, cxfin, yini, yfin, zini, zfin;
			GetInteractionCells(dcell[p1], hdiv, nc, cellzero, cxini, cxfin, yini, yfin, zini, zfin);

			//-Search for neighbours in adjacent cells (first bound and then fluid+floating).
			for (unsigned cellinitial = 0; cellinitial <= cellfluid; cellinitial += cellfluid) {
				for (int z = zini; z<zfin; z++) {
					const int zmod = (nc.w)*z + cellinitial; //-Sum from start of fluid or boundary cells. | Le suma donde empiezan las celdas de fluido o bound.
					for (int y = yini; y<yfin; y++) {
						int ymod = zmod + nc.x*y;
						const unsigned pini = beginendcell[cxini + ymod];
						const unsigned pfin = beginendcell[cxfin + ymod];

						//-Interaction of Floating Object particles with type Fluid or Bound. | Interaccion de Floating con varias Fluid o Bound.
						//-----------------------------------------------------------------------------------------------------------------------
						for (unsigned p2 = pini; p2<pfin; p2++)if (CODE_IsNotFluid(code[p2]) && tavp1 != CODE_GetTypeAndValue(code[p2])) {
							const float drx = (psingle ? psposp1.x - pspos[p2].x : float(posp1.x - pos[p2].x));
							const float dry = (psingle ? psposp1.y - pspos[p2].y : float(posp1.y - pos[p2].y));
							const float drz = (psingle ? psposp1.z - pspos[p2].z : float(posp1.z - pos[p2].z));
							const float rr2 = drx * drx + dry * dry + drz * drz;
							const float rad = sqrt(rr2);

							//-Calculate max value of demdt. | Calcula valor maximo de demdt.
							const typecode tavp2 = CODE_GetTypeAndValue(code[p2]);
							const float masstotp2 = demdata[tavp2].mass;
							const float taup2 = demdata[tavp2].tau;
							const float kfricp2 = demdata[tavp2].kfric;
							const float restitup2 = demdata[tavp2].restitu;
							//const StDemData *demp2=demobjs+CODE_GetTypeAndValue(code[p2]);

							const float nu_mass = (!cellinitial ? masstotp1 / 2 : masstotp1 * masstotp2 / (masstotp1 + masstotp2)); //-Con boundary toma la propia masa del floating 1.
							const float kn = 4 / (3 * (taup1 + taup2))*sqrt(float(Dp) / 4); //-Generalized rigidity - Lemieux 2008.
							const float dvx = velrhop[p1].x - velrhop[p2].x, dvy = velrhop[p1].y - velrhop[p2].y, dvz = velrhop[p1].z - velrhop[p2].z; //vji
							const float nx = drx / rad, ny = dry / rad, nz = drz / rad; //normal_ji               
							const float vn = dvx * nx + dvy * ny + dvz * nz; //vji.nji
							const float demvisc = 0.2f / (3.21f*(pow(nu_mass / kn, 0.4f)*pow(fabs(vn), -0.2f)) / 40.f);
							if (demdtp1<demvisc)demdtp1 = demvisc;

							const float over_lap = 1.0f*float(Dp) - rad; //-(ri+rj)-|dij|
							if (over_lap>0.0f) { //-Contact.
												 //-Normal.
								const float eij = (restitup1 + restitup2) / 2;
								const float gn = -(2.0f*log(eij)*sqrt(nu_mass*kn)) / (sqrt(float(PI) + log(eij)*log(eij))); //-Generalized damping - Cummins 2010.
																															//const float gn=0.08f*sqrt(nu_mass*sqrt(float(Dp)/2)/((taup1+taup2)/2)); //-Generalized damping - Lemieux 2008.
								float rep = kn * pow(over_lap, 1.5f);
								float fn = rep - gn * pow(over_lap, 0.25f)*vn;
								acep1.x += (fn*nx); acep1.y += (fn*ny); acep1.z += (fn*nz); //-Force is applied in the normal between the particles.
																							//-Tangential.
								float dvxt = dvx - vn * nx, dvyt = dvy - vn * ny, dvzt = dvz - vn * nz; //Vji_t
								float vt = sqrt(dvxt*dvxt + dvyt * dvyt + dvzt * dvzt);
								float tx = 0, ty = 0, tz = 0; //-Tang vel unit vector.
								if (vt != 0) { tx = dvxt / vt; ty = dvyt / vt; tz = dvzt / vt; }
								float ft_elast = 2 * (kn*float(DemDtForce) - gn)*vt / 7; //-Elastic frictional string -->  ft_elast=2*(kn*fdispl-gn*vt)/7; fdispl=dtforce*vt;
								const float kfric_ij = (kfricp1 + kfricp2) / 2;
								float ft = kfric_ij * fn*tanh(8 * vt);  //-Coulomb.
								ft = (ft<ft_elast ? ft : ft_elast);   //-Not above yield criteria, visco-elastic model.
								acep1.x += (ft*tx); acep1.y += (ft*ty); acep1.z += (ft*tz);
							}
						}
					}
				}
			}
			//-Sum results together. | Almacena resultados.
			if (acep1.x || acep1.y || acep1.z) {
				ace[p1] = ace[p1] + acep1;
				const int th = omp_get_thread_num();
				if (demdtth[th*OMP_STRIDE]<demdtp1)demdtth[th*OMP_STRIDE] = demdtp1;
			}
		}
	}
	//-Update viscdt with max value of viscdt or demdt* | Actualiza viscdt con el valor maximo de viscdt y demdt*.
	float demdt = demdtth[0];
	for (int th = 1; th<OmpThreads; th++)if (demdt<demdtth[th*OMP_STRIDE])demdt = demdtth[th*OMP_STRIDE];
	if (viscdt<demdt)viscdt = demdt;
}


//==============================================================================
/// Computes sub-particle stress tensor (Tau) for SPS turbulence model.   
//==============================================================================
void JSphSolidCpu::ComputeSpsTau(unsigned n, unsigned pini, const tfloat4 *velrhop, const tsymatrix3f *gradvel, tsymatrix3f *tau)const {
	const int pfin = int(pini + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (static)
#endif
	for (int p = int(pini); p<pfin; p++) {
		const tsymatrix3f gradvel = SpsGradvelc[p];
		const float pow1 = gradvel.xx*gradvel.xx + gradvel.yy*gradvel.yy + gradvel.zz*gradvel.zz;
		const float prr = pow1 + pow1 + gradvel.xy*gradvel.xy + gradvel.xz*gradvel.xz + gradvel.yz*gradvel.yz;
		const float visc_sps = SpsSmag * sqrt(prr);
		const float div_u = gradvel.xx + gradvel.yy + gradvel.zz;
		const float sps_k = (2.0f / 3.0f)*visc_sps*div_u;
		const float sps_blin = SpsBlin * prr;
		const float sumsps = -(sps_k + sps_blin);
		const float twovisc_sps = (visc_sps + visc_sps);
		const float one_rho2 = 1.0f / velrhop[p].w;
		tau[p].xx = one_rho2 * (twovisc_sps*gradvel.xx + sumsps);
		tau[p].xy = one_rho2 * (visc_sps   *gradvel.xy);
		tau[p].xz = one_rho2 * (visc_sps   *gradvel.xz);
		tau[p].yy = one_rho2 * (twovisc_sps*gradvel.yy + sumsps);
		tau[p].yz = one_rho2 * (visc_sps   *gradvel.yz);
		tau[p].zz = one_rho2 * (twovisc_sps*gradvel.zz + sumsps);
	}
}

//==============================================================================
/// Computes stress tensor rate for solid - Matthias 
//==============================================================================
void JSphSolidCpu::ComputeJauTauDot_M(unsigned n, unsigned pini, const tsymatrix3f *gradvel, tsymatrix3f *tau, tsymatrix3f *taudot, tsymatrix3f *omega)const {
	const int pfin = int(pini + n);
#ifdef OMP_USE
#pragma omp parallel for schedule (static)
#endif
	for (int p = int(pini); p<pfin; p++) {
		const tsymatrix3f tau = Tauc_M[p];
		const tsymatrix3f gradvel = StrainDotc_M[p];
		const tsymatrix3f omega = Spinc_M[p];
		/*const tsymatrix3f E = {
			(C1 - K_M.x / 3.0f) * gradvel.xx + (C12 - K_M.y / 3.0f) * gradvel.yy + (C13 - K_M.z / 3.0f) * gradvel.zz,
			C4 * gradvel.xy,
			C5 * gradvel.xz,
			(C12 - K_M.x / 3.0f) * gradvel.xx + (C2 - K_M.y / 3.0f) * gradvel.yy + (C23 - K_M.z / 3.0f) * gradvel.zz,
			C6 * gradvel.yz,
			(C13 - K_M.x / 3.0f) * gradvel.xx + (C23 - K_M.y / 3.0f) * gradvel.yy + (C3 - K_M.z / 3.0f) * gradvel.zz };*/
		/*const tsymatrix3f E = {
			(C1 - K_M.x) * gradvel.xx + (C12 - K_M.y) * gradvel.yy + (C13 - K_M.z) * gradvel.zz,
			C4 * gradvel.xy,
			C5 * gradvel.xz,
			(C12 - K_M.x) * gradvel.xx + (C2 - K_M.y) * gradvel.yy + (C23 - K_M.z) * gradvel.zz,
			C6 * gradvel.yz,
			(C13 - K_M.x) * gradvel.xx + (C23 - K_M.y) * gradvel.yy + (C3 - K_M.z) * gradvel.zz };*/
		const tsymatrix3f E = {
			(2.0f/3.0f*C1 - 1.0f/3.0f*C12 -1.0f/3.0f*C13)*gradvel.xx + (2.0f/3.0f*C12 - 1.0f/3.0f*C2 - 1.0f/3.0f*C23)*gradvel.yy + (2.0f/3.0f*C13 - 1.0f/3.0f*C23 - 1.0f/3.0f*C3) * gradvel.zz,
			C4 * gradvel.xy,
			C5 * gradvel.xz,
			(-1.0f/3.0f*C1 + 2.0f/3.0f*C12 - 1.0f/3.0f*C13)*gradvel.xx + (-1.0f/3.0f*C12 + 2.0f/3.0f*C2 - 1.0f/3.0f*C23)*gradvel.yy + (-1.0f/3.0f*C13 + 2.0f/3.0f*C23 - 1.0f/3.0f*C3)*gradvel.zz,
			C6 * gradvel.yz,
			(-1.0f/3.0f*C1 - 1.0f/3.0f*C12 + 2.0f/3.0f*C13)*gradvel.xx + (-1.0f/3.0f*C12 - 1.0f/3.0f*C2 + 2.0f/3.0f*C23)*gradvel.yy + (-1.0f/3.0f*C13 - 1.0f/3.0f*C23 + 2.0f/3.0f*C3)*gradvel.zz };

		taudot[p].xx = E.xx + 2.0f*tau.xy*omega.xy + 2.0f*tau.xz*omega.xz;
		taudot[p].xy = E.xy + (tau.yy - tau.xx)*omega.xy + tau.xz*omega.yz + tau.yz*omega.xz;
		taudot[p].xz = E.xz + (tau.zz - tau.xx)*omega.xz - tau.xy*omega.yz + tau.yz*omega.xy;
		taudot[p].yy = E.yy - 2.0f*tau.xy*omega.xy + 2.0f*tau.yz*omega.yz;
		taudot[p].yz = E.yz + (tau.zz - tau.yy)*omega.yz - tau.xz*omega.xy - tau.xy*omega.xz;
		taudot[p].zz = E.zz - 2.0f*tau.xz*omega.xz - 2.0f*tau.yz*omega.yz;
	}
}

//==============================================================================
/// Computes stress tensor rate for solid (IMPLICIT) - Matthias 
//==============================================================================
/*void JSphSolidCpu::ComputeJauTauDotImplicit_M(unsigned n, unsigned pini, const double dt
	, const tsymatrix3f *gradvel, tsymatrix3f *tau, tsymatrix3f *taudot, tsymatrix3f *omega)const {
	const int pfin = int(pini + n);
	// Matthias - Implicit parameters
	const int N_IMP = 10;
	float epsilon_imp = 0.001f;


#ifdef OMP_USE
#pragma omp parallel for schedule (static)
#endif
	for (int p = int(pini); p<pfin; p++) {
		const tsymatrix3f gradvel = StrainDot_M[p];
		const tsymatrix3f omega = JauOmega_M[p];
		tsymatrix3f tautemp = Tauc_M[p];
		tsymatrix3f taubase, taudottemp;

		// Update Deviatoric Stress - Implicit
		int n_imp = 0;

		while ((max_M(abs_M(tautemp - taubase)) > epsilon_imp) && (n_imp<N_IMP)) {
			taubase = tautemp;
			const tsymatrix3f E = {
				2.0f / 3.0f * gradvel.xx - 1.0f / 3.0f * gradvel.yy - 1.0f / 3.0f * gradvel.zz,
				gradvel.xy,
				gradvel.xz,
				2.0f / 3.0f * gradvel.yy - 1.0f / 3.0f * gradvel.xx - 1.0f / 3.0f * gradvel.zz,
				gradvel.yz,
				2.0f / 3.0f * gradvel.zz - 1.0f / 3.0f * gradvel.xx - 1.0f / 3.0f * gradvel.yy };

			taudottemp = 2.0f*Mu*E + TSymatrix3f(
				2.0f*taubase.xy*omega.xy + 2.0f*taubase.xz*omega.xz,
				(taubase.yy - taubase.xx)*omega.xy + taubase.xz*omega.yz + taubase.yz*omega.xz,
				(taubase.zz - taubase.xx)*omega.xz - taubase.xy*omega.yz + taubase.yz*omega.xy,
				2.0f*Mu*E.yy - 2.0f*taubase.xy*omega.xy + 2.0f*taubase.yz*omega.yz,
				2.0f*Mu*E.yz + (taubase.zz - taubase.yy)*omega.yz - taubase.xz*omega.xy - taubase.xy*omega.xz,
				2.0f*Mu*E.zz - 2.0f*taubase.xz*omega.xz - 2.0f*taubase.yz*omega.yz);

			//EulerUpdate
			tautemp = tautemp + taudottemp*float(dt);
			n_imp++;
		}
		tau[p] = tautemp;
	}
}*/


//==============================================================================
/// Selection of template parameters for Interaction_ForcesFluidT.
/// Seleccion de parametros template para Interaction_ForcesFluidT.
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *spstau, tsymatrix3f *spsgradvel
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		//-Interaction Fluid-Fluid.
		InteractionForcesFluid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, spstau, spsgradvel, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesFluid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, spstau, spsgradvel, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);

		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}

//==============================================================================
/// Surcharche solide de IntForceT w NSPH - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tmatrix3f *L
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		//-Interaction Fluid-Fluid.
		InteractionForcesNSPH_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, L, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);

		//-Interaction Fluid-Bound.
		//InteractionForcesNSPH_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, L, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
			
		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
		ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}



//==============================================================================
/// Surcharche solide de IntForceT - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		//-Interaction Fluid-Fluid.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);

		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
		ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}

//==============================================================================
/// Surcharche solide de IntForceT - Matthias
//==============================================================================
/*template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const tfloat3 *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		//-Interaction Fluid-Fluid.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
				
		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
		ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}*/

//==============================================================================
/// Surcharche solide de IntForceT - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		/*//-Interaction Fluid-Fluid.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		*/

		//-Interaction Fluid-Fluid.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
				
		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
	    ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}

//==============================================================================
/// Surcharche solide de IntForceT w Press3D - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const tfloat3 *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		/*//-Interaction Fluid-Fluid.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolid<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		*/
		//-Interaction Fluid-Fluid.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		
		//-Interaction Fluid-Bound.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);

		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
		ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}



//==============================================================================
/// Surcharche solide, press1D et quad de IntForceT - Matthias
//==============================================================================
template<bool psingle, TpKernel tker, TpFtMode ftmode, bool lamsps, TpDeltaSph tdelta, bool shift> void JSphSolidCpu::Interaction_ForcesT
(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat3 *pspos, const tfloat4 *velrhop, const typecode *code, const unsigned *idp
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega, tsymatrix3f *qf
	, TpShifting tshifting, tfloat3 *shiftpos, float *shiftdetect)const
{
	const unsigned npf = np - npb;
	const tint4 nc = TInt4(int(ncells.x), int(ncells.y), int(ncells.z), int(ncells.x*ncells.y));
	const tint3 cellzero = TInt3(cellmin.x, cellmin.y, cellmin.z);
	const unsigned cellfluid = nc.w*nc.z + 1;
	const int hdiv = (CellMode == CELLMODE_H ? 2 : 1);

	if (npf) {
		//-Interaction Fluid-Fluid.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, cellfluid, Visco, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);
		//-Interaction Fluid-Bound.
		InteractionForcesSolMass_M<psingle, tker, ftmode, lamsps, tdelta, shift>(npf, npb, nc, hdiv, 0, Visco*ViscoBoundFactor, begincell, cellzero, dcell, jautau, jaugradvel, jauomega, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, tshifting, shiftpos, shiftdetect);

		//-Interaction of DEM Floating-Bound & Floating-Floating. //(DEM)
		if (USE_DEM)InteractionForcesDEM<psingle>(CaseNfloat, nc, hdiv, cellfluid, begincell, cellzero, dcell, FtRidp, DemData, pos, pspos, velrhop, code, idp, viscdt, ace);

		//-Computes tau for Laminar+SPS.
		//if (lamsps)ComputeSpsTau(npf, npb, velrhop, spsgradvel, spstau);

		// Compute Sdot
		ComputeJauTauDot_M(npf, npb, jaugradvel, jautau, jautaudot, jauomega);
	}
	if (npbok) {
		//-Interaction Bound-Fluid.
		InteractionForcesBound      <psingle, tker, ftmode>(npbok, 0, nc, hdiv, cellfluid, begincell, cellzero, dcell, pos, pspos, velrhop, code, idp, viscdt, ar);
	}
}

//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::Interaction_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::Interaction_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}


//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}


//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::Interaction_Forces(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *spstau, tsymatrix3f *spsgradvel
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
/// Selection of template parameters for Interaction_ForcesX.
/// Seleccion de parametros template para Interaction_ForcesX.
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *spstau, tsymatrix3f *spsgradvel
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, viscdt, ar, ace, delta, spstau, spsgradvel, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
// ===== PRESSURE 3D
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const tfloat3 *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

void JSphSolidCpu::Interaction_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const tfloat3 *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}



//==============================================================================
// ===== Press 1D / Quadratic form -- Matthias
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega, tsymatrix3f *qf
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}


void JSphSolidCpu::Interaction_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega, tsymatrix3f *qf
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, qf, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
// ===== P1SMQ NSPH -- Matthias
//==============================================================================
void JSphSolidCpu::InteractionSimple_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tfloat3 *pspos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, tmatrix3f *L
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tdouble3 *pos = NULL;
	const bool psingle = true;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass,  viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega,  L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega,  L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega,  L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}


void JSphSolidCpu::Interaction_Forces_M(unsigned np, unsigned npb, unsigned npbok
	, tuint3 ncells, const unsigned *begincell, tuint3 cellmin, const unsigned *dcell
	, const tdouble3 *pos, const tfloat4 *velrhop, const unsigned *idp, const typecode *code
	, const float *press, const float *pore, const float *mass
	, tmatrix3f *L
	, float &viscdt, float* ar, tfloat3 *ace, float *delta
	, tsymatrix3f *jautau, tsymatrix3f *jaugradvel, tsymatrix3f *jautaudot, tsymatrix3f *jauomega
	, tfloat3 *shiftpos, float *shiftdetect)const
{
	tfloat3 *pspos = NULL;
	const bool psingle = false;
	if (TKernel == KERNEL_Wendland) {
		const TpKernel tker = KERNEL_Wendland;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass,  viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega,  L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Gaussian) {
		const TpKernel tker = KERNEL_Gaussian;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
	else if (TKernel == KERNEL_Cubic) {
		const TpKernel tker = KERNEL_Cubic;
		if (!WithFloating) {
			const TpFtMode ftmode = FTMODE_None;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else if (!UseDEM) {
			const TpFtMode ftmode = FTMODE_Sph;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
		else {
			const TpFtMode ftmode = FTMODE_Dem;
			if (TShifting) {
				const bool tshift = true;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
			else {
				const bool tshift = false;
				if (TVisco == VISCO_LaminarSPS) {
					const bool lamsps = true;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega,  L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
				else {
					const bool lamsps = false;
					if (TDeltaSph == DELTA_None)      Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_None, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_Dynamic)   Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_Dynamic, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
					if (TDeltaSph == DELTA_DynamicExt)Interaction_ForcesT<psingle, tker, ftmode, lamsps, DELTA_DynamicExt, tshift>(np, npb, npbok, ncells, begincell, cellmin, dcell, pos, pspos, velrhop, code, idp, press, pore, mass, viscdt, ar, ace, delta, jautau, jaugradvel, jautaudot, jauomega, L, TShifting, shiftpos, shiftdetect);
				}
			}
		}
	}
}

//==============================================================================
/// Update pos, dcell and code to move with indicated displacement.
/// The value of outrhop indicates is it outside of the density limits.
/// Check the limits in funcion of MapRealPosMin & MapRealSize that this is valid
/// for single-cpu because DomRealPos & MapRealPos are equal. For multi-cpu it will be 
/// necessary to mark the particles that leave the domain without leaving the map.
///
/// Actualiza pos, dcell y code a partir del desplazamiento indicado.
/// El valor de outrhop indica si esta fuera de los limites de densidad.
/// Comprueba los limites en funcion de MapRealPosMin y MapRealSize esto es valido
/// para single-cpu pq DomRealPos y MapRealPos son iguales. Para multi-cpu seria 
/// necesario marcar las particulas q salgan del dominio sin salir del mapa.
//==============================================================================
void JSphSolidCpu::UpdatePos(tdouble3 rpos, double movx, double movy, double movz
	, bool outrhop, unsigned p, tdouble3 *pos, unsigned *cell, typecode *code)const
{
	//-Check validity of displacement. | Comprueba validez del desplazamiento.
	bool outmove = (fabs(float(movx))>MovLimit || fabs(float(movy))>MovLimit || fabs(float(movz))>MovLimit);
	//-Applies dsiplacement. | Aplica desplazamiento.
	rpos.x += movx; rpos.y += movy; rpos.z += movz;
	//-Check limits of real domain. | Comprueba limites del dominio reales.
	double dx = rpos.x - MapRealPosMin.x;
	double dy = rpos.y - MapRealPosMin.y;
	double dz = rpos.z - MapRealPosMin.z;
	bool out = (dx != dx || dy != dy || dz != dz || dx<0 || dy<0 || dz<0 || dx >= MapRealSize.x || dy >= MapRealSize.y || dz >= MapRealSize.z);
	//-Adjust position according to periodic conditions and compare domain limits. | Ajusta posicion segun condiciones periodicas y vuelve a comprobar los limites del dominio.
	if (PeriActive && out) {
		bool xperi = ((PeriActive & 1) != 0), yperi = ((PeriActive & 2) != 0), zperi = ((PeriActive & 4) != 0);
		if (xperi) {
			if (dx<0) { dx -= PeriXinc.x; dy -= PeriXinc.y; dz -= PeriXinc.z; }
			if (dx >= MapRealSize.x) { dx += PeriXinc.x; dy += PeriXinc.y; dz += PeriXinc.z; }
		}
		if (yperi) {
			if (dy<0) { dx -= PeriYinc.x; dy -= PeriYinc.y; dz -= PeriYinc.z; }
			if (dy >= MapRealSize.y) { dx += PeriYinc.x; dy += PeriYinc.y; dz += PeriYinc.z; }
		}
		if (zperi) {
			if (dz<0) { dx -= PeriZinc.x; dy -= PeriZinc.y; dz -= PeriZinc.z; }
			if (dz >= MapRealSize.z) { dx += PeriZinc.x; dy += PeriZinc.y; dz += PeriZinc.z; }
		}
		bool outx = !xperi && (dx<0 || dx >= MapRealSize.x);
		bool outy = !yperi && (dy<0 || dy >= MapRealSize.y);
		bool outz = !zperi && (dz<0 || dz >= MapRealSize.z);
		out = (outx || outy || outz);
		rpos = TDouble3(dx, dy, dz) + MapRealPosMin;
	}
	//-Keep current position. | Guarda posicion actualizada.
	pos[p] = rpos;
	//-Keep cell and check. | Guarda celda y check.
	if (outrhop || outmove || out) {//-Particle out.
		typecode rcode = code[p];
		if (out)rcode = CODE_SetOutPos(rcode);
		else if (outrhop)rcode = CODE_SetOutRhop(rcode);
		else rcode = CODE_SetOutMove(rcode);
		code[p] = rcode;
		cell[p] = 0xFFFFFFFF;
	}
	else {//-Particle in.
		if (PeriActive) {
			dx = rpos.x - DomPosMin.x;
			dy = rpos.y - DomPosMin.y;
			dz = rpos.z - DomPosMin.z;
		}
		unsigned cx = unsigned(dx / Scell), cy = unsigned(dy / Scell), cz = unsigned(dz / Scell);
		cell[p] = PC__Cell(DomCellCode, cx, cy, cz);
	}
}

//==============================================================================
/// Calculate new values of position, velocity & density for fluid (using Verlet).
/// Calcula nuevos valores de posicion, velocidad y densidad para el fluido (usando Verlet).
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeVerletVarsFluid(const tfloat4 *velrhop1, const tfloat4 *velrhop2, double dt, double dt2
	, tdouble3 *pos, unsigned *dcell, typecode *code, tfloat4 *velrhopnew)const
{
	const double dt205 = 0.5*dt*dt;
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = pini; p<pfin; p++) {
		//-Calculate density. | Calcula densidad.
		const float rhopnew = float(double(velrhop2[p].w) + dt2 * Arc[p]);
		if (!WithFloating || CODE_IsFluid(code[p])) {//-Fluid Particles.
													 //-Calculate displacement and update position. | Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop1[p].x)*dt + double(Acec[p].x)*dt205;
			double dy = double(velrhop1[p].y)*dt + double(Acec[p].y)*dt205;
			double dz = double(velrhop1[p].z)*dt + double(Acec[p].z)*dt205;
			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);
			//-Update velocity & density. | Actualiza velocidad y densidad.
			velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z)*dt2);
			velrhopnew[p].w = rhopnew;
		}
		else {//-Floating Particles.
			velrhopnew[p] = velrhop1[p];
			velrhopnew[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones. | Evita q las floating absorvan a las fluidas.
		}
	}
}

//==============================================================================
/// Calculate new values of position, velocity & density for Solid (using Verlet). - Matthias
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeVerletVarsSolid_M(const tfloat4 *velrhop1, const tfloat4 *velrhop2, const tsymatrix3f *tau1, const tsymatrix3f *tau2, double dt, double dt2
	, tdouble3 *pos, unsigned *dcell, typecode *code, tfloat4 *velrhopnew, tsymatrix3f *taunew)const
{
	const double dt205 = 0.5*dt*dt;
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = pini; p<pfin; p++) {
		//-Calculate density. | Calcula densidad.
		const float rhopnew = float(double(velrhop2[p].w) + dt2 * Arc[p]);
		if (!WithFloating || CODE_IsFluid(code[p])) {//-Fluid Particles.
													 //-Calculate displacement and update position. | Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop1[p].x)*dt + double(Acec[p].x)*dt205;
			double dy = double(velrhop1[p].y)*dt + double(Acec[p].y)*dt205;
			double dz = double(velrhop1[p].z)*dt + double(Acec[p].z)*dt205;
			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & density. | Actualiza velocidad y densidad.
			velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z)*dt2);
			velrhopnew[p].w = rhopnew;

			// Update Shear stress
			taunew[p].xx = float(double(tau2[p].xx) + double(TauDotc_M[p].xx)*dt2);
			taunew[p].xy = float(double(tau2[p].xy) + double(TauDotc_M[p].xy)*dt2);
			taunew[p].xz = float(double(tau2[p].xz) + double(TauDotc_M[p].xz)*dt2);
			taunew[p].yy = float(double(tau2[p].yy) + double(TauDotc_M[p].yy)*dt2);
			taunew[p].yz = float(double(tau2[p].yz) + double(TauDotc_M[p].yz)*dt2);
			taunew[p].zz = float(double(tau2[p].zz) + double(TauDotc_M[p].zz)*dt2);
		}
		else {//-Floating Particles.
			velrhopnew[p] = velrhop1[p];
			velrhopnew[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones. | Evita q las floating absorvan a las fluidas.
		}
	}
}

//==============================================================================
/// Verlet update with Solid, pore pressure and mass - Matthias
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeVerletVarsSolMass_M(const tfloat4 *velrhop1, const tfloat4 *velrhop2
	, const tsymatrix3f *tau1, const tsymatrix3f *tau2, const float *mass1, const float *mass2
	, double dt, double dt2, tdouble3 *pos, unsigned *dcell, typecode *code, tfloat4 *velrhopnew, tsymatrix3f *taunew, float *massnew)const
{
	const double dt205 = 0.5*dt*dt;
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = pini; p<pfin; p++) {
		//-Calculate density. | Calcula densidad.
		float rhopnew = float(double(velrhop2[p].w) + dt2 * Arc[p] );

		if (!WithFloating || CODE_IsFluid(code[p])) {//-Fluid Particles.
													 //-Calculate displacement and update position. | Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop1[p].x)*dt + double(Acec[p].x)*dt205;
			double dy = double(velrhop1[p].y)*dt + double(Acec[p].y)*dt205;
			double dz = double(velrhop1[p].z)*dt + double(Acec[p].z)*dt205;

			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
		//	printf("rvell ,race = %f,%f,%f,%f,%f,%f", velrhop1[p].x, velrhop1[p].y, velrhop1[p].z, Acec[p].x, Acec[p].y, Acec[p].z);
			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & NOT density. | Actualiza velocidad y densidad.
			velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z)*dt2); 
			// Update with growth momentum change

			// Update Shear stress
			taunew[p].xx = float(double(tau2[p].xx) + double(TauDotc_M[p].xx)*dt2);
			taunew[p].xy = float(double(tau2[p].xy) + double(TauDotc_M[p].xy)*dt2);
			taunew[p].xz = float(double(tau2[p].xz) + double(TauDotc_M[p].xz)*dt2);
			taunew[p].yy = float(double(tau2[p].yy) + double(TauDotc_M[p].yy)*dt2);
			taunew[p].yz = float(double(tau2[p].yz) + double(TauDotc_M[p].yz)*dt2);
			taunew[p].zz = float(double(tau2[p].zz) + double(TauDotc_M[p].zz)*dt2);

			// Source Density and Mass
			//const float volu = float(double(mass2[p]) / double(velrhop2[p].w));
			const float volu = float(double(mass2[p]) / double(rhopnew));
			//printf("M2: %.6f, Rho2: %.6f, V2: %.6f, ", mass2[p], velrhop2[p].w, volu);

			//float adens = float(LambdaMass * (1.0f - rhopnew / RhopZero));
			float adens = float(LambdaMass * (RhopZero / rhopnew - 1));
			rhopnew = float(rhopnew + dt2 * adens);
			//printf("Rho: %.6f, Drho2: %.6f, ", rhopnew, adens);

			// Update mass, velocity and density
			massnew[p] = float(double(mass2[p]) + dt2 * double(adens*volu));
			// There is justifications to DO NOT take into account momentum change due to mass growth [Chapman2012]
			/*velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x + adens * velrhop2[p].x / rhopnew)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y + adens * velrhop2[p].y / rhopnew)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z + adens * velrhop2[p].z / rhopnew)*dt2);*/
			velrhopnew[p].w = rhopnew;
		}
		else {//-Floating Particles.
			velrhopnew[p] = velrhop1[p];
			velrhopnew[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones. | Evita q las floating absorvan a las fluidas.
		}
	}
}


//==============================================================================
/// Verlet update with Solid, pore, mass and quadratic form - Matthias
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeVerletVarsQuad_M(const tfloat4 *velrhop1, const tfloat4 *velrhop2
	, const tsymatrix3f *tau1, const tsymatrix3f *tau2, const tsymatrix3f *qf1, const tsymatrix3f *qf2, const float *mass1, const float *mass2
	, double dt, double dt2, tdouble3 *pos, unsigned *dcell, typecode *code, tfloat4 *velrhopnew, tsymatrix3f *taunew, tsymatrix3f *qfnew, float *massnew)const
{
	const double dt205 = 0.5*dt*dt;
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = pini; p<pfin; p++) {
		//-Calculate density. | Calcula densidad.
		float rhopnew = float(double(velrhop2[p].w) + dt2 * Arc[p]);

		if (!WithFloating || CODE_IsFluid(code[p])) {//-Fluid Particles.
													 //-Calculate displacement and update position. | Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop1[p].x)*dt + double(Acec[p].x)*dt205;
			double dy = double(velrhop1[p].y)*dt + double(Acec[p].y)*dt205;
			double dz = double(velrhop1[p].z)*dt + double(Acec[p].z)*dt205;

			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
			//	printf("rvell ,race = %f,%f,%f,%f,%f,%f", velrhop1[p].x, velrhop1[p].y, velrhop1[p].z, Acec[p].x, Acec[p].y, Acec[p].z);
			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & NOT density. | Actualiza velocidad y densidad.
			velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z)*dt2);
			// Update with growth momentum change

			// Update Shear stress
			taunew[p].xx = float(double(tau2[p].xx) + double(TauDotc_M[p].xx)*dt2);
			taunew[p].xy = float(double(tau2[p].xy) + double(TauDotc_M[p].xy)*dt2);
			taunew[p].xz = float(double(tau2[p].xz) + double(TauDotc_M[p].xz)*dt2);
			taunew[p].yy = float(double(tau2[p].yy) + double(TauDotc_M[p].yy)*dt2);
			taunew[p].yz = float(double(tau2[p].yz) + double(TauDotc_M[p].yz)*dt2);
			taunew[p].zz = float(double(tau2[p].zz) + double(TauDotc_M[p].zz)*dt2);

			// Update Quadratic form -- //Should include GradVel
			tmatrix3f Q = TMatrix3f(qf2[p].xx, qf2[p].xy, qf2[p].xz, qf2[p].xy, qf2[p].yy, qf2[p].yz, qf2[p].xz, qf2[p].yz, qf2[p].zz);
			tmatrix3f GdVel = 0.5* TMatrix3f(StrainDotc_M[p].xx, StrainDotc_M[p].xy, StrainDotc_M[p].xz
				, StrainDotc_M[p].xy, StrainDotc_M[p].yy, StrainDotc_M[p].yz
				, StrainDotc_M[p].xz, StrainDotc_M[p].yz, StrainDotc_M[p].zz) - 0.5f* TMatrix3f(Spinc_M[p].xx, Spinc_M[p].xy, Spinc_M[p].xz
					, -Spinc_M[p].xy, Spinc_M[p].yy, Spinc_M[p].yz
					, -Spinc_M[p].xz, -Spinc_M[p].yz, Spinc_M[p].zz);
			tmatrix3f DQD = ToTMatrix3f((TMatrix3d(1, 0, 0, 0, 1, 0, 0, 0, 1) - dt2
				* ToTMatrix3d(Ttransp(GdVel))) * ToTMatrix3d(Q) * (TMatrix3d(1, 0, 0, 0, 1, 0, 0, 0, 1) - dt2 * ToTMatrix3d(GdVel)));
			//tmatrix3f Pe = DQD;
			//printf("MatProd: %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f\n", Pe.a11, Pe.a12, Pe.a13, Pe.a21, Pe.a22, Pe.a23, Pe.a31, Pe.a32, Pe.a33);

			qfnew[p].xx = float(DQD.a11);
			qfnew[p].xy = float(DQD.a12);
			qfnew[p].xz = float(DQD.a13);
			qfnew[p].yy = float(DQD.a22);
			qfnew[p].yz = float(DQD.a23);
			qfnew[p].zz = float(DQD.a33);
			//printf("Q: %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f\n", Q.a11, Q.a12, Q.a13, Q.a21, Q.a22, Q.a23, Q.a31, Q.a32, Q.a33);
			//printf("DQD: %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f, %.5f\n", DQD.a11, DQD.a12, DQD.a13, DQD.a21, DQD.a22, DQD.a23, DQD.a31, DQD.a32, DQD.a33);

			// Source Density and Mass
			//const float volu = float(double(mass2[p]) / double(velrhop2[p].w));
			const float volu = float(double(mass2[p]) / double(rhopnew));
			//printf("M2: %.6f, Rho2: %.6f, V2: %.6f, ", mass2[p], velrhop2[p].w, volu);

			//float adens = float(LambdaMass * (1.0f - rhopnew / RhopZero));
			float adens = float(LambdaMass * (RhopZero / rhopnew - 1));
			rhopnew = float(rhopnew + dt2 * adens);
			//printf("Rho: %.6f, Drho2: %.6f, ", rhopnew, adens);

			// Update mass, velocity and density
			massnew[p] = float(double(mass2[p]) + dt2 * double(adens*volu));
			// There is justifications to DO NOT take into account momentum change due to mass growth [Chapman2012]
			/*velrhopnew[p].x = float(double(velrhop2[p].x) + double(Acec[p].x + adens * velrhop2[p].x / rhopnew)*dt2);
			velrhopnew[p].y = float(double(velrhop2[p].y) + double(Acec[p].y + adens * velrhop2[p].y / rhopnew)*dt2);
			velrhopnew[p].z = float(double(velrhop2[p].z) + double(Acec[p].z + adens * velrhop2[p].z / rhopnew)*dt2);*/
			velrhopnew[p].w = rhopnew;
		}
		else {//-Floating Particles.
			velrhopnew[p] = velrhop1[p];
			velrhopnew[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones. | Evita q las floating absorvan a las fluidas.
		}

	}

}

//==============================================================================
/// Calculate new values of position, velocity & density for fluid (using Euler)
/// Matthias
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeEulerVarsFluid_M(tfloat4 *velrhop, double dt, tdouble3 *pos, unsigned *dcell, word *code)const
{
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef _WITHOMP
#pragma omp parallel for schedule (static) if(npf>LIMIT_COMPUTESTEP_OMP)
#endif
	for (int p = pini; p < pfin; p++) {
		const float rhopnew = float(double(velrhop[p].w) + dt * Arc[p]);
		if (!WithFloating || CODE_GetType(code[p]) == CODE_TYPE_FLUID) {//-Fluid Particles / Particulas: Fluid
																		//-Calculate displacement and update position / Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop[p].x)*dt;
			double dy = double(velrhop[p].y)*dt;
			double dz = double(velrhop[p].z)*dt;

			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);

			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & density / Actualiza velocidad y densidad. 
			// and mass
			velrhop[p].x = float(double(velrhop[p].x) + double(Acec[p].x)*dt);
			velrhop[p].y = float(double(velrhop[p].y) + double(Acec[p].y)*dt);
			velrhop[p].z = float(double(velrhop[p].z) + double(Acec[p].z)*dt);
			velrhop[p].w = rhopnew;
		}
		else {//-Fluid Particles / Particulas: Floating
			velrhop[p].w = (rhopnew < RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones / Evita q las floating absorvan a las fluidas.
		}
	}
}

//==============================================================================
/// Calculate new values of position, velocity & density for Solid (w Euler)
/// Matthias
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeEulerVarsSolid_M(tfloat4 *velrhop, double dt
	, tdouble3 *pos, tsymatrix3f *tau, unsigned *dcell, word *code)const
{
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef _WITHOMP
#pragma omp parallel for schedule (static) if(npf>LIMIT_COMPUTESTEP_OMP)
#endif
	for (int p = pini; p < pfin; p++) {
		const float rhopnew = float(double(velrhop[p].w) + dt * Arc[p]);
		if (!WithFloating || CODE_GetType(code[p]) == CODE_TYPE_FLUID) {//-Fluid Particles / Particulas: Fluid
																		//-Calculate displacement and update position / Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop[p].x)*dt;
			double dy = double(velrhop[p].y)*dt;
			double dz = double(velrhop[p].z)*dt;

			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);

			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & density / Actualiza velocidad y densidad. 
			// and mass
			velrhop[p].x = float(double(velrhop[p].x) + double(Acec[p].x)*dt);
			velrhop[p].y = float(double(velrhop[p].y) + double(Acec[p].y)*dt);
			velrhop[p].z = float(double(velrhop[p].z) + double(Acec[p].z)*dt);
			velrhop[p].w = rhopnew;

			// Update Deviatoric Stress
			//ComputeJauTauDot_M(npf, npb, velrhop, jaugradvel, jautau, jautaudot, jauomega);
			tau[p].xx = float(double(tau[p].xx) + double(TauDotc_M[p].xx)*dt);
			tau[p].xy = float(double(tau[p].xy) + double(TauDotc_M[p].xy)*dt);
			tau[p].xz = float(double(tau[p].xz) + double(TauDotc_M[p].xz)*dt);
			tau[p].yy = float(double(tau[p].yy) + double(TauDotc_M[p].yy)*dt);
			tau[p].yz = float(double(tau[p].yz) + double(TauDotc_M[p].yz)*dt);
			tau[p].zz = float(double(tau[p].zz) + double(TauDotc_M[p].zz)*dt);
		}
		else {//-Fluid Particles / Particulas: Floating
			velrhop[p].w = (rhopnew < RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones / Evita q las floating absorvan a las fluidas.
		}
	}
}

//==============================================================================
/// Calculate new values (IMPLICIT) of position, velocity & density for Solid (w Euler)
/// Matthias
//==============================================================================

template<bool shift> void JSphSolidCpu::ComputeEulerVarsSolidImplicit_M(tfloat4 *velrhop, double dt
	, tdouble3 *pos, tsymatrix3f *tau, tsymatrix3f *gradvel, tsymatrix3f *omega, unsigned *dcell, word *code)const
{
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
	// Matthias - Implicit parameters
	const int N_IMP = 100;
	int n_imp = 0;
	float epsilon_imp = 0.001f;

#ifdef _WITHOMP
#pragma omp parallel for schedule (static) if(npf>LIMIT_COMPUTESTEP_OMP)
#endif
	for (int p = pini; p < pfin; p++) {
		const float rhopnew = float(double(velrhop[p].w) + dt * Arc[p]);
		if (!WithFloating || CODE_GetType(code[p]) == CODE_TYPE_FLUID) {//-Fluid Particles / Particulas: Fluid
																		//-Calculate displacement and update position / Calcula desplazamiento y actualiza posicion.
			double dx = double(velrhop[p].x)*dt;
			double dy = double(velrhop[p].y)*dt;
			double dz = double(velrhop[p].z)*dt;

			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);

			UpdatePos(pos[p], dx, dy, dz, outrhop, p, pos, dcell, code);

			//-Update velocity & density / Actualiza velocidad y densidad. 
			// and mass
			velrhop[p].x = float(double(velrhop[p].x) + double(Acec[p].x)*dt);
			velrhop[p].y = float(double(velrhop[p].y) + double(Acec[p].y)*dt);
			velrhop[p].z = float(double(velrhop[p].z) + double(Acec[p].z)*dt);
			velrhop[p].w = rhopnew;

			// Update Deviatoric Stress - Implicit

			/*tau[p].xx = float(double(tau[p].xx) + double(JauTauDot_M[p].xx)*dt);
			tau[p].xy = float(double(tau[p].xy) + double(JauTauDot_M[p].xy)*dt);
			tau[p].xz = float(double(tau[p].xz) + double(JauTauDot_M[p].xz)*dt);
			tau[p].yy = float(double(tau[p].yy) + double(JauTauDot_M[p].yy)*dt);
			tau[p].yz = float(double(tau[p].yz) + double(JauTauDot_M[p].yz)*dt);
			tau[p].zz = float(double(tau[p].zz) + double(JauTauDot_M[p].zz)*dt);*/
		}
		else {//-Fluid Particles / Particulas: Floating
			velrhop[p].w = (rhopnew < RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorved by floating ones / Evita q las floating absorvan a las fluidas.
		}
	}
}

//==============================================================================
/// Calculate new values of density and set velocity=zero for cases of  
/// (fixed+moving, no floating).
///
/// Calcula nuevos valores de densidad y pone velocidad a cero para el contorno 
/// (fixed+moving, no floating).
//==============================================================================
void JSphSolidCpu::ComputeVelrhopBound(const tfloat4* velrhopold, double armul, tfloat4* velrhopnew)const {
	const int npb = int(Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npb>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = 0; p<npb; p++) {
		const float rhopnew = float(double(velrhopold[p].w) + armul * Arc[p]);
		velrhopnew[p] = TFloat4(0, 0, 0, (rhopnew<RhopZero ? RhopZero : rhopnew));//-Avoid fluid particles being absorved by boundary ones. | Evita q las boundary absorvan a las fluidas.
	}
}

//==============================================================================
/// Update of particles according to forces and dt using Verlet.
/// Actualizacion de particulas segun fuerzas y dt usando Verlet.
//==============================================================================
void JSphSolidCpu::ComputeVerlet(double dt) {
	TmcStart(Timers, TMC_SuComputeStep);
	VerletStep++;
	//printf("VerletStep: %d\n", VerletStep);
	/*if (VerletStep<VerletSteps) {
		const double twodt = dt + dt;
		if (TShifting)ComputeVerletVarsSolid_M<true>(Velrhopc, VelrhopM1c, Tauc2_M, JauTauM1c2_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, Tauc2_M);
		else         ComputeVerletVarsSolid_M<false>(Velrhopc, VelrhopM1c, Tauc2_M, JauTauM1c2_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, Tauc2_M);
		ComputeVelrhopBound(VelrhopM1c, twodt, VelrhopM1c);
	}
	else {
		if (TShifting)ComputeVerletVarsSolid_M<true>(Velrhopc, Velrhopc, Tauc2_M, Tauc2_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, Tauc2_M);
		else         ComputeVerletVarsSolid_M<false>(Velrhopc, Velrhopc, Tauc2_M, Tauc2_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, Tauc2_M);
		ComputeVelrhopBound(Velrhopc, dt, VelrhopM1c);
		VerletStep = 0;
	}*/
	/*if (VerletStep<VerletSteps) {
		const double twodt = dt + dt;
		if (TShifting)ComputeVerletVarsSolMass_M<true>(Velrhopc, VelrhopM1c, Tauc2_M, JauTauM1c2_M, Massc_M, MassM1c_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, JauTauM1c2_M, MassM1c_M);
		else         ComputeVerletVarsSolMass_M<false>(Velrhopc, VelrhopM1c, Tauc2_M, JauTauM1c2_M, Massc_M, MassM1c_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, JauTauM1c2_M, MassM1c_M);
		ComputeVelrhopBound(VelrhopM1c, twodt, VelrhopM1c);
	}
	else {
		if (TShifting)ComputeVerletVarsSolMass_M<true>(Velrhopc, Velrhopc, Tauc2_M, Tauc2_M, Massc_M, Massc_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, JauTauM1c2_M, MassM1c_M);
		else         ComputeVerletVarsSolMass_M<false>(Velrhopc, Velrhopc, Tauc2_M, Tauc2_M, Massc_M, Massc_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, JauTauM1c2_M, MassM1c_M);
		ComputeVelrhopBound(Velrhopc, dt, VelrhopM1c);
		VerletStep = 0;
	}*/

	if (VerletStep<VerletSteps) {
		const double twodt = dt + dt;
		if (TShifting)ComputeVerletVarsQuad_M<true>(Velrhopc, VelrhopM1c, Tauc_M, TauM1c_M, QuadFormc_M, QuadFormM1c_M, Massc_M, MassM1c_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, TauM1c_M, QuadFormM1c_M, MassM1c_M);
		else         ComputeVerletVarsQuad_M<false>(Velrhopc, VelrhopM1c, Tauc_M, TauM1c_M, QuadFormc_M, QuadFormM1c_M, Massc_M, MassM1c_M, dt, twodt, Posc, Dcellc, Codec, VelrhopM1c, TauM1c_M, QuadFormM1c_M, MassM1c_M);
		ComputeVelrhopBound(VelrhopM1c, twodt, VelrhopM1c);
	}
	else {
		if (TShifting)ComputeVerletVarsQuad_M<true>(Velrhopc, Velrhopc, Tauc_M, Tauc_M, QuadFormc_M, QuadFormc_M, Massc_M, Massc_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, TauM1c_M, QuadFormM1c_M, MassM1c_M);
		else         ComputeVerletVarsQuad_M<false>(Velrhopc, Velrhopc, Tauc_M, Tauc_M, QuadFormc_M, QuadFormc_M, Massc_M, Massc_M, dt, dt, Posc, Dcellc, Codec, VelrhopM1c, TauM1c_M, QuadFormM1c_M, MassM1c_M);
		ComputeVelrhopBound(Velrhopc, dt, VelrhopM1c);
		VerletStep = 0;
	}


	//-New values are calculated en VelrhopM1c.
	swap(Velrhopc, VelrhopM1c);     //-Swap Velrhopc & VelrhopM1c. | Intercambia Velrhopc y VelrhopM1c.
	swap(Tauc_M, TauM1c_M);     //-Swap Velrhopc & VelrhopM1c. | Intercambia Velrhopc y VelrhopM1c.
	swap(QuadFormc_M, QuadFormM1c_M);     //-Swap Velrhopc & VelrhopM1c. | Intercambia Velrhopc y VelrhopM1c.
	swap(Massc_M, MassM1c_M);     //-Swap Velrhopc & VelrhopM1c. | Intercambia Velrhopc y VelrhopM1c.
	TmcStop(Timers, TMC_SuComputeStep);
}

//==============================================================================
/// Update of particles according to forces and dt using Euler
/// Matthias
//==============================================================================
void JSphSolidCpu::ComputeEuler_M(double dt) {
	TmcStart(Timers, TMC_SuComputeStep);
	//if (TShifting)ComputeEulerVarsFluid_M<true>(Velrhopc, dt, Posc, Dcellc, Codec);
	//else         ComputeEulerVarsFluid_M<false>(Velrhopc, dt, Posc, Dcellc, Codec);

	if (TShifting)ComputeEulerVarsSolid_M<true>(Velrhopc, dt, Posc, Tauc_M, Dcellc, Codec);
	else         ComputeEulerVarsSolid_M<false>(Velrhopc, dt, Posc, Tauc_M, Dcellc, Codec);

	//ComputeJauTauDotImplicit_M(Np, Npb, dt, StrainDot_M, Tauc2_M, JauTauDot_M, JauOmega_M);
	//if (TShifting)ComputeEulerVarsSolidImplicit_M<true>(Velrhopc, dt, Posc, Tauc2_M, StrainDot_M, JauOmega_M, Dcellc, Codec);
	//else         ComputeEulerVarsSolidImplicit_M<false>(Velrhopc, dt, Posc, Tauc2_M, StrainDot_M, JauOmega_M, Dcellc, Codec);

	ComputeVelrhopBound(Velrhopc, dt, Velrhopc);
	TmcStop(Timers, TMC_SuComputeStep);
}

//==============================================================================
/// Update of particles according to forces and dt using Symplectic-Predictor.
/// Actualizacion de particulas segun fuerzas y dt usando Symplectic-Predictor.
//==============================================================================
void JSphSolidCpu::ComputeSymplecticPre(double dt) {
	if (TShifting)ComputeSymplecticPreT<false>(dt); //-We strongly recommend running the shifting correction only for the corrector. If you want to re-enable shifting in the predictor, change the value here to "true".
	else         ComputeSymplecticPreT<false>(dt);
}

//==============================================================================
/// Update of particles according to forces and dt using Symplectic-Predictor.
/// Actualizacion de particulas segun fuerzas y dt usando Symplectic-Predictor.
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeSymplecticPreT(double dt) {
	TmcStart(Timers, TMC_SuComputeStep);
	//-Assign memory to variables Pre. | Asigna memoria a variables Pre.
	PosPrec = ArraysCpu->ReserveDouble3();
	VelrhopPrec = ArraysCpu->ReserveFloat4();
	//-Change data to variables Pre to calculate new data. | Cambia datos a variables Pre para calcular nuevos datos.
	swap(PosPrec, Posc);         //Put value of Pos[] in PosPre[].         | Es decir... PosPre[] <= Pos[].
	swap(VelrhopPrec, Velrhopc); //Put value of Velrhop[] in VelrhopPre[]. | Es decir... VelrhopPre[] <= Velrhop[].
								 //-Calculate new values of particles. | Calcula nuevos datos de particulas.
	const double dt05 = dt * .5;

	//-Calculate new density for boundary and copy velocity. | Calcula nueva densidad para el contorno y copia velocidad.
	const int npb = int(Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npb>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = 0; p<npb; p++) {
		const tfloat4 vr = VelrhopPrec[p];
		const float rhopnew = float(double(vr.w) + dt05 * Arc[p]);
		Velrhopc[p] = TFloat4(vr.x, vr.y, vr.z, (rhopnew<RhopZero ? RhopZero : rhopnew));//-Avoid fluid particles being absorbed by boundary ones. | Evita q las boundary absorvan a las fluidas.
	}

	//-Calculate new values of fluid. | Calcula nuevos datos del fluido.
	const int np = int(Np);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(np>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = npb; p<np; p++) {
		//-Calculate density.
		const float rhopnew = float(double(VelrhopPrec[p].w) + dt05 * Arc[p]);
		if (!WithFloating || CODE_IsFluid(Codec[p])) {//-Fluid Particles.
													  //-Calculate displacement & update position. | Calcula desplazamiento y actualiza posicion.
			double dx = double(VelrhopPrec[p].x)*dt05;
			double dy = double(VelrhopPrec[p].y)*dt05;
			double dz = double(VelrhopPrec[p].z)*dt05;
			if (shift) {
				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
			UpdatePos(PosPrec[p], dx, dy, dz, outrhop, p, Posc, Dcellc, Codec);
			//-Update velocity & density. | Actualiza velocidad y densidad.
			Velrhopc[p].x = float(double(VelrhopPrec[p].x) + double(Acec[p].x)* dt05);
			Velrhopc[p].y = float(double(VelrhopPrec[p].y) + double(Acec[p].y)* dt05);
			Velrhopc[p].z = float(double(VelrhopPrec[p].z) + double(Acec[p].z)* dt05);
			Velrhopc[p].w = rhopnew;
		}
		else {//-Floating Particles.
			Velrhopc[p] = VelrhopPrec[p];
			Velrhopc[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorbed by floating ones. | Evita q las floating absorvan a las fluidas.
																	 //-Copy position. | Copia posicion.
			Posc[p] = PosPrec[p];
		}
	}

	//-Copy previous position of boundary. | Copia posicion anterior del contorno.
	memcpy(Posc, PosPrec, sizeof(tdouble3)*Npb);

	TmcStop(Timers, TMC_SuComputeStep);
}

//==============================================================================
/// Update particles according to forces and dt using Symplectic-Corrector.
/// Actualizacion de particulas segun fuerzas y dt usando Symplectic-Corrector.
//==============================================================================
void JSphSolidCpu::ComputeSymplecticCorr(double dt) {
	if (TShifting)ComputeSymplecticCorrT<true>(dt);
	else         ComputeSymplecticCorrT<false>(dt);
}

//==============================================================================
/// Update particles according to forces and dt using Symplectic-Corrector.
/// Actualizacion de particulas segun fuerzas y dt usando Symplectic-Corrector.
//==============================================================================
template<bool shift> void JSphSolidCpu::ComputeSymplecticCorrT(double dt) {
	TmcStart(Timers, TMC_SuComputeStep);

	//-Calculate rhop of boudary and set velocity=0. | Calcula rhop de contorno y vel igual a cero.
	const int npb = int(Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npb>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = 0; p<npb; p++) {
		const double epsilon_rdot = (-double(Arc[p]) / double(Velrhopc[p].w))*dt;
		const float rhopnew = float(double(VelrhopPrec[p].w) * (2. - epsilon_rdot) / (2. + epsilon_rdot));
		Velrhopc[p] = TFloat4(0, 0, 0, (rhopnew<RhopZero ? RhopZero : rhopnew));//-Avoid fluid particles being absorbed by boundary ones. | Evita q las boundary absorvan a las fluidas.
	}

	//-Calculate fluid values. | Calcula datos de fluido.
	const double dt05 = dt * .5;
	const int np = int(Np);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(np>OMP_LIMIT_COMPUTESTEP)
#endif
	for (int p = npb; p<np; p++) {
		const double epsilon_rdot = (-double(Arc[p]) / double(Velrhopc[p].w))*dt;
		const float rhopnew = float(double(VelrhopPrec[p].w) * (2. - epsilon_rdot) / (2. + epsilon_rdot));
		if (!WithFloating || CODE_IsFluid(Codec[p])) {//-Fluid Particles.
													  //-Update velocity & density. | Actualiza velocidad y densidad.
			Velrhopc[p].x = float(double(VelrhopPrec[p].x) + double(Acec[p].x) * dt);
			Velrhopc[p].y = float(double(VelrhopPrec[p].y) + double(Acec[p].y) * dt);
			Velrhopc[p].z = float(double(VelrhopPrec[p].z) + double(Acec[p].z) * dt);
			Velrhopc[p].w = rhopnew;
			//-Calculate displacement and update position. | Calcula desplazamiento y actualiza posicion.
			double dx = (double(VelrhopPrec[p].x) + double(Velrhopc[p].x)) * dt05;
			double dy = (double(VelrhopPrec[p].y) + double(Velrhopc[p].y)) * dt05;
			double dz = (double(VelrhopPrec[p].z) + double(Velrhopc[p].z)) * dt05;
			if (shift) {

				dx += double(ShiftPosc[p].x);
				dy += double(ShiftPosc[p].y);
				dz += double(ShiftPosc[p].z);
			}
			bool outrhop = (rhopnew<RhopOutMin || rhopnew>RhopOutMax);
			UpdatePos(PosPrec[p], dx, dy, dz, outrhop, p, Posc, Dcellc, Codec);
		}
		else {//-Floating Particles.
			Velrhopc[p] = VelrhopPrec[p];
			Velrhopc[p].w = (rhopnew<RhopZero ? RhopZero : rhopnew); //-Avoid fluid particles being absorbed by floating ones. | Evita q las floating absorvan a las fluidas.
																	 //-Copy position. | Copia posicion.
			Posc[p] = PosPrec[p];
		}
	}

	//-Free memory assigned to variables Pre and ComputeSymplecticPre(). | Libera memoria asignada a variables Pre en ComputeSymplecticPre().
	ArraysCpu->Free(PosPrec);      PosPrec = NULL;
	ArraysCpu->Free(VelrhopPrec);  VelrhopPrec = NULL;
	TmcStop(Timers, TMC_SuComputeStep);
}

//==============================================================================
/// Calculate variable Dt.
/// Calcula un Dt variable.
//==============================================================================
double JSphSolidCpu::DtVariable(bool final) {
	//-dt1 depends on force per unit mass.
	//printf("Acemax: %.8f\n", AceMax);
	const double dt1 = (AceMax ? (sqrt(double(H) / AceMax)) : DBL_MAX);
	//-dt2 combines the Courant and the viscous time-step controls.
	const double dt2 = double(H) / (max(Cs0, VelMax*10.) + double(H)*ViscDtMax);
	//-dt new value of time step.
	double dt = double(CFLnumber)*min(dt1, dt2);
	if (DtFixed)dt = DtFixed->GetDt(float(TimeStep), float(dt));
	if (dt<double(DtMin)) {
		dt = double(DtMin); DtModif++;
		if (DtModif >= DtModifWrn) {
			Log->PrintfWarning("%d DTs adjusted to DtMin (t:%g, nstep:%u)", DtModif, TimeStep, Nstep);
			DtModifWrn *= 10;
		}
	}
	if (SaveDt && final)SaveDt->AddValues(TimeStep, dt, dt1*CFLnumber, dt2*CFLnumber, AceMax, ViscDtMax, VelMax);
	//if (TimeStep>=TimePartNext) {
	//	printf("A: %.4f, V: %.4f, Visc: %.4f\n", AceMax, VelMax, ViscDtMax);
	//	printf("Dt1: %.4f, Dt2: %.4f\n", dt1, dt2);
	//	printf("Dt: %.4f\n", dt);
	//}
	return(dt);
}

//==============================================================================
/// Calculate final Shifting for particles' position.
/// Calcula Shifting final para posicion de particulas.
//==============================================================================
void JSphSolidCpu::RunShifting(double dt) {
	TmcStart(Timers, TMC_SuShifting);
	const double coeftfs = (Simulate2D ? 2.0 : 3.0) - ShiftTFS;
	const int pini = int(Npb), pfin = int(Np), npf = int(Np - Npb);
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(npf>OMP_LIMIT_COMPUTELIGHT)
#endif
	for (int p = pini; p<pfin; p++) {
		double vx = double(Velrhopc[p].x);
		double vy = double(Velrhopc[p].y);
		double vz = double(Velrhopc[p].z);
		double umagn = double(ShiftCoef)*double(H)*sqrt(vx*vx + vy * vy + vz * vz)*dt;
		if (ShiftDetectc) {
			if (ShiftDetectc[p]<ShiftTFS)umagn = 0;
			else umagn *= (double(ShiftDetectc[p]) - ShiftTFS) / coeftfs;
		}
		if (ShiftPosc[p].x == FLT_MAX)umagn = 0; //-Zero shifting near boundary. | Anula shifting por proximidad del contorno.
		const float maxdist = 0.1f*float(Dp); //-Max shifting distance permitted (recommended).
		const float shiftdistx = float(double(ShiftPosc[p].x)*umagn);
		const float shiftdisty = float(double(ShiftPosc[p].y)*umagn);
		const float shiftdistz = float(double(ShiftPosc[p].z)*umagn);
		ShiftPosc[p].x = (shiftdistx<maxdist ? shiftdistx : maxdist);
		ShiftPosc[p].y = (shiftdisty<maxdist ? shiftdisty : maxdist);
		ShiftPosc[p].z = (shiftdistz<maxdist ? shiftdistz : maxdist);
	}
	TmcStop(Timers, TMC_SuShifting);
}

//==============================================================================
/// Calculate position of particles according to idp[]. When it is not met set as UINT_MAX.
/// When periactive is False assume that there are no duplicate particles (periodic ones)
/// and all are set as CODE_NORMAL.
///
/// Calcula posicion de particulas segun idp[]. Cuando no la encuentra es UINT_MAX.
/// Cuando periactive es False supone que no hay particulas duplicadas (periodicas)
/// y todas son CODE_NORMAL.
//==============================================================================
void JSphSolidCpu::CalcRidp(bool periactive, unsigned np, unsigned pini, unsigned idini, unsigned idfin, const typecode *code, const unsigned *idp, unsigned *ridp)const {
	//-Assign values UINT_MAX. | Asigna valores UINT_MAX.
	const unsigned nsel = idfin - idini;
	memset(ridp, 255, sizeof(unsigned)*nsel);
	//-Calculate position according to id. | Calcula posicion segun id.
	const int pfin = int(pini + np);
	if (periactive) {//-Calculate position according to id checking that the particles are normal (i.e. not periodic). | Calcula posicion segun id comprobando que las particulas son normales (no periodicas).
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(pfin>OMP_LIMIT_COMPUTELIGHT)
#endif
		for (int p = int(pini); p<pfin; p++) {
			const unsigned id = idp[p];
			if (idini <= id && id<idfin) {
				if (CODE_IsNormal(code[p]))ridp[id - idini] = p;
			}
		}
	}
	else {//-Calculate position according to id assuming that all the particles are normal (i.e. not periodic). | Calcula posicion segun id suponiendo que todas las particulas son normales (no periodicas).
#ifdef OMP_USE
#pragma omp parallel for schedule (static) if(pfin>OMP_LIMIT_COMPUTELIGHT)
#endif
		for (int p = int(pini); p<pfin; p++) {
			const unsigned id = idp[p];
			if (idini <= id && id<idfin)ridp[id - idini] = p;
		}
	}
}

//==============================================================================
/// Applies a linear movement to a group of particles.
/// Aplica un movimiento lineal a un conjunto de particulas.
//==============================================================================
void JSphSolidCpu::MoveLinBound(unsigned np, unsigned ini, const tdouble3 &mvpos, const tfloat3 &mvvel
	, const unsigned *ridp, tdouble3 *pos, unsigned *dcell, tfloat4 *velrhop, typecode *code)const
{
	const unsigned fin = ini + np;
	for (unsigned id = ini; id<fin; id++) {
		const unsigned pid = RidpMove[id];
		if (pid != UINT_MAX) {
			UpdatePos(pos[pid], mvpos.x, mvpos.y, mvpos.z, false, pid, pos, dcell, code);
			velrhop[pid].x = mvvel.x;  velrhop[pid].y = mvvel.y;  velrhop[pid].z = mvvel.z;
		}
	}
}

//==============================================================================
/// Applies a matrix movement to a group of particles.
/// Aplica un movimiento matricial a un conjunto de particulas.
//==============================================================================
void JSphSolidCpu::MoveMatBound(unsigned np, unsigned ini, tmatrix4d m, double dt
	, const unsigned *ridpmv, tdouble3 *pos, unsigned *dcell, tfloat4 *velrhop, typecode *code)const
{
	const unsigned fin = ini + np;
	for (unsigned id = ini; id<fin; id++) {
		const unsigned pid = RidpMove[id];
		if (pid != UINT_MAX) {
			tdouble3 ps = pos[pid];
			tdouble3 ps2 = MatrixMulPoint(m, ps);
			if (Simulate2D)ps2.y = ps.y;
			const double dx = ps2.x - ps.x, dy = ps2.y - ps.y, dz = ps2.z - ps.z;
			UpdatePos(ps, dx, dy, dz, false, pid, pos, dcell, code);
			velrhop[pid].x = float(dx / dt);  velrhop[pid].y = float(dy / dt);  velrhop[pid].z = float(dz / dt);
		}
	}
}

//==============================================================================
/// Process movement of boundary particles.
/// Procesa movimiento de boundary particles.
//==============================================================================
void JSphSolidCpu::RunMotion(double stepdt) {
	const char met[] = "RunMotion";
	TmcStart(Timers, TMC_SuMotion);
	const bool motsim = true;
	const JSphMotion::TpMotionMode mode = (motsim ? JSphMotion::MOMT_Simple : JSphMotion::MOMT_Ace2dt);
	BoundChanged = false;
	if (Motion->ProcesTime(mode, TimeStep + MotionTimeMod, stepdt)) {
		CalcRidp(PeriActive != 0, Npb, 0, CaseNfixed, CaseNfixed + CaseNmoving, Codec, Idpc, RidpMove);
		BoundChanged = true;
		bool typesimple;
		tdouble3 simplemov, simplevel, simpleace;
		tmatrix4d matmov, matmov2;
		for (unsigned ref = 0; ref<MotionObjCount; ref++)if (Motion->ProcesTimeGetData(ref, typesimple, simplemov, simplevel, simpleace, matmov, matmov2)) {
			const unsigned pini = MotionObjBegin[ref] - CaseNfixed, np = MotionObjBegin[ref + 1] - MotionObjBegin[ref];
			if (typesimple) {//-Simple movement. | Movimiento simple.
				if (Simulate2D)simplemov.y = simplevel.y = simpleace.y = 0;
				if (motsim)MoveLinBound(np, pini, simplemov, ToTFloat3(simplevel), RidpMove, Posc, Dcellc, Velrhopc, Codec);
				//else    MoveLinBoundAce(np,pini,simplemov,ToTFloat3(simplevel),ToTFloat3(simpleace),RidpMove,Posc,Dcellc,Velrhopc,Acec,Codec);
			}
			else {//-Movement using a matrix. | Movimiento con matriz.
				if (motsim)MoveMatBound(np, pini, matmov, stepdt, RidpMove, Posc, Dcellc, Velrhopc, Codec);
				//else    MoveMatBoundAce(np,pini,matmov,matmov2,stepdt,RidpMove,Posc,Dcellc,Velrhopc,Acec,Codec);
			}
		}
	}
	//-Process other modes of motion. | Procesa otros modos de motion.
	if (WaveGen) {
		if (!BoundChanged)CalcRidp(PeriActive != 0, Npb, 0, CaseNfixed, CaseNfixed + CaseNmoving, Codec, Idpc, RidpMove);
		BoundChanged = true;
		//-Control of wave generation (WaveGen). | Gestion de WaveGen.
		if (WaveGen)for (unsigned c = 0; c<WaveGen->GetCount(); c++) {
			bool typesimple;
			tdouble3 simplemov, simplevel, simpleace;
			tmatrix4d matmov, matmov2;
			unsigned nparts, idbegin;
			//-Get movement data.
			const bool svdata = (TimeStep + stepdt >= TimePartNext);
			if (motsim)typesimple = WaveGen->GetMotion(svdata, c, TimeStep + MotionTimeMod, stepdt, simplemov, simplevel, matmov, nparts, idbegin);
			else      typesimple = WaveGen->GetMotionAce(svdata, c, TimeStep + MotionTimeMod, stepdt, simplemov, simplevel, simpleace, matmov, matmov2, nparts, idbegin);
			//-Applies movement to paddle particles.
			const unsigned np = nparts, pini = idbegin - CaseNfixed;
			if (typesimple) {//-Simple movement. | Movimiento simple.
				if (Simulate2D)simplemov.y = simplevel.y = simpleace.y = 0;
				if (motsim)MoveLinBound(np, pini, simplemov, ToTFloat3(simplevel), RidpMove, Posc, Dcellc, Velrhopc, Codec);
				//else    MoveLinBoundAce(np,pini,simplemov,ToTFloat3(simplevel),ToTFloat3(simpleace),RidpMove,Posc,Dcellc,Velrhopc,Acec,Codec);
			}
			else {
				if (motsim)MoveMatBound(np, pini, matmov, stepdt, RidpMove, Posc, Dcellc, Velrhopc, Codec);
				//else    MoveMatBoundAce(np,pini,matmov,matmov2,stepdt,RidpMove,Posc,Dcellc,Velrhopc,Acec,Codec);
			}
		}
	}
	TmcStop(Timers, TMC_SuMotion);
}

//==============================================================================
/// Applies Damping to selected particles.
/// Aplica Damping a las particulas indicadas.
//==============================================================================
void JSphSolidCpu::RunDamping(double dt, unsigned np, unsigned npb, const tdouble3 *pos, const typecode *code, tfloat4 *velrhop)const {
	if (CaseNfloat || PeriActive)Damping->ComputeDamping(TimeStep, dt, np - npb, npb, pos, code, velrhop);
	else Damping->ComputeDamping(TimeStep, dt, np - npb, npb, pos, NULL, velrhop);
}

//============================================================================== 
/// Adjust variables of floating body particles.
/// Ajusta variables de particulas floating body.
//==============================================================================
void JSphSolidCpu::InitFloating() {
	if (PartBegin) {
		JPartFloatBi4Load ftdata;
		ftdata.LoadFile(PartBeginDir);
		//-Check cases of constant values. | Comprueba coincidencia de datos constantes.
		for (unsigned cf = 0; cf<FtCount; cf++)ftdata.CheckHeadData(cf, FtObjs[cf].mkbound, FtObjs[cf].begin, FtObjs[cf].count, FtObjs[cf].mass);
		//-Load PART data. | Carga datos de PART.
		ftdata.LoadPart(PartBegin);
		for (unsigned cf = 0; cf<FtCount; cf++) {
			FtObjs[cf].center = OrderCodeValue(CellOrder, ftdata.GetPartCenter(cf));
			FtObjs[cf].fvel = OrderCodeValue(CellOrder, ftdata.GetPartFvel(cf));
			FtObjs[cf].fomega = OrderCodeValue(CellOrder, ftdata.GetPartFomega(cf));
			FtObjs[cf].radius = ftdata.GetHeadRadius(cf);
		}
		DemDtForce = ftdata.GetPartDemDtForce();
	}
}

//============================================================================== 
/// Show active timers.
/// Muestra los temporizadores activos.
//==============================================================================
void JSphSolidCpu::ShowTimers(bool onlyfile) {
	JLog2::TpMode_Out mode = (onlyfile ? JLog2::Out_File : JLog2::Out_ScrFile);
	Log->Print("[CPU Timers]", mode);
	if (!SvTimers)Log->Print("none", mode);
	else for (unsigned c = 0; c<TimerGetCount(); c++)if (TimerIsActive(c))Log->Print(TimerToText(c), mode);
}

//============================================================================== 
/// Return string with names and values of active timers.
/// Devuelve string con nombres y valores de los timers activos.
//==============================================================================
void JSphSolidCpu::GetTimersInfo(std::string &hinfo, std::string &dinfo)const {
	for (unsigned c = 0; c<TimerGetCount(); c++)if (TimerIsActive(c)) {
		hinfo = hinfo + ";" + TimerGetName(c);
		dinfo = dinfo + ";" + fun::FloatStr(TimerGetValue(c) / 1000.f);
	}
}


