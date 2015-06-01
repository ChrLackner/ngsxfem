#include "xstokesNitsche.hpp"

namespace ngfem
{

  template <int D>
  void XStokesNitscheIntegrator<D>::
  CalcElementMatrix (const FiniteElement & base_fel,
		     const ElementTransformation & eltrans, 
		     FlatMatrix<double> elmat,
		     LocalHeap & lh) const
  {
    static Timer timer ("XStokesNitscheIntegrator::CalcElementMatrix");
    RegionTimer reg (timer);

    const CompoundFiniteElement & cfel = 
      dynamic_cast<const CompoundFiniteElement&> (base_fel);

    const CompoundFiniteElement & feuv_comp =
      dynamic_cast<const CompoundFiniteElement&> (cfel[0]);

    const CompoundFiniteElement & fep_comp =
      dynamic_cast<const CompoundFiniteElement&> (cfel[D]);

    const ScalarFiniteElement<D> & feuv =
      dynamic_cast<const ScalarFiniteElement<D>&> (feuv_comp[0]);
    
    const XFiniteElement * feuvx =
      dynamic_cast<const XFiniteElement *> (&feuv_comp[1]);
    const XDummyFE * dummy_feuvx =
      dynamic_cast<const XDummyFE *> (&feuv_comp[1]);

    const ScalarFiniteElement<D> & fep = 
      dynamic_cast<const ScalarFiniteElement<D>&> (fep_comp[0]);

    const XFiniteElement * fepx =
      dynamic_cast<const XFiniteElement *> (&fep_comp[1]);
    const XDummyFE * dummy_fepx =
      dynamic_cast<const XDummyFE *> (&fep_comp[1]);

    elmat = 0.0;

    if(!feuvx)
      return;

    if (!feuvx && !dummy_feuvx) 
      throw Exception(" not containing X-elements (velocity)?");

    if (!fepx && !dummy_fepx) 
      throw Exception(" not containing X-elements (pressure)?");

    int ndofuv_x = feuvx!=NULL ? feuvx->GetNDof() : 0;
    int ndofuv = feuv.GetNDof();
    int ndofuv_total = ndofuv+ndofuv_x;

    shared_ptr<IntRange> dofrangeuv[D];
    shared_ptr<IntRange> dofrangeuv_x[D];
    int cnt = 0;
    for (int d = 0; d < D; ++d)
    {
      dofrangeuv[d] = make_shared<IntRange>(cnt,cnt+ndofuv);
      cnt += ndofuv;
      dofrangeuv_x[d] = make_shared<IntRange>(cnt,cnt+ndofuv_x);
      cnt += ndofuv_x;
    }

    int ndofp = fep.GetNDof();
    int ndofp_x = fepx!=NULL ? fepx->GetNDof() : 0;
    int ndofp_total = ndofp+ndofp_x;

    IntRange dofrangep(cnt, cnt+ndofp);
    cnt+= ndofp;
    IntRange dofrangep_x(cnt, cnt+ndofp_x);

    int ndof_total = ndofp_total + D*ndofuv_total;

    FlatVector<> shapep_total(ndofp_total,lh);
    FlatVector<> shapep(ndofp,&shapep_total(0));
    FlatVector<> shapepx(ndofp,&shapep_total(ndofp));

    FlatVector<> shapeuv_total(ndofuv_total,lh);
    FlatVector<> shapeuv(ndofuv,&shapeuv_total(0));
    FlatVector<> shapeuvx(ndofuv,&shapeuv_total(ndofuv));
    
    int orderuv = feuv.Order();
    int orderp = fep.Order();
    int ps = orderuv;

    Mat<D> mat = 0;

    FlatMatrixFixWidth<D> bmat(ndof_total,lh);
    FlatMatrixFixWidth<D> bmatjump(ndof_total,lh);

    bmat = 0.0;
    bmatjump = 0.0;

    FlatMatrix<> Nc(ndof_total,ndof_total,lh);
    FlatMatrix<> Ns(ndof_total,ndof_total,lh);
    
    Ns = 0.0;
    Nc = 0.0;

    const FlatArray<DOMAIN_TYPE>& xusign = feuvx->GetSignsOfDof();
    const FlatArray<DOMAIN_TYPE>& xpsign = fepx->GetSignsOfDof();
    
    //int p = scafe->Order();

    const FlatXLocalGeometryInformation & xgeom(feuvx->GetFlatLocalGeometry());
    const FlatCompositeQuadratureRule<D> & fcompr(xgeom.GetCompositeRule<D>());
    const FlatQuadratureRuleCoDim1<D> & fquad(fcompr.GetInterfaceRule());

    IntegrationPoint ipc(0.0,0.0,0.0);
    MappedIntegrationPoint<D,D> mipc(ipc, eltrans);

    const double h = D == 2 ? sqrt(mipc.GetMeasure()) : cbrt(mipc.GetMeasure());

    const double a_t_neg = alpha_neg->Evaluate(mipc);
    const double a_t_pos = alpha_pos->Evaluate(mipc);

    double kappa_neg;
    double kappa_pos;

    if (xgeom.kappa[NEG] >= 0.5)
      kappa_neg = 1.0;
    else
      kappa_neg = 0.0;
    kappa_pos = 1.0 - kappa_neg;

    kappa_neg=kappa_pos=0.5;
       
    const double lam = lambda->EvaluateConst();

    //double ava = a_t_pos*0.5+a_t_neg*0.5;

    for (int i = 0; i < fquad.Size(); ++i)
    {
      IntegrationPoint ip(&fquad.points(i,0),0.0);
      MappedIntegrationPoint<D,D> mip(ip, eltrans);
      
      Mat<D,D> Finv = mip.GetJacobianInverse();
      const double absdet = mip.GetMeasure();

      Vec<D> nref = fquad.normals.Row(i);
      Vec<D> normal = absdet * Trans(Finv) * nref ;
      double len = L2Norm(normal);
      normal /= len;

      const double weight = fquad.weights(i) * len; 
      
      const double a_neg = alpha_neg->Evaluate(mip);
      const double a_pos = alpha_pos->Evaluate(mip);
        
      shapep = fep.GetShape(mip.IP(), lh);
      shapep*=-1.0;
      
      FlatMatrixFixWidth<D> gradu(ndofuv, lh);
      feuv.CalcMappedDShape (mip, gradu);
      
      for (int d = 0; d<D; ++d)
	{
	  bmat.Rows(*dofrangeuv[d]).Col(d)=(a_pos*kappa_pos+a_neg*kappa_neg)*gradu*normal;
	  bmat.Rows(*dofrangeuv_x[d]).Col(d)=gradu*normal;
	  bmat.Rows(dofrangep).Col(d)=normal[d]*shapep;
	  bmat.Rows(dofrangep_x).Col(d)=normal[d]*shapep;
	}    
      
      shapeuv = feuv.GetShape(mip.IP(),lh);
      
      for (int d = 0; d<D; ++d)
	{
	  
	  bmatjump.Rows(*dofrangeuv[d]).Col(d)=0.0;
	  bmatjump.Rows(*dofrangeuv_x[d]).Col(d)=shapeuv;
	  bmatjump.Rows(dofrangep).Col(d)=0.0;
	  bmatjump.Rows(dofrangep_x).Col(d)=0.0;
	}  
      
      for (int d=0; d<D; ++d)
	{
	  for (int l = 0; l < ndofuv_x; ++l)
	    {
	      if (xusign[l] == NEG){
		bmatjump.Row(dofrangeuv[d]->Next()+l)[d] *= -1.0;
		bmat.Row(dofrangeuv[d]->Next()+l)[d] *= kappa_neg * a_neg; 
	      }
	      else{
		bmatjump.Row(dofrangeuv[d]->Next()+l)[d] *= 1.0;
		bmat.Row(dofrangeuv[d]->Next()+l)[d] *= kappa_pos * a_pos; 
	      }
	    }
	}
      
      for (int l = 0; l < ndofp_x; ++l)
	{
	  if (xpsign[l] == NEG){
	    //bmatjump.Row(dofrangep.Next()+l) *= -1.0;
	    bmat.Row(dofrangep.Next()+l) *= kappa_neg * a_neg; 
	  }
	  else{
	    //bmatjump.Row(dofrangep.Next()+l) *= 1.0;
	    bmat.Row(dofrangep.Next()+l) *= kappa_pos * a_pos; 
	  }
	}
      
      /*
      cout<<"bmat = "<<bmat<<endl;
      cout<<"bmatjump = "<<bmatjump<<endl;
      getchar();
      */

      Nc = -weight * bmatjump * Trans(bmat);
      Ns = weight * bmatjump * Trans(bmatjump);

      elmat+= Nc + Trans(Nc) + lam*(ps+1)*ps/h * Ns; 
      //elmat+= Nc - Trans(Nc) + lam*(ps+1)*ps/h * Ns; 
      //elmat+= lam*(ps+1)*ps/h * Ns; 
      // FlatMatrix<> lapmat(ndof_total,ndof_total,lh);
      // laplace->CalcElementMatrix(

      // cout<<"elmat = "<<elmat<<endl;
      // getchar();

      // Vector<double> lami(elmat.Height());
      // Matrix<double> evecs(elmat.Height());
                        
      // CalcEigenSystem (elmat, lami, evecs);
      // cout << "lami = " << endl << lami << endl;
      // getchar();
      
    }
    
  }

  template class XStokesNitscheIntegrator<2>;
  template class XStokesNitscheIntegrator<3>;

  static RegisterBilinearFormIntegrator<XStokesNitscheIntegrator<2>> inisnitschestokes2d("xstokesnitsche",2,3);
  static RegisterBilinearFormIntegrator<XStokesNitscheIntegrator<3>> inisnitschestokes3d("xstokesnitsche",3,3);

}




