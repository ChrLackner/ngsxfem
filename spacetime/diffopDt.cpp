#define FILE_DIFFOPDT_CPP
#include "diffopDt.hpp"
#include <diffop_impl.hpp>
#include "SpaceTimeFE.hpp"


namespace ngfem
{

  template <typename FEL, typename MIP, typename MAT>
  void DiffOpDt::GenerateMatrix (const FEL & bfel, const MIP & mip,
                                             MAT & mat, LocalHeap & lh)
  {
      IntegrationPoint ip(mip.IP());
      mat = 0.0;

      const SpaceTimeFE<2>* scafe2 =
              dynamic_cast<const SpaceTimeFE<2> * > (& bfel);
      if(scafe2) {
          FlatVector<> dtshape (scafe2->GetNDof(),lh);
          scafe2->CalcDtShape(ip,dtshape);
          mat.Row(0) = dtshape;
          return;
      }

      const SpaceTimeFE<3>* scafe3 =
              dynamic_cast<const SpaceTimeFE<3> * > (& bfel);
      if(scafe3) {
          FlatVector<> dtshape (scafe3->GetNDof(),lh);
          scafe3->CalcDtShape(ip,dtshape);
          mat.Row(0) = dtshape;
          return;
      }

    }

  template class T_DifferentialOperator<DiffOpDt>;

  template <int D>
  template <typename FEL, typename MIP, typename MAT>
  void DiffOpDtVec<D>::GenerateMatrix (const FEL & bfel, const MIP & mip,
                                             MAT & mat, LocalHeap & lh)
  {
      IntegrationPoint ip(mip.IP());
      mat = 0.0;

      const SpaceTimeFE<2> * scafe2 =
              dynamic_cast<const SpaceTimeFE<2> * > (& bfel);
      if(scafe2){
        FlatVector<> dtshape (scafe2->GetNDof(),lh);
        scafe2->CalcDtShape(ip,dtshape);
        for (int j = 0; j < D; j++)
          for (int k = 0; k < dtshape.Size(); k++)
            mat(j,k*D+j) = dtshape(k);
      }

      const SpaceTimeFE<3> * scafe3 =
              dynamic_cast<const SpaceTimeFE<3> * > (& bfel);
      if(scafe3){
        FlatVector<> dtshape (scafe3->GetNDof(),lh);
        scafe3->CalcDtShape(ip,dtshape);
        for (int j = 0; j < D; j++)
          for (int k = 0; k < dtshape.Size(); k++)
            mat(j,k*D+j) = dtshape(k);
      }

    }

  template class T_DifferentialOperator<DiffOpDtVec<1>>;
  template class T_DifferentialOperator<DiffOpDtVec<2>>;
  template class T_DifferentialOperator<DiffOpDtVec<3>>;

  template void DiffOpDtVec<3>::GenerateMatrix<FiniteElement, MappedIntegrationPoint<2, 2, double>,
     SliceMatrix<double, (ORDERING)0> >(FiniteElement const&, MappedIntegrationPoint<2, 2, double> const&,
                                        SliceMatrix<double, (ORDERING)0>&, LocalHeap&);
  template void DiffOpDtVec<3>::GenerateMatrix<FiniteElement, MappedIntegrationPoint<2, 2, double>,
     SliceMatrix<double, (ORDERING)0> const>(FiniteElement const&, MappedIntegrationPoint<2, 2, double> const&,
                                        SliceMatrix<double, (ORDERING)0> const&, LocalHeap&);

  template <int time>
  template <typename FEL, typename MIP, typename MAT>
  void DiffOpFixt<time>::GenerateMatrix (const FEL & bfel, const MIP & mip,
                                             MAT & mat, LocalHeap & lh)
  {

      IntegrationPoint ip(mip.IP()(0),mip.IP()(1), mip.IP()(2), time);
      ip.SetPrecomputedGeometry(true);
      mat = 0.0;

      const SpaceTimeFE<2> * scafe2 =
              dynamic_cast<const SpaceTimeFE<2> * > (&bfel);
      if(scafe2){
          FlatVector<> shape (scafe2->GetNDof(),lh);
          scafe2->CalcShape(ip,shape);
          mat.Row(0) = shape;
      }

      const SpaceTimeFE<3> * scafe3 =
              dynamic_cast<const SpaceTimeFE<3> * > (&bfel);
      if(scafe3){
          FlatVector<> shape (scafe3->GetNDof(),lh);
          scafe3->CalcShape(ip,shape);
          mat.Row(0) = shape;
      }

   }

  template class T_DifferentialOperator<DiffOpFixt<0>>;
  template class T_DifferentialOperator<DiffOpFixt<1>>;


  void DiffOpFixAnyTime ::
  CalcMatrix (const FiniteElement & bfel,
              const BaseMappedIntegrationPoint & bmip,
              SliceMatrix<double,ColMajor> mat,
              LocalHeap & lh) const
  {

      mat = 0.0;
      const MappedIntegrationPoint<DIM_ELEMENT,DIM_SPACE> & mip =
      static_cast<const MappedIntegrationPoint<DIM_ELEMENT,DIM_SPACE>&> (bmip);

      IntegrationPoint ip(mip.IP()(0),mip.IP()(1),mip.IP()(2), time);
      ip.SetPrecomputedGeometry(true);

      const SpaceTimeFE<2> * scafe2 =
            dynamic_cast<const SpaceTimeFE<2> *> (&bfel);
      if(scafe2){
        FlatVector<> shape (scafe2->GetNDof(),lh);
        scafe2->CalcShape(ip,shape);
        mat.Row(0) = shape;
      }
      const SpaceTimeFE<3> * scafe3 =
            dynamic_cast<const SpaceTimeFE<3> *> (&bfel);
      if(scafe3){
        FlatVector<> shape (scafe3->GetNDof(),lh);
        scafe3->CalcShape(ip,shape);
        mat.Row(0) = shape;
      }
  }

  void DiffOpFixAnyTime ::
  ApplyTrans (const FiniteElement & fel,
              const BaseMappedIntegrationPoint & mip,
              FlatVector<double> flux,
              FlatVector<double> x,
              LocalHeap & lh) const
  {
    HeapReset hr(lh);
    FlatMatrix<double,ColMajor> mat(Dim(), x.Size(), lh);
    CalcMatrix (fel, mip, mat, lh);
    x = Trans(mat) * flux;
  }



}


