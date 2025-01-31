/*********************************************************************/
/* File:   symboliccutbfi.cpp                                        */
/* Author: Christoph Lehrenfeld based on symbolicintegrator.cpp      */
/*         from Joachim Schoeberl (in NGSolve)                       */
/* Date:   September 2016                                            */
/*********************************************************************/
/*
   Symbolic cut integrators
*/

#include <fem.hpp>
#include "../xfem/symboliccutbfi.hpp"
#include "../cutint/xintegration.hpp"
#include "../cutint/straightcutrule.hpp"

namespace ngfem
{

  SymbolicCutBilinearFormIntegrator ::
  SymbolicCutBilinearFormIntegrator (shared_ptr<CoefficientFunction> acf_lset,
                                     shared_ptr<CoefficientFunction> acf,
                                     DOMAIN_TYPE adt,
                                     int aforce_intorder,
                                     int asubdivlvl,
                                     SWAP_DIMENSIONS_POLICY apol,
                                     VorB avb, VorB aelement_vb)
    : SymbolicBilinearFormIntegrator(acf,avb,aelement_vb),
    cf_lset(acf_lset),
    dt(adt),
    force_intorder(aforce_intorder),
    subdivlvl(asubdivlvl),
    pol(apol)
  {
    tie(cf_lset,gf_lset) = CF2GFForStraightCutRule(cf_lset,subdivlvl);
  }


  void 
  SymbolicCutBilinearFormIntegrator ::
  CalcElementMatrix (const FiniteElement & fel,
                     const ElementTransformation & trafo, 
                     FlatMatrix<double> elmat,
                     LocalHeap & lh) const
  {
    elmat = 0.0;
    T_CalcElementMatrixAdd<double,double> (fel, trafo, elmat, lh);
  }

  void
  SymbolicCutBilinearFormIntegrator ::
  CalcElementMatrixAdd (const FiniteElement & fel,
                        const ElementTransformation & trafo,
                        FlatMatrix<double> elmat,
                        LocalHeap & lh) const
  {
    T_CalcElementMatrixAdd<double,double,double> (fel, trafo, elmat, lh);
  }

  void
  SymbolicCutBilinearFormIntegrator ::
  CalcElementMatrixAdd (const FiniteElement & fel,
                        const ElementTransformation & trafo,
                        FlatMatrix<Complex> elmat,
                        LocalHeap & lh) const
  {
    if (fel.ComplexShapes() || trafo.IsComplex())
      T_CalcElementMatrixAdd<Complex,Complex> (fel, trafo, elmat, lh);
    else
      T_CalcElementMatrixAdd<Complex,double> (fel, trafo, elmat, lh);
  }


  template <typename SCAL, typename SCAL_SHAPES, typename SCAL_RES>
  void SymbolicCutBilinearFormIntegrator ::
  T_CalcElementMatrixAdd (const FiniteElement & fel,
                          const ElementTransformation & trafo, 
                          FlatMatrix<SCAL_RES> elmat,
                          LocalHeap & lh) const
    
  {
    static Timer t(string("SymbolicCutBFI::CalcElementMatrixAdd")+typeid(SCAL).name()+typeid(SCAL_SHAPES).name()+typeid(SCAL_RES).name(), 2);
    // ThreadRegionTimer reg(t, TaskManager::GetThreadId());

    if (element_vb != VOL)
      {
        //throw Exception ("EB not yet implemented");
        T_CalcElementMatrixEBAdd<SCAL, SCAL_SHAPES, SCAL_RES> (fel, trafo, elmat, lh);
        return;
      }

    bool is_mixedfe = typeid(fel) == typeid(const MixedFiniteElement&);
    const MixedFiniteElement * mixedfe = static_cast<const MixedFiniteElement*> (&fel);
    const FiniteElement & fel_trial = is_mixedfe ? mixedfe->FETrial() : fel;
    const FiniteElement & fel_test = is_mixedfe ? mixedfe->FETest() : fel;
    // size_t first_std_eval = 0;

    int trial_difforder = 99, test_difforder = 99;
    for (auto proxy : trial_proxies)
      trial_difforder = min(trial_difforder, proxy->Evaluator()->DiffOrder());
    for (auto proxy : test_proxies)
      test_difforder = min(test_difforder, proxy->Evaluator()->DiffOrder());

    int intorder = fel_trial.Order()+fel_test.Order();

    auto et = trafo.GetElementType();
    if (et == ET_TRIG || et == ET_TET)
      intorder -= test_difforder+trial_difforder;

    if (! (et == ET_SEGM || et == ET_TRIG || et == ET_TET || et == ET_QUAD || et == ET_HEX) )
      throw Exception("SymbolicCutBFI can only treat simplices or hyperrectangulars right now");

    if (force_intorder >= 0)
      intorder = force_intorder;

    const IntegrationRule * ir1;
    Array<double> wei_arr;
    tie (ir1, wei_arr) = CreateCutIntegrationRule(cf_lset, gf_lset, trafo, dt, intorder, time_order, lh, subdivlvl, pol);

    if (ir1 == nullptr)
      return;
    ///
    const IntegrationRule * ir = nullptr;
    if (false && time_order > -1) //simple tensor product rule (no moving cuts with this..) ...
    {
       static bool warned = false;
       if (!warned)
       {
         cout << "WARNING: This is a pretty simple tensor product rule in space-time.\n";
         cout << "         A mapped integration rule of this will not see the time,\n";
         cout << "         but the underlying integration rule will." << endl;
         warned = true;
       }
       auto ir1D = SelectIntegrationRule (ET_SEGM, time_order);
       ir = new (lh) IntegrationRule(ir1->Size()*ir1D.Size(),lh);
       for (int i = 0; i < ir1D.Size(); i ++)
         for (int j = 0; j < ir->Size(); j ++)
           (*ir)[i*ir1->Size()+j] = IntegrationPoint((*ir1)[j](0),(*ir1)[j](1),ir1D[i](0),(*ir1)[j].Weight()*ir1D[i].Weight());
       //cout << *ir<< endl;
    }
    else
      ir = ir1;
    BaseMappedIntegrationRule & mir = trafo(*ir, lh);
    
    ProxyUserData ud;
    const_cast<ElementTransformation&>(trafo).userdata = &ud;
    
    // tstart.Stop();
    bool symmetric_so_far = false; //we don't check for symmetry in the formulatin so far (TODO)!
    int k1 = 0;
    int k1nr = 0;
    for (auto proxy1 : trial_proxies)
      {
        int l1 = 0;
        int l1nr = 0;
        for (auto proxy2 : test_proxies)
          {
            bool is_diagonal = proxy1->Dimension() == proxy2->Dimension();
            bool is_nonzero = false;

            for (int k = 0; k < proxy1->Dimension(); k++)
              for (int l = 0; l < proxy2->Dimension(); l++)
                if (nonzeros(l1+l, k1+k))
                  {
                    if (k != l) is_diagonal = false;
                    is_nonzero = true;
                  }

            if (is_nonzero) //   && k1nr*test_proxies.Size()+l1nr >= first_std_eval)
              {
                HeapReset hr(lh);
                bool samediffop = (*(proxy1->Evaluator()) == *(proxy2->Evaluator())) && !is_mixedfe;
                // td.Start();
                FlatTensor<3,SCAL> proxyvalues(lh, mir.Size(), proxy1->Dimension(), proxy2->Dimension());
                FlatVector<SCAL> diagproxyvalues(mir.Size()*proxy1->Dimension(), lh);
                FlatMatrix<SCAL> val(mir.Size(), 1, lh);
                
                
                if (!is_diagonal)
                  for (int k = 0; k < proxy1->Dimension(); k++)
                    for (int l = 0; l < proxy2->Dimension(); l++)
                      {
                        if (nonzeros(l1+l, k1+k))
                          {
                            if (k != l) is_diagonal = false;
                            is_nonzero = true;
                            ud.trialfunction = proxy1;
                            ud.trial_comp = k;
                            ud.testfunction = proxy2;
                            ud.test_comp = l;
                            
                            cf -> Evaluate (mir, val);
                            proxyvalues(STAR,k,l) = val.Col(0);
                          }
                        else
                          proxyvalues(STAR,k,l) = 0.0;
                      }
                else
                  for (int k = 0; k < proxy1->Dimension(); k++)
                    {
                      ud.trialfunction = proxy1;
                      ud.trial_comp = k;
                      ud.testfunction = proxy2;
                      ud.test_comp = k;

                      if (!elementwise_constant)
                        {
                          cf -> Evaluate (mir, val);
                          diagproxyvalues.Slice(k, proxy1->Dimension()) = val.Col(0);
                        }
                      else
                        {
                          cf -> Evaluate (mir[0], val.Row(0));
                          diagproxyvalues.Slice(k, proxy1->Dimension()) = val(0,0);
                        }
                    }
            
                // td.Stop();

                if (!mir.IsComplex())
                  {
                    if (!is_diagonal)
                      for (int i = 0; i < mir.Size(); i++)
                        proxyvalues(i,STAR,STAR) *= mir[i].GetMeasure()*wei_arr[i];
                    else
                      for (int i = 0; i < mir.Size(); i++)
                        diagproxyvalues.Range(proxy1->Dimension()*IntRange(i,i+1)) *= mir[i].GetMeasure()*wei_arr[i];
                  }
                else
                  { // pml
                    throw Exception("not treated yet (interface-weights!)");
                    if (!is_diagonal)
                      for (int i = 0; i < mir.Size(); i++)
                        proxyvalues(i,STAR,STAR) *= mir[i].GetMeasure()*wei_arr[i];
                    else
                      for (int i = 0; i < mir.Size(); i++)
                        diagproxyvalues.Range(proxy1->Dimension()*IntRange(i,i+1)) *=
                          static_cast<const ScalMappedIntegrationPoint<SCAL>&> (mir[i]).GetJacobiDet()*wei_arr[i];
                  }
                IntRange r1 = proxy1->Evaluator()->UsedDofs(fel_trial);
                IntRange r2 = proxy2->Evaluator()->UsedDofs(fel_test);
                SliceMatrix<SCAL_RES> part_elmat = elmat.Rows(r2).Cols(r1);
                FlatMatrix<SCAL_SHAPES,ColMajor> bmat1(proxy1->Dimension(), elmat.Width(), lh);
                FlatMatrix<SCAL_SHAPES,ColMajor> bmat2(proxy2->Dimension(), elmat.Height(), lh);

                
                constexpr size_t BS = 16;
                for (size_t i = 0; i < mir.Size(); i+=BS)
                  {
                    HeapReset hr(lh);
                    int bs = min2(size_t(BS), mir.Size()-i);
                    
                    FlatMatrix<SCAL_SHAPES> bbmat1(elmat.Width(), bs*proxy1->Dimension(), lh);
                    FlatMatrix<SCAL> bdbmat1(elmat.Width(), bs*proxy2->Dimension(), lh);
                    FlatMatrix<SCAL_SHAPES> bbmat2 = samediffop ?
                      bbmat1 : FlatMatrix<SCAL_SHAPES>(elmat.Height(), bs*proxy2->Dimension(), lh);

                    // tb.Start();
                    BaseMappedIntegrationRule & bmir = mir.Range(i, i+bs, lh);

                    proxy1->Evaluator()->CalcMatrix(fel_trial, bmir, Trans(bbmat1), lh);

                    if (!samediffop)
                      proxy2->Evaluator()->CalcMatrix(fel_test, bmir, Trans(bbmat2), lh);
                    // tb.Stop();

                    // tdb.Start();
                    if (is_diagonal)
                      {
                        FlatVector<SCAL> diagd(bs*proxy1->Dimension(), lh);
                        diagd = diagproxyvalues.Range(i*proxy1->Dimension(),
                                                      (i+bs)*proxy1->Dimension());
                        for (size_t i = 0; i < diagd.Size(); i++)
                          bdbmat1.Col(i) = diagd(i) * bbmat1.Col(i);
                        // MultMatDiagMat(bbmat1, diagd, bdbmat1);
                        // tdb.AddFlops (bbmat1.Height()*bbmat1.Width());
                      }
                    else
                      {
                        for (int j = 0; j < bs; j++)
                          {
                            int ii = i+j;
                            IntRange r1 = proxy1->Dimension() * IntRange(j,j+1);
                            IntRange r2 = proxy2->Dimension() * IntRange(j,j+1);
                            // bdbmat1.Cols(r2) = bbmat1.Cols(r1) * proxyvalues(ii,STAR,STAR);
                            MultMatMat (bbmat1.Cols(r1), proxyvalues(ii,STAR,STAR), bdbmat1.Cols(r2));
                          }
                        // tdb.AddFlops (proxy1->Dimension()*proxy2->Dimension()*bs*bbmat1.Height());
                      }
                    // tdb.Stop();
                    // tlapack.Start();
                    // elmat.Rows(r2).Cols(r1) += bbmat2.Rows(r2) * Trans(bdbmat1.Rows(r1));
                    // AddABt (bbmat2.Rows(r2), bdbmat1.Rows(r1), elmat.Rows(r2).Cols(r1));

                    symmetric_so_far &= samediffop && is_diagonal;
                    if (symmetric_so_far)
                      AddABtSym (bbmat2.Rows(r2), bdbmat1.Rows(r1), part_elmat);
                    else
                      AddABt (bbmat2.Rows(r2), bdbmat1.Rows(r1), part_elmat);
                    // tlapack.Stop();
                    // tlapack.AddFlops (r2.Size()*r1.Size()*bdbmat1.Width());
                  }

                if (symmetric_so_far)
                  for (int i = 0; i < part_elmat.Height(); i++)
                    for (int j = i+1; j < part_elmat.Width(); j++)
                      part_elmat(i,j) = part_elmat(j,i);
              }
            
            l1 += proxy2->Dimension();
            l1nr++;
          }
        k1 += proxy1->Dimension();
        k1nr++;
      }
  }

