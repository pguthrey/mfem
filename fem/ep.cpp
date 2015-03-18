#include "ep.hpp"

namespace mfem {

using namespace std;

MyHypreParVector::MyHypreParVector(MPI_Comm comm, int glob_size, int *col)
  : HypreParVector(comm,glob_size,col),
    comm_(comm)
{;}

MyHypreParVector::MyHypreParVector(ParFiniteElementSpace *pfes)
  : HypreParVector(pfes)
{
  comm_ = pfes->GetComm();
}

double
MyHypreParVector::Norml2()
{
  double loc_nrm = this->Vector::Norml2();
  double loc_nrm2 = loc_nrm*loc_nrm;
  double glb_nrm2 = 0.0;

  MPI_Allreduce(&loc_nrm2,&glb_nrm2,1,MPI_DOUBLE,MPI_SUM,comm_);

  return( sqrt(glb_nrm2) );
}

double
MyHypreParVector::Normlinf()
{
  double loc_nrm = this->Vector::Normlinf();
  double glb_nrm = 0.0;

  MPI_Allreduce(&loc_nrm,&glb_nrm,1,MPI_DOUBLE,MPI_MAX,comm_);

  return( glb_nrm );
}

EPDoFs::EPDoFs(FiniteElementSpace & fes)
  : fes_(&fes), nExposedDofs_(0), nPrivateDofs_(0),
    expDoFsByElem_(NULL), priOffset_(NULL)
{
  Mesh * mesh = fes_->GetMesh();
  const FiniteElementCollection * fec = fes_->FEColl();

  for (int i=0; i<fes_->GetNE(); i++)
    nPrivateDofs_ += fec->DofForGeometry(mesh->GetElementBaseGeometry(i));

  nExposedDofs_ = fes_->GetNDofs() - nPrivateDofs_;
}

EPDoFs::~EPDoFs()
{
  if ( expDoFsByElem_ != NULL ) delete expDoFsByElem_;
  if ( priOffset_     != NULL ) delete [] priOffset_;
}

void
EPDoFs::BuildElementToDofTable()
{
  if (expDoFsByElem_ != NULL )
    return;

  Mesh * mesh = fes_->GetMesh();

  Table *el_dof = new Table;
  Array<int> dofs;
  el_dof -> MakeI (mesh -> GetNE());
  for (int i = 0; i < mesh -> GetNE(); i++)
    {
      GetElementDofs (i, dofs);
      el_dof -> AddColumnsInRow (i, dofs.Size());
    }
  el_dof -> MakeJ();
  for (int i = 0; i < mesh -> GetNE(); i++)
    {
      GetElementDofs (i, dofs);
      el_dof -> AddConnections (i, (int *)dofs, dofs.Size());
    }
  el_dof -> ShiftUpI();
  expDoFsByElem_ = el_dof;
}

void
EPDoFs::GetElementDofs(const int elem,
		       Array<int> & ExpDoFs)
{
  if ( expDoFsByElem_ != NULL ) {

    expDoFsByElem_->GetRow(elem,ExpDoFs);

  } else {

    Mesh * mesh = fes_->GetMesh();
    const FiniteElementCollection * fec = fes_->FEColl();

    int nvdofs = mesh->GetNV() * fec->DofForGeometry(Geometry::POINT);

    int nedofs = 0;
    if ( mesh->Dimension() > 1 )
      nedofs = mesh->GetNEdges() * fec->DofForGeometry(Geometry::SEGMENT);

    Array<int> V, E, Eo, F, Fo;
    int k, j, nv, ne, nf, nd;
    int *ind, dim;

    dim = mesh->Dimension();
    nv = fec->DofForGeometry(Geometry::POINT);
    ne = (dim > 1) ? ( fec->DofForGeometry(Geometry::SEGMENT) ) : ( 0 );
    if (nv > 0)
      mesh->GetElementVertices(elem, V);
    if (ne > 0)
      mesh->GetElementEdges(elem, E, Eo);
    nf = 0;
    if (dim == 3)
      if (fec->HasFaceDofs(mesh->GetElementBaseGeometry(elem)))
	{
	  mesh->GetElementFaces(elem, F, Fo);
	  nf = fec->DofForGeometry(mesh->GetFaceBaseGeometry(F[0]));
	}
    nd = V.Size() * nv + E.Size() * ne + F.Size() * nf;
    ExpDoFs.SetSize(nd);
    if (nv > 0)
      {
	for (k = 0; k < V.Size(); k++)
	  for (j = 0; j < nv; j++)
	    ExpDoFs[k*nv+j] = V[k]*nv+j;
	nv *= V.Size();
      }
    if (ne > 0)
      // if (dim > 1)
      for (k = 0; k < E.Size(); k++)
	{
	  ind = fec->DofOrderForOrientation(Geometry::SEGMENT, Eo[k]);
	  for (j = 0; j < ne; j++)
	    if (ind[j] < 0)
	      ExpDoFs[nv+k*ne+j] = -1 - ( nvdofs+E[k]*ne+(-1-ind[j]) );
	    else
	      ExpDoFs[nv+k*ne+j] = nvdofs+E[k]*ne+ind[j];
	}
    ne = nv + ne * E.Size();
    if (nf > 0)
      // if (dim == 3)
      {
	for (k = 0; k < F.Size(); k++)
	  {
	    ind = fec->DofOrderForOrientation(mesh->GetFaceBaseGeometry(F[k]),
					      Fo[k]);
	    for (j = 0; j < nf; j++)
	      {
		if (ind[j] < 0)
		  ExpDoFs[ne+k*nf+j] = -1 - ( nvdofs+nedofs+F[k]*nf+(-1-ind[j]) );
		else
		  ExpDoFs[ne+k*nf+j] = nvdofs+nedofs+F[k]*nf+ind[j];
	      }
	  }
      }	
  }
}

void
EPDoFs::GetElementDofs(const int elem,
		       Array<int> & ExpDoFs,
		       int & PriOffset, int & numPri)
{

  this->GetElementDofs(elem,ExpDoFs);

  if ( priOffset_ == NULL ) {

    Mesh * mesh = fes_->GetMesh();
    const FiniteElementCollection * fec = fes_->FEColl();

    priOffset_ = new int[fes_->GetNE()+1]; //assert( priOffset_ != NULL );
    priOffset_[0] = 0;
    for (int i=0; i<fes_->GetNE(); i++)
      priOffset_[i+1] = priOffset_[i] + 
	fec->DofForGeometry(mesh->GetElementBaseGeometry(i));      

  }
  PriOffset = priOffset_[elem];
  numPri    = priOffset_[elem+1]-PriOffset;
}

ParEPDoFs::ParEPDoFs(ParFiniteElementSpace & pfes)
  : EPDoFs(pfes), pfes_(&pfes), Pe_(NULL),
    nParExposedDofs_(-1),
    ExposedPart_(NULL) , TExposedPart_(NULL) 
{
  MPI_Comm comm = pfes_->GetComm();
  int numProcs  = pfes_->GetNRanks();
  int nExposed  = this->GetNExposedDofs();

  int myRank = -1;
  MPI_Comm_rank(comm,&myRank);

  ExposedPart_  = new int[numProcs+1];
  TExposedPart_ = new int[numProcs+1];

  ExposedPart_[0]  = 0;
  TExposedPart_[0] = 0;

  MPI_Allgather(&nExposed,1,MPI_INT,&ExposedPart_[1],1,MPI_INT,comm);

  for (int p=1; p<=numProcs; p++) {
    ExposedPart_[p] += ExposedPart_[p-1];
  }

  HypreParMatrix * P = pfes_->Dof_TrueDof_Matrix();

  int * DoFPart  = P->RowPart();
  int * TDoFPart = P->ColPart();
  int * nPri     = new int[numProcs];

  TExposedPart_[0] = 0;

  for (int p=0; p<numProcs; p++) {
    nPri[p] = (DoFPart[p+1] - DoFPart[p]) - 
      (ExposedPart_[p+1] - ExposedPart_[p]);

    TExposedPart_[p+1] = TExposedPart_[p] +
      (TDoFPart[p+1] - TDoFPart[p]) - nPri[p];
  }

  nParExposedDofs_ = TExposedPart_[myRank+1]-TExposedPart_[myRank];

  hypre_CSRMatrix * csr_P = hypre_MergeDiagAndOffd((hypre_ParCSRMatrix*)*P);

  int csr_P_nnz = hypre_CSRMatrixNumNonzeros(csr_P);

  for (int j=0; j<csr_P_nnz; j++) {
    int r = 0;
    while ( hypre_CSRMatrixJ(csr_P)[j] >= TDoFPart[r+1] ) r++;

    for (int p=0; p<r; p++)
      hypre_CSRMatrixJ(csr_P)[j] -= nPri[p];
  }

  Pe_ = new HypreParMatrix(comm,
			   ExposedPart_[myRank+1]-ExposedPart_[myRank],
			   ExposedPart_[numProcs],
			   TExposedPart_[numProcs],
			   hypre_CSRMatrixI(csr_P),
			   hypre_CSRMatrixJ(csr_P),
			   hypre_CSRMatrixData(csr_P),
			   ExposedPart_,
			   TExposedPart_);

  hypre_CSRMatrixDestroy(csr_P);

  delete [] nPri;
}

ParEPDoFs::~ParEPDoFs()
{
  if ( Pe_           != NULL ) delete    Pe_;
  if ( ExposedPart_  != NULL ) delete [] ExposedPart_;
  if ( TExposedPart_ != NULL ) delete [] TExposedPart_;
}

EPField::EPField(ParEPDoFs & epdofs)
  : numFields_(0),
    epdofs_(&epdofs),
    ExposedDoFs_(NULL),
    PrivateDoFs_(NULL)
{}

EPField::~EPField()
{
  if ( ExposedDoFs_ != NULL ) {
    for (unsigned int i=0; i<numFields_; i++)
      if ( ExposedDoFs_[i] != NULL ) delete ExposedDoFs_[i];
    delete [] ExposedDoFs_;
  }
  if ( PrivateDoFs_ != NULL ) {
    for (unsigned int i=0; i<numFields_; i++)
      if ( PrivateDoFs_[i]  != NULL ) delete PrivateDoFs_[i];
    delete [] PrivateDoFs_;
  }
}

void
EPField::initVectors(const unsigned int num)
{
  numFields_ = num;

  ExposedDoFs_ = new Vector*[num];
  PrivateDoFs_ = new Vector*[num];

  for (unsigned int i=0; i<num; i++) {
    ExposedDoFs_[i] = new Vector(epdofs_->GetNExposedDofs());
    PrivateDoFs_[i] = new Vector(epdofs_->GetNPrivateDofs());
  }
}

double
EPField::Norml2()
{
  double norm = 0.0;
  double  tmp = -1.0;

  for (unsigned int i=0; i<numFields_; i++) {
    tmp = ExposedDoFs_[i]->Norml2();
    norm += tmp*tmp;

    tmp = PrivateDoFs_[i]->Norml2();
    norm += tmp*tmp;
  }
  return( sqrt(norm) );
}

EPField &
EPField::operator-=(const EPField &v)
{
  // assert( numFields_ == v.GetNFields() );

  for (unsigned int i=0; i<numFields_; i++) {
    (*ExposedDoFs_[i]) -= *v.ExposedDoFs(i);
    (*PrivateDoFs_[i]) -= *v.PrivateDoFs(i);
  }

  return *this;
}

void
EPField::initFromInterleavedVector(const Vector & x)
{
  this->initVectors(1);

  FiniteElementSpace * fes = epdofs_->FESpace();

  Vector * xE = this->ExposedDoFs(0);
  Vector * xP = this->PrivateDoFs(0);

  Array<int> allDoFs;
  Array<int> priDoFs;

  Array<int> expDoFs;
  int priOffset = 0;
  int nPri = 0;

  for (int i=0; i<fes->GetNE(); i++) {
    fes->GetElementDofs(i,allDoFs);
    fes->GetElementInteriorDofs(i,priDoFs);

    // int nPriDoFs = priDoFs.Size();

    epdofs_->GetElementDofs(i,expDoFs,priOffset,nPri);

    // assert( nPriDoFs == nPri);

    for (int j=0; j<expDoFs.Size(); j++) {
      if ( allDoFs[j] >= 0 ) {
	(*xE)[expDoFs[j]] = x[allDoFs[j]];
      } else
	(*xE)[1-expDoFs[j]] = x[1-allDoFs[j]];
    }

    for (int j=0; j<nPri; j++) {
      (*xP)[priOffset+j] = x[priDoFs[j]];
    }
  }
}

const Vector *
EPField::ExposedDoFs(const unsigned int i) const
{
  if ( i >= numFields_ ) {
    std::cerr << "EPVector::ExposedDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  if ( ExposedDoFs_ == NULL ) {
    std::cerr << "EPVector::ExposedDoFs() const - "
	      <<"Internal Vector is NULL."
	      << std::endl;
    return NULL;
  }
  return(ExposedDoFs_[i]);
}

Vector *
EPField::ExposedDoFs(const unsigned int i)
{
  if ( ExposedDoFs_ == NULL ) this->initVectors();
  if ( i >= numFields_ ) {
    std::cerr << "EPVector::ExposedDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  return(ExposedDoFs_[i]);
}

const Vector *
EPField::PrivateDoFs(const unsigned int i) const
{
  if ( i >= numFields_ ) {
    std::cerr << "EPVector::PrivateDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  if ( PrivateDoFs_ == NULL ) {
    std::cerr << "EPVector::PrivateDoFs() const - "
	      <<"Internal Vector is NULL."
	      << std::endl;
    return NULL;
  }
  return(PrivateDoFs_[i]);
}

Vector *
EPField::PrivateDoFs(const unsigned int i)
{
  if ( PrivateDoFs_ == NULL ) this->initVectors();
  if ( i >= numFields_ ) {
    std::cerr << "EPVector::PrivateDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  return(PrivateDoFs_[i]);
}

ParEPField::ParEPField(ParEPDoFs & pepdofs)
  : EPField(pepdofs),
    pepdofs_(&pepdofs),
    ParExposedDoFs_(NULL)
{}

ParEPField::~ParEPField()
{
  if ( ParExposedDoFs_ != NULL ) {
    for (unsigned int i=0; i<numFields_; i++)
      if ( ParExposedDoFs_[i] != NULL ) delete ParExposedDoFs_[i];
    delete [] ParExposedDoFs_;
  }
}

void
ParEPField::initVectors(const unsigned int num)
{
  ParExposedDoFs_ = new MyHypreParVector*[num];

  MPI_Comm comm = pepdofs_->GetComm();
  int numProcs  = pepdofs_->GetNRanks();
  int * part    = pepdofs_->GetTPartitioning();

  for (unsigned int i=0; i<num; i++) {
    ParExposedDoFs_[i] = new MyHypreParVector(comm,part[numProcs],part);
  }
}

void
ParEPField::updateParExposedDoFs()
{
  HypreParMatrix * Pe = pepdofs_->EDof_TrueEDof_Matrix();
  // assert( Pe != NULL );

  for (unsigned int i=0; i<numFields_; i++) {
    // Sums together contributions to shared DoFs from all ranks
    Pe->MultTranspose(*this->ExposedDoFs(i),*this->ParExposedDoFs(i));

    // Redistributes the summed DoFs to all ranks
    Pe->Mult(*this->ParExposedDoFs(i),*this->ExposedDoFs(i));
  }
}

void
ParEPField::updateExposedDoFs()
{
  HypreParMatrix * Pe = pepdofs_->EDof_TrueEDof_Matrix();
  // assert( Pe != NULL );

  for (unsigned int i=0; i<numFields_; i++) {
    // Redistributes the summed DoFs to all ranks
    Pe->Mult(*this->ParExposedDoFs(i),*this->ExposedDoFs(i));
  }

}

double
ParEPField::Norml2()
{
  double norm = 0.0;
  double  tmp = 0.0;

  for (unsigned int i=0; i<numFields_; i++) {
    tmp = this->PrivateDoFs(i)->Norml2();
    norm += tmp*tmp;
  }

  MPI_Allreduce(&norm,&tmp,1,MPI_DOUBLE,MPI_SUM,pepdofs_->GetComm());
  norm = tmp;

  for (unsigned int i=0; i<numFields_; i++) {
    tmp = ParExposedDoFs_[i]->Norml2();
    norm += tmp*tmp;
  }
  norm = sqrt(norm);

  return( norm );
}

double
ParEPField::Normlinf() {

  double loc_norm = 0.0;
  double norm     = 0.0;
  double tmp      = 0.0;

  for (unsigned int i=0; i<numFields_; i++) {
    tmp      = this->PrivateDoFs(i)->Normlinf();
    loc_norm = (tmp>loc_norm)?tmp:loc_norm;
  }

  MPI_Allreduce(&loc_norm,&tmp,1,MPI_DOUBLE,MPI_MAX,pepdofs_->GetComm());
  norm = tmp;

  for (unsigned int i=0; i<numFields_; i++) {
    tmp = ParExposedDoFs_[i]->Normlinf();
    norm = (tmp>norm)?tmp:norm;
  }

  return( norm );
}

ParEPField &
ParEPField::operator-=(const ParEPField &v)
{
  // assert( numFields_ == v.GetNFields() );

  for (unsigned int i=0; i<numFields_; i++)
    (*ParExposedDoFs_[i]) -= *v.ParExposedDoFs(i);

  this->EPField::operator-=(v);

  return *this;
}

void
ParEPField::initFromInterleavedVector(const HypreParVector & x)
{
  ParFiniteElementSpace * pfes = pepdofs_->PFESpace();
  HypreParMatrix * P  = pfes->Dof_TrueDof_Matrix();

  HypreParVector Px(P->GetComm(),P->M(),P->RowPart());

  P->Mult(x,Px,1.0,0.0);

  this->EPField::initFromInterleavedVector(Px);

  int xSize = x.Size();
  int nPri  = pepdofs_->GetNPrivateDofs();

  MyHypreParVector * xE = this->ParExposedDoFs(0);

  for (int j=0; j<xSize-nPri; j++)
    (*xE)(j) = x(j);
}

const MyHypreParVector *
ParEPField::ParExposedDoFs(const unsigned int i) const
{
  if ( i >= numFields_ ) {
    std::cerr << "ParEPVector::ParExposedDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  if ( ParExposedDoFs_ == NULL ) {
    std::cerr << "EPVector::ParExposedDoFs() const - "
	      <<"Internal Vector is NULL."
	      << std::endl;
    return NULL;
  }
  return(ParExposedDoFs_[i]);
}

MyHypreParVector *
ParEPField::ParExposedDoFs(const unsigned int i)
{
  if ( ParExposedDoFs_ == NULL ) this->initVectors();
  if ( i >= numFields_ ) {
    std::cerr << "ParEPVector::ParExposedDoFs() const - "
	      << "Invalid Field index requested."
	      << std::endl;
    return NULL;
  }
  return(ParExposedDoFs_[i]);
}

BlockDiagonalMatrix::BlockDiagonalMatrix(const int nBlocks,
					 const int * blockOffsets)
  : Matrix(blockOffsets_[nBlocks]),
    blocks_(NULL),
    blockOffsets_(blockOffsets),
    nBlocks_(nBlocks)
{
  blocks_ = new DenseMatrix*[nBlocks_];

  for (int i=0; i<nBlocks_; i++) {
    blocks_[i] = new DenseMatrix(blockOffsets_[i+1]-blockOffsets_[i]);
  }
}

BlockDiagonalMatrix::~BlockDiagonalMatrix()
{
  if ( blocks_ != NULL ) {
    for (int i=0; i<nBlocks_; i++) {
      delete blocks_[i];
    }
    delete [] blocks_;
  }
}

EPMatrix::EPMatrix(EPDoFs & epdofsL, EPDoFs & epdofsR,
		   BilinearFormIntegrator & bfi): 
  epdofsL_(&epdofsL),
  epdofsR_(&epdofsR),
  bfi_(&bfi),
  Mee_(NULL),
  Mep_(NULL),
  Mpe_(NULL),
  Mrr_(NULL),
  Mpp_(NULL),
  MppInv_(NULL),
  reducedRHS_(NULL),
  vecp_(NULL)
{}

EPMatrix::~EPMatrix()
{
  if ( Mee_        != NULL ) delete Mee_;
  if ( Mep_        != NULL ) delete Mep_;
  if ( Mpe_        != NULL ) delete Mpe_;
  if ( Mrr_        != NULL ) delete Mrr_;

  for (int i=0; i<epdofsR_->GetNElements(); i++) {
    if ( Mpp_[i]    != NULL ) delete Mpp_[i];
    if ( MppInv_[i] != NULL ) delete MppInv_[i];
  }

  if ( Mpp_        != NULL ) delete [] Mpp_;
  if ( MppInv_     != NULL ) delete [] MppInv_;

  if ( reducedRHS_ != NULL ) delete reducedRHS_;
  if ( vecp_       != NULL ) delete vecp_;
}

void
EPMatrix::Assemble()
{
  if ( epdofsL_ == epdofsR_ ) {
    reducedRHS_ = new Vector(epdofsR_->GetNExposedDofs());
    vecp_       = new Vector(epdofsR_->GetNPrivateDofs());
  }

  Mee_ = new SparseMatrix(epdofsL_->GetNExposedDofs(),
			  epdofsR_->GetNExposedDofs());
  Mep_ = new SparseMatrix(epdofsL_->GetNExposedDofs(),
			  epdofsR_->GetNPrivateDofs());
  Mrr_ = new SparseMatrix(epdofsL_->GetNExposedDofs(),
			  epdofsR_->GetNExposedDofs());
  if ( epdofsL_ != epdofsR_ ) {
    Mpe_ = new SparseMatrix(epdofsL_->GetNPrivateDofs(),
			    epdofsR_->GetNExposedDofs());
  }

  Mpp_    = new DenseMatrix*[epdofsR_->GetNElements()];

  if ( epdofsL_ == epdofsR_ )
    MppInv_ = new DenseMatrixInverse*[epdofsR_->GetNElements()];

  Array<int> expDoFsL;
  Array<int> expDoFsR;
  int priOffsetL = 0;
  int priOffsetR = 0;
  int nPriL = 0;
  int nPriR = 0;

  FiniteElementSpace * fes = epdofsR_->FESpace();
  Vector vpR,veL,vcMpe;
  DenseMatrix elmat;
  DenseMatrix mee,mpe,mep,mrr;

  for (int i=0; i<epdofsR_->GetNElements(); i++) {

    const FiniteElement &fe = *fes->GetFE(i);
    ElementTransformation *eltrans = fes->GetElementTransformation(i);
    bfi_->AssembleElementMatrix(fe, *eltrans, elmat);

    epdofsL_->GetElementDofs(i,expDoFsL,priOffsetL,nPriL);
    epdofsR_->GetElementDofs(i,expDoFsR,priOffsetR,nPriR);

    mee.CopyMN(elmat,expDoFsL.Size(),expDoFsR.Size(),0,0);

    Mee_->AddSubMatrix(expDoFsL,expDoFsR,mee);

    mep.CopyMN(elmat,expDoFsL.Size(),nPriR,0,expDoFsR.Size());

    for (int ii=0; ii<expDoFsL.Size(); ii++)
      for (int jj=0; jj<nPriR; jj++)
	Mep_->Add(expDoFsL[ii],priOffsetR+jj,mep(ii,jj));

    if ( Mpe_ != NULL ) {
      mpe.CopyMN(elmat,nPriL,expDoFsR.Size(),expDoFsL.Size(),0);

      for (int ii=0; ii<nPriL; ii++)
	for (int jj=0; jj<expDoFsR.Size(); jj++)
	  Mpe_->Add(priOffsetR+jj,expDoFsR[ii],mpe(ii,jj));
    }

    Mpp_[i] = new DenseMatrix(nPriL,nPriR);
    Mpp_[i]->CopyMN(elmat,nPriL,nPriR,expDoFsL.Size(),expDoFsR.Size());

    if ( MppInv_ != NULL ) {
      MppInv_[i] = (DenseMatrixInverse*)Mpp_[i]->Inverse();

      vpR.SetSize(nPriR);
      veL.SetSize(expDoFsL.Size());
      mrr.SetSize(expDoFsL.Size(),expDoFsR.Size());

      for (int jj=0; jj<expDoFsR.Size(); jj++) {
	double * colMpe = &mep.Data()[jj*nPriR];

	vcMpe.SetDataAndSize(colMpe,MppInv_[i]->Size());
	MppInv_[i]->Mult(vcMpe,vpR);

	mep.Mult(vpR,veL);

	for (int ii=0; ii<expDoFsL.Size(); ii++)
	  mrr(ii,jj) = -veL(ii);
      }
    }

    mrr += mee;
    Mrr_->AddSubMatrix(expDoFsL,expDoFsR,mrr);
  }
  Mee_->Finalize();
  Mep_->Finalize();
  Mrr_->Finalize();
  if ( Mpe_ != NULL ) Mpe_->Finalize();
}

void
EPMatrix::Mult(const EPField & x, EPField & y) const
{
  Mee_->Mult(*x.ExposedDoFs(0),*y.ExposedDoFs(0));
  Mep_->AddMult(*x.PrivateDoFs(0),*y.ExposedDoFs(0));

  int nElems = epdofsR_->GetNElements();

  const int * priOffR = epdofsR_->GetPrivateOffsets();
  const int * priOffL = epdofsL_->GetPrivateOffsets();

  for (int i=0; i<nElems; i++)
    Mpp_[i]->Mult(&x.PrivateDoFs(0)->GetData()[priOffL[i]],
		  &y.PrivateDoFs(0)->GetData()[priOffR[i]]);

  if ( Mpe_ != NULL )
    Mpe_->AddMult(*x.ExposedDoFs(0),*y.PrivateDoFs(0));
  else
    Mep_->AddMultTranspose(*x.ExposedDoFs(0),*y.PrivateDoFs(0));
}

void
EPMatrix::Mult(const Vector & x, Vector & y) const
{
  assert(false);
}

const Vector *
EPMatrix::ReducedRHS(const EPField & x) const
{
  int nElems = epdofsR_->GetNElements();

  const int * priOffR = epdofsR_->GetPrivateOffsets();
  const int * priOffL = epdofsL_->GetPrivateOffsets();

  Vector v1,v2;

  for (int i=0; i<nElems; i++) {
    int size = MppInv_[i]->Size();
    v1.SetDataAndSize(&x.PrivateDoFs(0)->GetData()[priOffL[i]],size);
    v2.SetDataAndSize(&vecp_->GetData()[priOffR[i]],size);
    MppInv_[i]->Mult(v1,v2);
  }

  reducedRHS_->Set(1.0,*x.ExposedDoFs());

  Mep_->AddMult(*vecp_,*reducedRHS_,-1.0);

  return( reducedRHS_ );
}

void
EPMatrix::SolvePrivateDoFs(const Vector & x, EPField & y) const
{
  vecp_->Set(1.0,x);
  if ( Mpe_ != NULL )
    Mpe_->AddMult(*y.ExposedDoFs(),*vecp_,-1.0);
  else
    Mep_->AddMultTranspose(*y.ExposedDoFs(),*vecp_,-1.0);

  int nElems = epdofsR_->GetNElements();

  const int * priOffR = epdofsR_->GetPrivateOffsets();
  const int * priOffL = epdofsL_->GetPrivateOffsets();

  Vector v1,v2;

  for (int i=0; i<nElems; i++) {
    int size = MppInv_[i]->Size();
    v1.SetDataAndSize(&vecp_->GetData()[priOffL[i]],size);
    v2.SetDataAndSize(&y.PrivateDoFs(0)->GetData()[priOffR[i]],size);

    MppInv_[i]->Mult(v1,v2);
  }
}

ParEPMatrix::ParEPMatrix(ParEPDoFs & pepdofsL, ParEPDoFs & pepdofsR,
			 BilinearFormIntegrator & bfi)
  : EPMatrix(pepdofsL,pepdofsR,bfi),
    pepdofsL_(&pepdofsL),
    pepdofsR_(&pepdofsR),
    preducedRHS_(NULL),
    vec_(NULL),
    vecp_(NULL)
{}

ParEPMatrix::~ParEPMatrix()
{
  if ( preducedOp_  != NULL ) delete preducedOp_;
  if ( preducedRHS_ != NULL ) delete preducedRHS_;
  if ( vec_         != NULL ) delete vec_;
  if ( vecp_        != NULL ) delete vecp_;
}

void
ParEPMatrix::Assemble()
{
  this->EPMatrix::Assemble();

  if ( pepdofsL_ == pepdofsR_ ) {

    MPI_Comm comm = pepdofsR_->GetComm();
    int numProcs  = pepdofsR_->GetNRanks();
    int * part    = pepdofsR_->GetTPartitioning();
    preducedOp_   = new ParReducedOp(pepdofsR_,
				     this->EPMatrix::GetMrr());
    preducedRHS_  = new HypreParVector(comm,part[numProcs],part);
    vec_          = new Vector(pepdofsR_->GetNExposedDofs());
    vecp_         = new Vector(pepdofsR_->GetNPrivateDofs());
  }
}

void
ParEPMatrix::Mult(const ParEPField & x, ParEPField & y) const
{
  this->EPMatrix::Mult(x,y);
  y.updateParExposedDoFs();
}

const Operator *
ParEPMatrix::ReducedOperator() const
{
  if ( preducedOp_ == NULL)
    std::cout << "Oops!  The Reduced Operator is NULL!" << std::endl;
  return( preducedOp_ );
}

const HypreParVector *
ParEPMatrix::ReducedRHS(const ParEPField & x) const
{
  int nElems = pepdofsR_->GetNElements();

  const int * priOffR = pepdofsR_->GetPrivateOffsets();
  const int * priOffL = pepdofsL_->GetPrivateOffsets();

  DenseMatrixInverse ** MppInv = this->GetMppInv();

  Vector v1,v2;

  for (int i=0; i<nElems; i++) {
    int size = MppInv[i]->Size();
    v1.SetDataAndSize(&x.PrivateDoFs(0)->GetData()[priOffL[i]],size);
    v2.SetDataAndSize(&vecp_->GetData()[priOffR[i]],size);
    MppInv[i]->Mult(v1,v2);
  }
  this->GetMep()->Mult(*vecp_,*vec_);

  pepdofsR_->EDof_TrueEDof_Matrix()->MultTranspose(*vec_,
						   *preducedRHS_);
  (*preducedRHS_) *= -1.0;
  (*preducedRHS_) += *x.ParExposedDoFs();

  return( preducedRHS_ );
}

}