  template <typename SCAL, typename SCAL_SHAPES, typename SCAL_RES>
  void SymbolicCutBilinearFormIntegrator ::
    T_CalcElementMatrixEBAdd (const FiniteElement & fel,
                                   const ElementTransformation & trafo,
                                   FlatMatrix<SCAL_RES> elmat,
                                   LocalHeap & lh) const

                                       {
      static Timer t("symbolicBFI - CalcElementMatrix EB", 2);
      /*
      static Timer tir("symbolicBFI - CalcElementMatrix EB - intrules", 2);
      static Timer td("symbolicBFI - CalcElementMatrix EB - dmats", 2);
      static Timer tdb("symbolicBFI - CalcElementMatrix EB - b*d", 2);
      static Timer tb("symbolicBFI - CalcElementMatrix EB - bmats", 2);
      static Timer tmult("symbolicBFI - CalcElementMatrix EB - mult", 2);
      */
      RegionTimer reg(t);

      //elmat = 0;

      const MixedFiniteElement * mixedfe = dynamic_cast<const MixedFiniteElement*> (&fel);
      const FiniteElement & fel_trial = mixedfe ? mixedfe->FETrial() : fel;
      const FiniteElement & fel_test = mixedfe ? mixedfe->FETest() : fel;

      auto eltype = trafo.GetElementType();

      Facet2ElementTrafo transform(eltype, element_vb);
      int nfacet = transform.GetNFacets();

      /*
      if (simd_evaluate)
        // if (false)  // throwing the no-simd exception after some terms already added is still a problem
        {
          try
            {
              for (int k = 0; k < nfacet; k++)
                {
                  HeapReset hr(lh);
                  ngfem::ELEMENT_TYPE etfacet = transform.FacetType (k);
                  if(etfacet != ET_SEGM) throw Exception("Only ET_SEGM support yet!");

                  IntegrationPoint ipl(0,0,0,0);
                  IntegrationPoint ipr(1,0,0,0);
                  const IntegrationPoint & facet_ip_l = transform( k, ipl);
                  const IntegrationPoint & facet_ip_r = transform( k, ipr);
                  MappedIntegrationPoint<2,2> mipl(facet_ip_l,trafo);
                  MappedIntegrationPoint<2,2> mipr(facet_ip_r,trafo);
                  double lset_l = gf_lset->Evaluate(mipl); //TODO: Not sure why that is seemingly better than cf_lset....
                  double lset_r = gf_lset->Evaluate(mipr);

                  const int order_sum = fel_trial.Order()+fel_test.Order();

                  IntegrationRule * ir_facet_tmp;
                  if ((lset_l > 0 && lset_r > 0) && dt != POS) continue;
                  if ((lset_l < 0 && lset_r < 0) && dt != NEG) continue;

                  if (dt == IF)
                  {
                    ir_facet_tmp = new (lh) IntegrationRule(1,lh);
                    double xhat = - lset_l / (lset_r - lset_l );
                    (*ir_facet_tmp)[0] = IntegrationPoint(xhat, 0, 0, 1.0);
                  }
                  else if ((lset_l > 0) != (lset_r > 0))
                  {
                    IntegrationRule ir_tmp (etfacet, order_sum);
                    ir_facet_tmp = new (lh) IntegrationRule(ir_tmp.Size(),lh);
                    ///....CutIntegrationRule(cf_lset, trafo, dt, intorder, subdivlvl, lh);
                    double x0 = 0.0;
                    double x1 = 1.0;
                    double xhat = - lset_l / (lset_r - lset_l );
                    if ( ((lset_l > 0) && dt == POS) || ((lset_l < 0) && dt == NEG))
                      x1 = xhat;
                    else
                      x0 = xhat;
                    double len = x1-x0;
                    for (int i = 0; i < ir_tmp.Size(); i++)
                      (*ir_facet_tmp)[i] = IntegrationPoint(x0 + ir_tmp[i].Point()[0] * len, 0, 0, len*ir_tmp[i].Weight());
                  }
                  else
                  {
                    ir_facet_tmp = new (lh) IntegrationRule(etfacet, order_sum);
                  }
                  SIMD_IntegrationRule ir_facet( (*ir_facet_tmp).Size(), lh);
                  for(int i=0; i<(*ir_facet_tmp).Size(); i++) ir_facet[i] = (*ir_facet_tmp)[i];
                  auto & ir_facet_vol = transform(k, ir_facet, lh);

                  auto & mir = trafo(ir_facet_vol, lh);

                  ProxyUserData ud;
                  const_cast<ElementTransformation&>(trafo).userdata = &ud;

                  mir.ComputeNormalsAndMeasure(eltype, k);

                  for (int k1 : Range(trial_proxies))
                    for (int l1 : Range(test_proxies))
                      {
                        if (!nonzeros_proxies(l1, k1)) continue;

                        auto proxy1 = trial_proxies[k1];
                        auto proxy2 = test_proxies[l1];
                        size_t dim_proxy1 = proxy1->Dimension();
                        size_t dim_proxy2 = proxy2->Dimension();
                        HeapReset hr(lh);
                        FlatMatrix<SIMD<SCAL>> proxyvalues(dim_proxy1*dim_proxy2, ir_facet.Size(), lh);

                        // td.Start();
                        for (int k = 0; k < dim_proxy1; k++)
                          for (int l = 0; l < dim_proxy2; l++)
                            {
                              ud.trialfunction = proxy1;
                              ud.trial_comp = k;
                              ud.testfunction = proxy2;
                              ud.test_comp = l;

                              auto kk = l + k*dim_proxy2;
                              cf->Evaluate (mir, proxyvalues.Rows(kk, kk+1));
                              if (dt != IF) {
                                 for (size_t i = 0; i < mir.Size(); i++)
                                   proxyvalues(kk, i) *= mir[i].GetWeight();
                              }
                              else
                                  for (size_t i = 0; i < mir.Size(); i++)
                                    proxyvalues(kk, i) *= ir_facet[i].Weight();
                            }


                        IntRange r1 = proxy1->Evaluator()->UsedDofs(fel_trial);
                        IntRange r2 = proxy2->Evaluator()->UsedDofs(fel_test);
                        SliceMatrix<SCAL> part_elmat = elmat.Rows(r2).Cols(r1);

                        FlatMatrix<SIMD<SCAL_SHAPES>> bbmat1(elmat.Width()*dim_proxy1, mir.Size(), lh);
                        FlatMatrix<SIMD<SCAL>> bdbmat1(elmat.Width()*dim_proxy2, mir.Size(), lh);
                        bool samediffop = false; // not yet available
                        FlatMatrix<SIMD<SCAL_SHAPES>> bbmat2 = samediffop ?
                          bbmat1 : FlatMatrix<SIMD<SCAL_SHAPES>>(elmat.Height()*dim_proxy2, mir.Size(), lh);

                        FlatMatrix<SIMD<SCAL>> hbdbmat1(elmat.Width(), dim_proxy2*mir.Size(),
                                                        &bdbmat1(0,0));
                        FlatMatrix<SIMD<SCAL_SHAPES>> hbbmat2(elmat.Height(), dim_proxy2*mir.Size(),
                                                              &bbmat2(0,0));

                        {
                          // ThreadRegionTimer regbmat(timer_SymbBFIbmat, TaskManager::GetThreadId());
                          proxy1->Evaluator()->CalcMatrix(fel_trial, mir, bbmat1);
                          if (!samediffop)
                            proxy2->Evaluator()->CalcMatrix(fel_test, mir, bbmat2);
                        }

                        bdbmat1 = 0.0;
                        for (auto i : r1)
                          for (size_t j = 0; j < dim_proxy2; j++)
                            for (size_t k = 0; k < dim_proxy1; k++)
                              {
                                auto res = bdbmat1.Row(i*dim_proxy2+j);
                                auto a = bbmat1.Row(i*dim_proxy1+k);
                                auto b = proxyvalues.Row(k*dim_proxy2+j);
                                res += pw_mult(a,b);
                              }

                        AddABt (hbbmat2.Rows(r2), hbdbmat1.Rows(r1), part_elmat);
                      }
                }
              return;
            }

          catch (ExceptionNOSIMD e)
            {
              cout << IM(4) << e.What() << endl
                   << "switching to scalar evaluation, may be a problem with Add" << endl;
              simd_evaluate = false;
              throw ExceptionNOSIMD("disabled simd-evaluate in AddElementMatrixEB");
            }
        }*/

      const int order_sum = fel_trial.Order()+fel_test.Order();
      for (int k = 0; k < nfacet; k++)
        {
          // tir.Start();
          HeapReset hr(lh);
          ngfem::ELEMENT_TYPE etfacet = transform.FacetType (k);
          //if(etfacet != ET_SEGM) throw Exception("Only ET_SEGM support yet!");
          const IntegrationRule * ir_facet_tmp;

          if(etfacet == ET_SEGM){
              IntegrationPoint ipl(0,0,0,0);
              IntegrationPoint ipr(1,0,0,0);
              const IntegrationPoint & facet_ip_l = transform( k, ipl);
              const IntegrationPoint & facet_ip_r = transform( k, ipr);
              MappedIntegrationPoint<2,2> mipl(facet_ip_l,trafo);
              MappedIntegrationPoint<2,2> mipr(facet_ip_r,trafo);
              double lset_l = gf_lset->Evaluate(mipl); //TODO: Not sure why that is seemingly better than cf_lset....
              double lset_r = gf_lset->Evaluate(mipr);

              if ((lset_l > 0 && lset_r > 0) && dt != POS) continue;
              if ((lset_l < 0 && lset_r < 0) && dt != NEG) continue;

              ir_facet_tmp = StraightCutIntegrationRuleUntransformed(Vec<2>{lset_r, lset_l}, ET_SEGM, dt, order_sum, FIND_OPTIMAL, lh);
          }
          else if((etfacet == ET_TRIG) || (etfacet == ET_QUAD)){
              int nverts = ElementTopology::GetNVertices(etfacet);
              // Determine vertex values of the level set function:
              vector<double> lset(nverts);
              const POINT3D * verts_pts = ElementTopology::GetVertices(etfacet);

              vector<Vec<2>> verts;
              for(int i=0; i<nverts; i++) verts.push_back(Vec<2>{verts_pts[i][0], verts_pts[i][1]});
              bool haspos = false;
              bool hasneg = false;
              for (int i = 0; i < nverts; i++)
              {
                IntegrationPoint ip = *(new (lh) IntegrationPoint(verts_pts[i][0],verts_pts[i][1]));

                const IntegrationPoint & ip_in_tet = transform( k, ip);
                MappedIntegrationPoint<3,3> & mip = *(new (lh) MappedIntegrationPoint<3,3>(ip_in_tet,trafo));

                //cout << "mip : " << mip.GetPoint() << endl;
                lset[i] = gf_lset->Evaluate(mip);
                //cout << "lset[i] : " << lset[i] << endl;
                haspos = lset[i] > 0 ? true : haspos;
                hasneg = lset[i] < 0 ? true : hasneg;
              }

              //if (!hasneg || !haspos) continue;
              if(dt != POS && !hasneg) continue;
              if(dt != NEG && !haspos) continue;
              FlatVector<double> lset_fv(nverts, lh);
              for(int i=0; i<nverts; i++){
                  lset_fv[i] = lset[i];
                  if(abs(lset_fv[i]) < 1e-16) throw Exception("lset val 0 in SymbolicCutFacetBilinearFormIntegrator");
              }

              LevelsetWrapper lsw(lset, etfacet);
              ir_facet_tmp = StraightCutIntegrationRuleUntransformed(lset_fv, etfacet, dt, order_sum, FIND_OPTIMAL, lh);
              //cout << "ir_facet_tmp: " << *ir_facet_tmp << endl;
              Vec<3> tetdiffvec2(0.);

              IntegrationRule & ir_scr_intet2 = transform( k, (*ir_facet_tmp), lh);
              MappedIntegrationRule<3,3> mir3(ir_scr_intet2,trafo,lh);
              int npoints = ir_facet_tmp->Size();
              for (int i = 0; i < npoints; i++)
              {
                  IntegrationPoint & ip = (*ir_facet_tmp)[i];
                  Vec<3> normal = lsw.GetNormal(ip.Point());
                  Vec<2> tang = {normal[1],-normal[0]};

                  tetdiffvec2 = transform.GetJacobian( k, lh) * tang;
                  auto F = mir3[i].GetJacobian();
                  auto mapped_tang = F * tetdiffvec2;
                  const double ratio_meas1D = L2Norm(mapped_tang);
                  ip.SetWeight((*ir_facet_tmp)[i].Weight() * ratio_meas1D);
              }
          }

          IntegrationRule ir_facet( (*ir_facet_tmp).Size(), lh);
          for(int i=0; i<(*ir_facet_tmp).Size(); i++) ir_facet[i] = (*ir_facet_tmp)[i];

          IntegrationRule & ir_facet_vol = transform(k, ir_facet, lh);

          BaseMappedIntegrationRule & mir = trafo(ir_facet_vol, lh);

          ProxyUserData ud;
          const_cast<ElementTransformation&>(trafo).userdata = &ud;

          mir.ComputeNormalsAndMeasure(eltype, k);

          for (int k1 : Range(trial_proxies))
            for (int l1 : Range(test_proxies))
              {
                if (!nonzeros_proxies(l1, k1)) continue;

                auto proxy1 = trial_proxies[k1];
                auto proxy2 = test_proxies[l1];

                HeapReset hr(lh);
                FlatTensor<3,SCAL> proxyvalues(lh, mir.Size(), proxy1->Dimension(), proxy2->Dimension());
                FlatMatrix<SCAL> val(mir.Size(), 1, lh);

                // td.Start();
                for (int k = 0; k < proxy1->Dimension(); k++)
                  for (int l = 0; l < proxy2->Dimension(); l++)
                    {
                      ud.trialfunction = proxy1;
                      ud.trial_comp = k;
                      ud.testfunction = proxy2;
                      ud.test_comp = l;

                      cf->Evaluate (mir, val);
                      if(dt != IF){
                        for (int i = 0; i < mir.Size(); i++)
                          val(i) *= ir_facet[i].Weight() * mir[i].GetMeasure();
                      }
                      else
                          for (int i = 0; i < mir.Size(); i++)
                            val(i) *= ir_facet[i].Weight();
                      proxyvalues(STAR,k,l) = val.Col(0);
                    }
                // td.Stop();
                /*
                for (int i = 0; i < mir.Size(); i++)
                  {
                    tb.Start();
                    FlatMatrix<SCAL_SHAPES,ColMajor> bmat1(proxy1->Dimension(), elmat.Width(), lh);
                    FlatMatrix<SCAL,ColMajor> dbmat1(proxy2->Dimension(), elmat.Width(), lh);
                    FlatMatrix<SCAL_SHAPES,ColMajor> bmat2(proxy2->Dimension(), elmat.Height(), lh);

                    proxy1->Evaluator()->CalcMatrix(fel, mir[i], bmat1, lh);
                    proxy2->Evaluator()->CalcMatrix(fel, mir[i], bmat2, lh);
                    tb.Stop();
                    tmult.Start();
                    IntRange r1 = proxy1->Evaluator()->UsedDofs(fel);
                    IntRange r2 = proxy2->Evaluator()->UsedDofs(fel);

                    dbmat1 = proxyvalues(i,STAR,STAR) * bmat1;
                    elmat.Rows(r2).Cols(r1) += Trans (bmat2.Cols(r2)) * dbmat1.Cols(r1);
                    tmult.Stop();
                  }
                */

                IntRange r1 = proxy1->Evaluator()->UsedDofs(fel_trial);
                IntRange r2 = proxy2->Evaluator()->UsedDofs(fel_test);
                SliceMatrix<SCAL> part_elmat = elmat.Rows(r2).Cols(r1);

                constexpr size_t BS = 16;
                for (size_t i = 0; i < mir.Size(); i+=BS)
                  {
                    HeapReset hr(lh);
                    int bs = min2(BS, mir.Size()-i);

                    //TODO: Changed AFlatMatrix into FlatMatrix here... Fine?
                    FlatMatrix<SCAL_SHAPES> bbmat1(elmat.Width(), bs*proxy1->Dimension(), lh);
                    FlatMatrix<SCAL> bdbmat1(elmat.Width(), bs*proxy2->Dimension(), lh);
                    FlatMatrix<SCAL_SHAPES> bbmat2(elmat.Height(), bs*proxy2->Dimension(), lh);

                    // tb.Start();
                    BaseMappedIntegrationRule & bmir = mir.Range(i, i+bs, lh);
                    proxy1->Evaluator()->CalcMatrix(fel_trial, bmir, Trans(bbmat1), lh);
                    proxy2->Evaluator()->CalcMatrix(fel_test, bmir, Trans(bbmat2), lh);
                    // tb.Stop();
                    bdbmat1 = 0.0;
                    // tdb.Start();

                    auto part_bbmat1 = bbmat1.Rows(r1);
                    auto part_bdbmat1 = bdbmat1.Rows(r1);
                    auto part_bbmat2 = bbmat2.Rows(r2);

                    for (int j = 0; j < bs; j++)
                      {
                        IntRange rj1 = proxy1->Dimension() * IntRange(j,j+1);
                        IntRange rj2 = proxy2->Dimension() * IntRange(j,j+1);
                        MultMatMat (part_bbmat1.Cols(rj1), proxyvalues(i+j,STAR,STAR), part_bdbmat1.Cols(rj2));
                      }

                    // tdb.Stop();

                    // tmult.Start();
                    AddABt (part_bbmat2, part_bdbmat1, part_elmat);
                    // part_elmat += part_bbmat2 * Trans(part_bdbmat1);
                    // tmult.Stop();
                    // tmult.AddFlops (r2.Size() * r1.Size() * bbmat2.Width());
                  }
              }
        }
    }


  SymbolicCutFacetBilinearFormIntegrator ::
  SymbolicCutFacetBilinearFormIntegrator (shared_ptr<CoefficientFunction> acf_lset,
                                          shared_ptr<CoefficientFunction> acf,
                                          DOMAIN_TYPE adt,
                                          int aforce_intorder,
                                          int asubdivlvl)
    : SymbolicFacetBilinearFormIntegrator(acf,VOL,false),
      cf_lset(acf_lset), dt(adt),
      force_intorder(aforce_intorder), subdivlvl(asubdivlvl)
  {
    simd_evaluate=false;
  }

  void  SymbolicCutFacetBilinearFormIntegrator::CalcFacetMatrix (
    const FiniteElement & fel1, int LocalFacetNr1,
    const ElementTransformation & trafo1, FlatArray<int> & ElVertices1,
    const FiniteElement & fel2, int LocalFacetNr2,
    const ElementTransformation & trafo2, FlatArray<int> & ElVertices2,
    FlatMatrix<double> elmat,
    LocalHeap & lh) const
  {
    static Timer t_all("SymbolicCutFacetBilinearFormIntegrator::CalcFacetMatrix", 2);
    RegionTimer reg(t_all);
    elmat = 0.0;
    
    if (LocalFacetNr2==-1) throw Exception ("SymbolicFacetBFI: LocalFacetNr2==-1");

    int maxorder = max2 (fel1.Order(), fel2.Order());
    
    auto eltype1 = trafo1.GetElementType();
    auto eltype2 = trafo2.GetElementType();
    auto etfacet = ElementTopology::GetFacetType (eltype1, LocalFacetNr1);

    Facet2ElementTrafo transform1(eltype1, ElVertices1); 

    if (etfacet != ET_SEGM){
        if(dt != IF) throw Exception("cut facet bilinear form can only do volume ints on ET_SEGM");
        if (etfacet != ET_TRIG && etfacet != ET_QUAD) throw Exception("cut facet bilinear form can do IF ints only on ET_SEGM, ET_TRIG and ET_QUAD");
    }

    IntegrationRule * ir_facet = nullptr;
    const IntegrationRule * ir_scr = nullptr;

    if (etfacet != ET_SEGM && dt == IF) // Codim 2 special case (3D -> 1D)
    {
      static Timer t("symbolicCutBFI - CoDim2-hack", 2);
      RegionTimer reg(t);
      static bool first = true;
      if (first)
      {
        cout << "WARNING: unfitted codim-2 integrals are experimental!" << endl;
        cout << "         (and not performance-tuned)" << endl;
      }
      first = false;

      int nverts = ElementTopology::GetNVertices(etfacet);
      // Determine vertex values of the level set function:
      vector<double> lset(nverts);
      const POINT3D * verts_pts = ElementTopology::GetVertices(etfacet);

      vector<Vec<2>> verts;
      for(int i=0; i<nverts; i++) verts.push_back(Vec<2>{verts_pts[i][0], verts_pts[i][1]});

      bool haspos = false;
      bool hasneg = false;
      for (int i = 0; i < nverts; i++)
      {
        IntegrationPoint ip = *(new (lh) IntegrationPoint(verts_pts[i][0],verts_pts[i][1]));
        
        const IntegrationPoint & ip_in_tet = transform1( LocalFacetNr1, ip);
        MappedIntegrationPoint<3,3> & mip = *(new (lh) MappedIntegrationPoint<3,3>(ip_in_tet,trafo1));
        //cout << "mip : " << mip.GetPoint() << endl;

        lset[i] = cf_lset->Evaluate(mip);
        //cout << "lset[i] : " << lset[i] << endl;
        haspos = lset[i] > 0 ? true : haspos;
        hasneg = lset[i] < 0 ? true : hasneg;

      }
      if (!hasneg || !haspos) return;

      FlatVector<double> lset_fv(nverts, lh);
        for(int i=0; i<nverts; i++){
            lset_fv[i] = lset[i];
                if(abs(lset_fv[i]) < 1e-16) throw Exception("lset val 0 in SymbolicCutFacetBilinearFormIntegrator");
        }

      LevelsetWrapper lsw(lset, etfacet);
      ir_scr = StraightCutIntegrationRuleUntransformed(lset_fv, etfacet, dt, 2*maxorder, FIND_OPTIMAL, lh);
      //cout << "ir_scr: " << *ir_scr << endl;
      Vec<3> tetdiffvec2(0.);

      IntegrationRule & ir_scr_intet2 = transform1( LocalFacetNr1, (*ir_scr), lh);
      MappedIntegrationRule<3,3> mir3(ir_scr_intet2,trafo1,lh);
      int npoints = ir_scr->Size();

      for (int i = 0; i < npoints; i++)
      {
          IntegrationPoint & ip = (*ir_scr)[i];
          Vec<3> normal = lsw.GetNormal(ip.Point());
          Vec<2> tang = {normal[1],-normal[0]};

          tetdiffvec2 = transform1.GetJacobian( LocalFacetNr1, lh) * tang;
          auto F = mir3[i].GetJacobian();
          auto mapped_tang = F * tetdiffvec2;
          const double ratio_meas1D = L2Norm(mapped_tang);
          ip.SetWeight((*ir_scr)[i].Weight() * ratio_meas1D);
      }
    }
    else //Codim1 or 2D->0D
    {
      IntegrationPoint ipl(0,0,0,0);
      IntegrationPoint ipr(1,0,0,0);
      const IntegrationPoint & facet_ip_l = transform1( LocalFacetNr1, ipl);
      const IntegrationPoint & facet_ip_r = transform1( LocalFacetNr1, ipr);
      MappedIntegrationPoint<2,2> mipl(facet_ip_l,trafo1);
      MappedIntegrationPoint<2,2> mipr(facet_ip_r,trafo1);
      double lset_l = cf_lset->Evaluate(mipl);
      double lset_r = cf_lset->Evaluate(mipr);

      
      if ((lset_l > 0 && lset_r > 0) && dt != POS) return;
      if ((lset_l < 0 && lset_r < 0) && dt != NEG) return;

      ir_scr = StraightCutIntegrationRuleUntransformed(Vec<2>{lset_r, lset_l}, ET_SEGM, dt, 2*maxorder, FIND_OPTIMAL, lh);
      if (ir_scr == nullptr) return;
    }

    IntegrationRule & ir_facet_vol1 = transform1(LocalFacetNr1, (*ir_scr), lh);

    Facet2ElementTrafo transform2(eltype2, ElVertices2); 
    IntegrationRule & ir_facet_vol2 = transform2(LocalFacetNr2, (*ir_scr), lh);

    BaseMappedIntegrationRule & mir1 = trafo1(ir_facet_vol1, lh);
    BaseMappedIntegrationRule & mir2 = trafo2(ir_facet_vol2, lh);

    mir1.SetOtherMIR (&mir2);
    mir2.SetOtherMIR (&mir1);
    
    ProxyUserData ud;
    const_cast<ElementTransformation&>(trafo1).userdata = &ud;

    for (int k1 : Range(trial_proxies))
      for (int l1 : Range(test_proxies))
        {
          HeapReset hr(lh);
          FlatMatrix<> val(mir1.Size(), 1,lh);

          auto proxy1 = trial_proxies[k1];
          auto proxy2 = test_proxies[l1];

          FlatTensor<3> proxyvalues(lh, mir1.Size(), proxy2->Dimension(), proxy1->Dimension());
          /*
          FlatVector<> measure(mir1.Size(), lh);
          switch (trafo1.SpaceDim())
            {
	    case 1:
              {
                Vec<1> normal_ref = ElementTopology::GetNormals<1>(eltype1)[LocalFacetNr1];
                for (int i = 0; i < mir1.Size(); i++)
                  {
                    auto & mip = static_cast<const MappedIntegrationPoint<1,1>&> (mir1[i]);
                    Mat<1> inv_jac = mip.GetJacobianInverse();
                    double det = mip.GetMeasure();
                    Vec<1> normal = det * Trans (inv_jac) * normal_ref;       
                    double len = L2Norm (normal);    // that's the surface measure 
                    normal /= len;                   // normal vector on physical element
                    const_cast<MappedIntegrationPoint<1,1>&> (mip).SetNV(normal);
                    measure(i) = len;
                  }
                break;
              }
            case 2:
              {
                Vec<2> normal_ref = ElementTopology::GetNormals<2>(eltype1)[LocalFacetNr1];
                for (int i = 0; i < mir1.Size(); i++)
                  {
                    auto & mip = static_cast<const MappedIntegrationPoint<2,2>&> (mir1[i]);
                    Mat<2> inv_jac = mip.GetJacobianInverse();
                    double det = mip.GetMeasure();
                    Vec<2> normal = det * Trans (inv_jac) * normal_ref;       
                    double len = L2Norm (normal);    // that's the surface measure 
                    normal /= len;                   // normal vector on physical element
                    const_cast<MappedIntegrationPoint<2,2>&> (mip).SetNV(normal);
                    measure(i) = len;
                  }
                break;
              }
            default:
              cout << "Symbolic DG in " << trafo1.SpaceDim() << " not available" << endl;
            }
          */

          mir1.ComputeNormalsAndMeasure (eltype1, LocalFacetNr1);
          mir2.ComputeNormalsAndMeasure (eltype2, LocalFacetNr2);
          
          for (int k = 0; k < proxy1->Dimension(); k++)
            for (int l = 0; l < proxy2->Dimension(); l++)
              {
                ud.trialfunction = proxy1;
                ud.trial_comp = k;
                ud.testfunction = proxy2;
                ud.test_comp = l;
                
                cf -> Evaluate (mir1, val);
                proxyvalues(STAR,l,k) = val.Col(0);
              }
          if (dt == IF) // either 2D->0D (no need for weight correction) or 3D->1D ( weights are already corrected)
              for (int i = 0; i < mir1.Size(); i++){
                    //proxyvalues(i,STAR,STAR) *= measure(i) * (*ir_scr)[i].Weight();
                    //proxyvalues(i,STAR,STAR) *= (*ir_facet)[i].Weight();
                    proxyvalues(i,STAR,STAR) *= (*ir_scr)[i].Weight(); //The right choice for 2D...
                    //cout << "The two options: " << (*ir_facet)[i].Weight() << "\n" << measure(i) * (*ir_scr)[i].Weight() << endl;
                    //cout << "(*ir_scr)[i].Weight(): " << (*ir_scr)[i].Weight() << endl;
              }

          else // codim 1
          {
             // throw Exception("Foo!");
             for (int i = 0; i < mir1.Size(); i++){
                 //proxyvalues(i,STAR,STAR) *= mir1[i].GetMeasure() * (*ir_scr)[i].Weight();
                 // proxyvalues(i,STAR,STAR) *= measure(i) * ir_scr[i].Weight();
                 proxyvalues(i,STAR,STAR) *= mir1[i].GetMeasure() * (*ir_scr)[i].Weight();
             }
          }

          IntRange trial_range = proxy1->IsOther() ? IntRange(fel1.GetNDof(), elmat.Width()) : IntRange(0, fel1.GetNDof());
          IntRange test_range  = proxy2->IsOther() ? IntRange(fel1.GetNDof(), elmat.Height()) : IntRange(0, fel1.GetNDof());

          auto loc_elmat = elmat.Rows(test_range).Cols(trial_range);
          FlatMatrix<double,ColMajor> bmat1(proxy1->Dimension(), loc_elmat.Width(), lh);
          FlatMatrix<double,ColMajor> bmat2(proxy2->Dimension(), loc_elmat.Height(), lh);

          // enum { BS = 16 };
          constexpr size_t BS = 16;
          for (int i = 0; i < mir1.Size(); i+=BS)
            {
              int rest = min2(size_t(BS), mir1.Size()-i);
              HeapReset hr(lh);
              FlatMatrix<double,ColMajor> bdbmat1(rest*proxy2->Dimension(), loc_elmat.Width(), lh);
              FlatMatrix<double,ColMajor> bbmat2(rest*proxy2->Dimension(), loc_elmat.Height(), lh);

              for (int j = 0; j < rest; j++)
                {
                  int ii = i+j;
                  IntRange r2 = proxy2->Dimension() * IntRange(j,j+1);
                  if (proxy1->IsOther())
                    proxy1->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat1, lh);
                  else
                    proxy1->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat1, lh);
                  
                  if (proxy2->IsOther())
                    proxy2->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat2, lh);
                  else
                    proxy2->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat2, lh);
                  
                  bdbmat1.Rows(r2) = proxyvalues(ii,STAR,STAR) * bmat1;
                  bbmat2.Rows(r2) = bmat2;
                }

              IntRange r1 = proxy1->Evaluator()->UsedDofs(proxy1->IsOther() ? fel2 : fel1);
              IntRange r2 = proxy2->Evaluator()->UsedDofs(proxy2->IsOther() ? fel2 : fel1);
              loc_elmat.Rows(r2).Cols(r1) += Trans (bbmat2.Cols(r2)) * bdbmat1.Cols(r1) | Lapack;
            }
        }
  }
  SymbolicFacetBilinearFormIntegrator2 ::
  SymbolicFacetBilinearFormIntegrator2 (shared_ptr<CoefficientFunction> acf,
                                        int aforce_intorder)
    : SymbolicFacetBilinearFormIntegrator(acf,VOL,false),
      force_intorder(aforce_intorder)
  {
    simd_evaluate=false;
  }

  void SymbolicFacetBilinearFormIntegrator2 ::
  CalcFacetMatrix (const FiniteElement & fel1, int LocalFacetNr1,
                   const ElementTransformation & trafo1, FlatArray<int> & ElVertices1,
                   const FiniteElement & fel2, int LocalFacetNr2,
                   const ElementTransformation & trafo2, FlatArray<int> & ElVertices2,
                   FlatMatrix<double> elmat,
                   LocalHeap & lh) const
  {
    elmat = 0.0;

    if (LocalFacetNr2==-1) throw Exception ("SymbolicFacetBFI: LocalFacetNr2==-1");

    int maxorder = max2 (fel1.Order(), fel2.Order());

    auto eltype1 = trafo1.GetElementType();
    auto eltype2 = trafo2.GetElementType();
    auto etfacet = ElementTopology::GetFacetType (eltype1, LocalFacetNr1);

    IntegrationRule ir_facet(etfacet, 2*maxorder);
    
    Facet2ElementTrafo transform1(eltype1, ElVertices1); 
    Facet2ElementTrafo transform2(eltype2, ElVertices2);
    
    IntegrationRule & ir_facet_vol1_tmp = transform1(LocalFacetNr1, ir_facet, lh);
    IntegrationRule & ir_facet_vol2_tmp = transform2(LocalFacetNr2, ir_facet, lh);

    IntegrationRule * ir_facet_vol1 = nullptr;
    IntegrationRule * ir_facet_vol2 = nullptr;

    
    if (time_order >= 0)
    {
      FlatVector<> st_point(3,lh);
      const IntegrationRule & ir_time = SelectIntegrationRule(ET_SEGM, time_order);

      auto ir_spacetime1 = new (lh) IntegrationRule (ir_facet_vol1_tmp.Size()*ir_time.Size(),lh);
      for (int i = 0; i < ir_time.Size(); i++)
      {
        for (int j = 0; j < ir_facet_vol1_tmp.Size(); j++)
        {
          const int ij = i*ir_facet_vol1_tmp.Size()+j;
          (*ir_spacetime1)[ij].SetWeight( ir_time[i].Weight() * ir_facet_vol1_tmp[j].Weight() );
          st_point = ir_facet_vol1_tmp[j].Point();
          (*ir_spacetime1)[ij].Point() = st_point;
          (*ir_spacetime1)[ij].SetWeight(ir_time[i](0));
          (*ir_spacetime1)[ij].SetPrecomputedGeometry(true);
        }
      }
      auto ir_spacetime2 = new (lh) IntegrationRule (ir_facet_vol2_tmp.Size()*ir_time.Size(),lh);
      for (int i = 0; i < ir_time.Size(); i++)
      {
        for (int j = 0; j < ir_facet_vol2_tmp.Size(); j++)
        {
          const int ij = i*ir_facet_vol2_tmp.Size()+j;
          (*ir_spacetime2)[ij].SetWeight( ir_time[i].Weight() * ir_facet_vol2_tmp[j].Weight() );
          st_point = ir_facet_vol2_tmp[j].Point();
          (*ir_spacetime2)[ij].Point() = st_point;
          (*ir_spacetime2)[ij].SetWeight(ir_time[i](0));
          (*ir_spacetime2)[ij].SetPrecomputedGeometry(true);
        }
      }
      ir_facet_vol1 = ir_spacetime1;
      ir_facet_vol2 = ir_spacetime2;
    }
    else
    {
      ir_facet_vol1 = &ir_facet_vol1_tmp;
      ir_facet_vol2 = &ir_facet_vol2_tmp;
    }
    
    BaseMappedIntegrationRule & mir1 = trafo1(*ir_facet_vol1, lh);
    BaseMappedIntegrationRule & mir2 = trafo2(*ir_facet_vol2, lh);

    ProxyUserData ud;
    const_cast<ElementTransformation&>(trafo1).userdata = &ud;

    for (int k1 : Range(trial_proxies))
      for (int l1 : Range(test_proxies))
        {
          HeapReset hr(lh);
          FlatMatrix<> val(mir1.Size(), 1,lh);
          
          auto proxy1 = trial_proxies[k1];
          auto proxy2 = test_proxies[l1];

          FlatTensor<3> proxyvalues(lh, mir1.Size(), proxy2->Dimension(), proxy1->Dimension());

          mir1.ComputeNormalsAndMeasure (eltype1, LocalFacetNr1);
          mir2.ComputeNormalsAndMeasure (eltype2, LocalFacetNr2);
          
          for (int k = 0; k < proxy1->Dimension(); k++)
            for (int l = 0; l < proxy2->Dimension(); l++)
              {
                ud.trialfunction = proxy1;
                ud.trial_comp = k;
                ud.testfunction = proxy2;
                ud.test_comp = l;
                
                cf -> Evaluate (mir1, val);
                proxyvalues(STAR,l,k) = val.Col(0);
              }

          for (int i = 0; i < mir1.Size(); i++)
            // proxyvalues(i,STAR,STAR) *= measure(i) * ir_facet[i].Weight();
            proxyvalues(i,STAR,STAR) *= mir1[i].GetMeasure() * ir_facet[i].Weight();

          IntRange trial_range  = proxy1->IsOther() ? IntRange(proxy1->Evaluator()->BlockDim()*fel1.GetNDof(), elmat.Width()) : IntRange(0, proxy1->Evaluator()->BlockDim()*fel1.GetNDof());
          IntRange test_range  = proxy2->IsOther() ? IntRange(proxy2->Evaluator()->BlockDim()*fel1.GetNDof(), elmat.Height()) : IntRange(0, proxy2->Evaluator()->BlockDim()*fel1.GetNDof());

          auto loc_elmat = elmat.Rows(test_range).Cols(trial_range);
          FlatMatrix<double,ColMajor> bmat1(proxy1->Dimension(), loc_elmat.Width(), lh);
          FlatMatrix<double,ColMajor> bmat2(proxy2->Dimension(), loc_elmat.Height(), lh);

          constexpr size_t BS = 16;
          for (size_t i = 0; i < mir1.Size(); i+=BS)
            {
              int rest = min2(size_t(BS), mir1.Size()-i);
              HeapReset hr(lh);
              FlatMatrix<double,ColMajor> bdbmat1(rest*proxy2->Dimension(), loc_elmat.Width(), lh);
              FlatMatrix<double,ColMajor> bbmat2(rest*proxy2->Dimension(), loc_elmat.Height(), lh);

              for (int j = 0; j < rest; j++)
                {
                  int ii = i+j;
                  IntRange r2 = proxy2->Dimension() * IntRange(j,j+1);
                  if (proxy1->IsOther())
                    proxy1->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat1, lh);
                  else
                    proxy1->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat1, lh);
                  
                  if (proxy2->IsOther())
                    proxy2->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat2, lh);
                  else
                    proxy2->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat2, lh);
                  
                  bdbmat1.Rows(r2) = proxyvalues(ii,STAR,STAR) * bmat1;
                  bbmat2.Rows(r2) = bmat2;
                }
              
              IntRange r1 = proxy1->Evaluator()->UsedDofs(proxy1->IsOther() ? fel2 : fel1);
              IntRange r2 = proxy2->Evaluator()->UsedDofs(proxy2->IsOther() ? fel2 : fel1);
              loc_elmat.Rows(r2).Cols(r1) += Trans (bbmat2.Cols(r2)) * bdbmat1.Cols(r1) | Lapack;
            }
        }
  }


  SymbolicFacetPatchBilinearFormIntegrator ::
  SymbolicFacetPatchBilinearFormIntegrator (shared_ptr<CoefficientFunction> acf,
                                            int aforce_intorder)
    : SymbolicFacetBilinearFormIntegrator(acf,VOL,false),
      force_intorder(aforce_intorder)
  {
    simd_evaluate=false;
  }

  // maps an integration point from inside one element to an integration point of the neighbor element
  // (integration point will be outside), so that the mapped points have the same coordinate
  template<int D>
  void MapPatchIntegrationPoint(IntegrationPoint & from_ip, const ElementTransformation & from_trafo,
                                const ElementTransformation & to_trafo, IntegrationPoint & to_ip,
                                LocalHeap & lh, bool spacetime_mode = false, double from_ip_weight =0.)
  {
    // cout << " ------------------------------------------- " << endl;
    const int max_its = 200;
    const double eps_acc = 1e-12;

    HeapReset hr(lh);

    FlatVector<double> vec(D,lh);
    FlatVector<double> diff(D,lh);
    FlatVector<double> update(D,lh);

    MappedIntegrationPoint<D,D> mip(from_ip, from_trafo);
    const double h = sqrt(mip.GetJacobiDet());

    IntegrationPoint * ip_x0 = new(lh) IntegrationPoint(0,0,0);
    IntegrationPoint * ip_x00 = new(lh) IntegrationPoint(0,0,0);
    vec = mip.GetPoint();
    double w00 = 0;
    double first_diffnorm = 0;

    {
      HeapReset hr(lh);
      auto ip_a0 = new (lh) IntegrationPoint(0,0,0);
      if(spacetime_mode) { ip_a0->SetWeight(from_ip.Weight()); ip_a0->SetPrecomputedGeometry(true); }
      auto mip_a0 = new (lh) MappedIntegrationPoint<D,D>(*ip_a0,to_trafo);
      FlatMatrix<double> A(D,D,lh);
      FlatMatrix<double> Ainv(D,D,lh);
      FlatVector<double> f(D,lh);
      f = vec - mip_a0->GetPoint();
      auto ip_ai = new (lh) IntegrationPoint(0.,0.,0.);
      for (int d = 0; d < D ;  d++)
      {
        FlatVector<double> xhat(D,lh);
        for (int di = 0; di < 3;  di++)
        {
          if (di == d)
            ip_ai->Point()[di] = 1;
          else
            ip_ai->Point()[di] = 0;
        }
        if(spacetime_mode) { ip_ai->SetWeight(from_ip.Weight()); ip_ai->SetPrecomputedGeometry(true); }
        auto mip_ai = new (lh) MappedIntegrationPoint<D,D>(*ip_ai,to_trafo);
        A.Col(d) = mip_ai->GetPoint() - mip_a0->GetPoint();
      }
      Ainv = Inv(A);
      w00 = abs(Det(A));
      ip_x00->Point().Range(0,D) = Ainv * f;
      ip_x0->Point().Range(0,D) = ip_x00->Point();
    }

    int its = 0;
    double w = 0;
    while (its==0 || (L2Norm(diff) > eps_acc*h && its < max_its))
    {
      if(spacetime_mode) { ip_x0->SetWeight(from_ip.Weight()); ip_x0->SetPrecomputedGeometry(true); }
      MappedIntegrationPoint<D,D> mip_x0(*ip_x0,to_trafo);
      diff = vec - mip_x0.GetPoint();
      if (its==0)
        first_diffnorm = L2Norm(diff);
      update = mip_x0.GetJacobianInverse() * diff;
      ip_x0->Point().Range(0,D) += update;
      its++;
      w = mip_x0.GetMeasure();
    }

    if(its >= max_its){
      cout << "MapPatchIntegrationPoint: Newton did not converge after "
           << its <<" iterations! (" << D <<"D)" << endl;
      cout << "taking a low order guess" << endl;
      cout << "diff = " << first_diffnorm << endl;
      to_ip = *ip_x00;
      if(spacetime_mode) to_ip.SetWeight(mip.GetMeasure() * from_ip_weight /w00);
      else to_ip.SetWeight(mip.GetWeight()/w00);
    }
    else
    {
      to_ip = *ip_x0;
      if(spacetime_mode) to_ip.SetWeight(mip.GetMeasure() * from_ip_weight /w);
      else to_ip.SetWeight(mip.GetWeight()/w);
    }
  }


  void SymbolicFacetPatchBilinearFormIntegrator ::
  CalcFacetMatrix (const FiniteElement & fel1, int LocalFacetNr1,
                   const ElementTransformation & trafo1, FlatArray<int> & ElVertices1,
                   const FiniteElement & fel2, int LocalFacetNr2,
                   const ElementTransformation & trafo2, FlatArray<int> & ElVertices2,
                   FlatMatrix<double> elmat,
                   LocalHeap & lh) const
  {
    elmat = 0.0;
    if (LocalFacetNr2==-1) throw Exception ("SymbolicFacetPatchBFI: LocalFacetNr2==-1");

    int D = trafo1.SpaceDim();
    int maxorder = max2 (fel1.Order(), fel2.Order());

    auto eltype1 = trafo1.GetElementType();
    auto eltype2 = trafo2.GetElementType();

    IntegrationRule ir_vol1(eltype1, 2*maxorder);
    IntegrationRule ir_vol2(eltype2, 2*maxorder);

    // cout << " ir_vol1 = " << ir_vol1 << endl;
    // cout << " ir_vol2 = " << ir_vol2 << endl;
    
    IntegrationRule ir_patch1 (ir_vol1.Size()+ir_vol2.Size(),lh);
    IntegrationRule ir_patch2 (ir_vol1.Size()+ir_vol2.Size(),lh);
    //In the non-space time case, the result of the mapping to the other element does not depend on the time
    //Therefore it is sufficient to do it once here.
    if(time_order == -1){
        for (int l = 0; l < ir_patch1.Size(); l++) {
            if (l<ir_vol1.Size()) {
                ir_patch1[l] = ir_vol1[l];
                if (D==2) MapPatchIntegrationPoint<2>(ir_patch1[l], trafo1, trafo2 ,ir_patch2[l], lh);
                else MapPatchIntegrationPoint<3>(ir_patch1[l], trafo1, trafo2 ,ir_patch2[l], lh);
            }
            else {
                ir_patch2[l] = ir_vol2[l - ir_vol1.Size()];
                if (D==2) MapPatchIntegrationPoint<2>(ir_patch2[l], trafo2, trafo1 ,ir_patch1[l], lh);
                else MapPatchIntegrationPoint<3>(ir_patch2[l], trafo2, trafo1 ,ir_patch1[l], lh);
            }
            ir_patch1[l].SetNr(l);
            ir_patch2[l].SetNr(l);
        }
    }

    // cout << " ir_patch1 = " << ir_patch1 << endl;
    // cout << " ir_patch2 = " << ir_patch2 << endl;
    
    IntegrationRule * ir1 = nullptr;
    IntegrationRule * ir2 = nullptr;

    Array<double> ir_st1_wei_arr;

    //Here we create approximately (up to the possible change in the element transformation due to the deformation)
    //tensor product quadrature rules for the space-time case
    if (time_order >= 0)
    {
      FlatVector<> st_point(3,lh);
      const IntegrationRule & ir_time = SelectIntegrationRule(ET_SEGM, time_order);

      auto ir_spacetime1 = new (lh) IntegrationRule (ir_patch1.Size()*ir_time.Size(),lh);
      ir_st1_wei_arr.SetSize(ir_spacetime1->Size());
      for (int i = 0; i < ir_time.Size(); i++)
      {
        double tval = ir_time[i](0);
        for (int j = 0; j < ir_patch1.Size(); j++)
        {
          const int ij = i*ir_patch1.Size()+j;

          //Task now: Calculate ir_patch1[j] in the spacetime setting at tval
          if(j< ir_vol1.Size()) ir_patch1[j] = ir_vol1[j];
          else {
            IntegrationPoint tmp = ir_vol2[j - ir_vol1.Size()];
            // ir_patch2[j] = ir_vol2[j - ir_vol1.Size()];
            double physical_weight = tmp.Weight();
            tmp.SetWeight(tval);
            tmp.SetPrecomputedGeometry(true);
            if (D==2) MapPatchIntegrationPoint<2>(tmp, trafo2, trafo1 ,ir_patch1[j], lh, true, physical_weight);
            else MapPatchIntegrationPoint<3>(tmp, trafo2, trafo1 ,ir_patch1[j], lh, true, physical_weight);
          }

          ir_st1_wei_arr[ij] = ir_time[i].Weight() * ir_patch1[j].Weight();

          st_point = ir_patch1[j].Point();
          (*ir_spacetime1)[ij].SetFacetNr(-1, VOL);
          (*ir_spacetime1)[ij].Point() = st_point;
          (*ir_spacetime1)[ij].SetWeight( tval);
          (*ir_spacetime1)[ij].SetPrecomputedGeometry(true);
          (*ir_spacetime1)[ij].SetNr(ij);
        }
      }
      auto ir_spacetime2 = new (lh) IntegrationRule (ir_patch2.Size()*ir_time.Size(),lh);
      for (int i = 0; i < ir_time.Size(); i++)
      {
        double tval = ir_time[i](0);
        for (int j = 0; j < ir_patch2.Size(); j++)
        {
          const int ij = i*ir_patch2.Size()+j;

          //Task now: Calculate ir_patch2[j] in the spacetime setting at tval
          if(j< ir_vol1.Size()) {
            IntegrationPoint tmp = ir_vol1[j];
            // ir_patch1[j] = ir_vol1[j];
            double physical_weight = tmp.Weight();
            tmp.SetWeight(tval);
            tmp.SetPrecomputedGeometry(true);
            if (D==2) MapPatchIntegrationPoint<2>(tmp, trafo1, trafo2 ,ir_patch2[j], lh, true, physical_weight);
            else MapPatchIntegrationPoint<3>(tmp, trafo1, trafo2 ,ir_patch2[j], lh, true, physical_weight);
          }
          else ir_patch2[j] = ir_vol2[j - ir_vol1.Size()];

          st_point = ir_patch2[j].Point();
          (*ir_spacetime2)[ij].SetFacetNr(-1, VOL);
          (*ir_spacetime2)[ij].Point() = st_point;
          (*ir_spacetime2)[ij].SetWeight(tval);
          (*ir_spacetime2)[ij].SetPrecomputedGeometry(true);
          (*ir_spacetime2)[ij].SetNr(ij);
        }
      }
      ir1 = ir_spacetime1;
      ir2 = ir_spacetime2;
      // cout << " *ir_spacetime1 = " << *ir_spacetime1 << endl;
      // cout << " *ir_spacetime2 = " << *ir_spacetime2 << endl;
    }
    else
    {
      ir1 = &ir_patch1;
      ir2 = &ir_patch2;
    }


    // getchar();
    
    BaseMappedIntegrationRule & mir1 = trafo1(*ir1, lh);
    BaseMappedIntegrationRule & mir2 = trafo2(*ir2, lh);

    ProxyUserData ud;
    const_cast<ElementTransformation&>(trafo1).userdata = &ud;

    for (int k1 : Range(trial_proxies))
      for (int l1 : Range(test_proxies))
        {
          HeapReset hr(lh);
          FlatMatrix<> val(mir1.Size(), 1,lh);
          
          auto proxy1 = trial_proxies[k1];
          auto proxy2 = test_proxies[l1];

          FlatTensor<3> proxyvalues(lh, mir1.Size(), proxy2->Dimension(), proxy1->Dimension());

          // mir1.ComputeNormalsAndMeasure (eltype1, LocalFacetNr1);
          // mir2.ComputeNormalsAndMeasure (eltype2, LocalFacetNr2);
          
          for (int k = 0; k < proxy1->Dimension(); k++)
            for (int l = 0; l < proxy2->Dimension(); l++)
              {
                ud.trialfunction = proxy1;
                ud.trial_comp = k;
                ud.testfunction = proxy2;
                ud.test_comp = l;
                
                cf -> Evaluate (mir1, val);
                proxyvalues(STAR,l,k) = val.Col(0);
              }

          for (int i = 0; i < mir1.Size(); i++){
            // proxyvalues(i,STAR,STAR) *= measure(i) * ir_facet[i].Weight();
            // proxyvalues(i,STAR,STAR) *= mir1[i].GetMeasure() * ir_facet[i].Weight();
            if(time_order >=0) proxyvalues(i,STAR,STAR) *= mir1[i].GetMeasure()*ir_st1_wei_arr[i];
            else proxyvalues(i,STAR,STAR) *= mir1[i].GetWeight();
          }

          IntRange trial_range  = proxy1->IsOther() ? IntRange(proxy1->Evaluator()->BlockDim()*fel1.GetNDof(), elmat.Width()) : IntRange(0, proxy1->Evaluator()->BlockDim()*fel1.GetNDof());
          IntRange test_range  = proxy2->IsOther() ? IntRange(proxy2->Evaluator()->BlockDim()*fel1.GetNDof(), elmat.Height()) : IntRange(0, proxy2->Evaluator()->BlockDim()*fel1.GetNDof());

          auto loc_elmat = elmat.Rows(test_range).Cols(trial_range);
          FlatMatrix<double,ColMajor> bmat1(proxy1->Dimension(), loc_elmat.Width(), lh);
          FlatMatrix<double,ColMajor> bmat2(proxy2->Dimension(), loc_elmat.Height(), lh);

          constexpr size_t BS = 16;
          for (size_t i = 0; i < mir1.Size(); i+=BS)
            {
              int rest = min2(size_t(BS), mir1.Size()-i);
              HeapReset hr(lh);
              FlatMatrix<double,ColMajor> bdbmat1(rest*proxy2->Dimension(), loc_elmat.Width(), lh);
              FlatMatrix<double,ColMajor> bbmat2(rest*proxy2->Dimension(), loc_elmat.Height(), lh);

              for (int j = 0; j < rest; j++)
                {
                  int ii = i+j;
                  IntRange r2 = proxy2->Dimension() * IntRange(j,j+1);
                  if (proxy1->IsOther())
                    proxy1->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat1, lh);
                  else
                    proxy1->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat1, lh);
                  
                  if (proxy2->IsOther())
                    proxy2->Evaluator()->CalcMatrix(fel2, mir2[ii], bmat2, lh);
                  else
                    proxy2->Evaluator()->CalcMatrix(fel1, mir1[ii], bmat2, lh);
                  
                  bdbmat1.Rows(r2) = proxyvalues(ii,STAR,STAR) * bmat1;
                  bbmat2.Rows(r2) = bmat2;
                }
              
              IntRange r1 = proxy1->Evaluator()->UsedDofs(proxy1->IsOther() ? fel2 : fel1);
              IntRange r2 = proxy2->Evaluator()->UsedDofs(proxy2->IsOther() ? fel2 : fel1);
              loc_elmat.Rows(r2).Cols(r1) += Trans (bbmat2.Cols(r2)) * bdbmat1.Cols(r1) | Lapack;
            }
        }
  }

}
