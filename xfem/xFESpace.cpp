#include "xFESpace.hpp"
#include "xfemdiffops.hpp"
using namespace ngsolve;
using namespace ngfem;

namespace ngcomp
{
  void XFESpace :: CleanUp ()
  {
    //empty
  }



  void XFESpace :: GetDofNrs (ElementId ei, Array<int> & dnums) const
  {
    if ( cutinfo
         && (cutinfo->GetElementsOfDomainType(IF,ei.VB())->Size() > 0
             && cutinfo->GetElementsOfDomainType(IF,ei.VB())->Test(ei.Nr())) )
    {
      if (ei.VB() == VOL)
        dnums = (*el2dofs)[ei.Nr()];
      else
        dnums = (*sel2dofs)[ei.Nr()];
    }
    else
      dnums.SetSize(0);
  }

  void XFESpace :: GetDomainNrs (ElementId ei, Array<DOMAIN_TYPE> & domnums) const
  {
    if ( cutinfo
         && (cutinfo->GetElementsOfDomainType(IF,ei.VB())->Size() > 0
             && cutinfo->GetElementsOfDomainType(IF,ei.VB())->Test(ei.Nr())) )
    {
      if (ei.VB() == VOL)
      {
        FlatArray<int> dofs = (*el2dofs)[ei.Nr()];
        domnums.SetSize(dofs.Size());
        for (int i = 0; i < dofs.Size(); ++i)
          domnums[i] = domofdof[dofs[i]];
      }
      else
      {
        FlatArray<int> dofs = (*sel2dofs)[ei.Nr()];
        domnums.SetSize(dofs.Size());
        for (int i = 0; i < dofs.Size(); ++i)
          domnums[i] = domofdof[dofs[i]];
      }
    }
    else
      domnums.SetSize(0);
  }

  void XFESpace :: UpdateCouplingDofArray()
  {
    ctofdof.SetSize(ndof);
    ctofdof = WIREBASKET_DOF;

    for (int i = 0; i < basedof2xdof.Size(); ++i)
    {
      const int dof = basedof2xdof[i];
      if (dof != -1)
      {
        // if (trace)
        ctofdof[dof] = basefes->GetDofCouplingType(i); //INTERFACE_DOF; //
        // else
        //   ctofdof[dof] = INTERFACE_DOF; //
      }
    }

    if (trace && ma->GetDimension() == 3)
    // face bubbles on the outer part of the band will be local dofs... (for static cond.)
    {
      for (int facnr = 0; facnr < ma->GetNFaces(); ++facnr)
      {
        Array<int> elnums;
        ma->GetFaceElements (facnr, elnums);
        int cutels = 0;
        for (auto elnr : elnums)
        {
          if (cutinfo->GetElementsOfDomainType(IF,VOL)->Test(elnr))
            cutels++;
        }
        if (cutels<2)
        {
          Array<int> facedofs;
          basefes->GetFaceDofNrs (facnr, facedofs);
          for (auto basedof : facedofs)
          {
            const int dof = basedof2xdof[basedof];
            if (dof != -1)
              ctofdof[dof] = LOCAL_DOF;
          }
        }
      }
    }
    *testout << "XFESpace, ctofdof = " << endl << ctofdof << endl;
    // cout << "XFESpace, ctofdof = " << endl << ctofdof << endl;
    // getchar();

  }


  void XFESpace::XToNegPos(shared_ptr<GridFunction> gf, shared_ptr<GridFunction> gf_neg_pos)
  {
    shared_ptr<GridFunction> gf_neg = gf_neg_pos->GetComponent(0);
    BaseVector & bv_neg = gf_neg->GetVector();
    FlatVector<> vneg = bv_neg.FVDouble();

    shared_ptr<GridFunction> gf_pos = gf_neg_pos->GetComponent(1);
    BaseVector & bv_pos = gf_pos->GetVector();
    FlatVector<> vpos = bv_pos.FVDouble();

    shared_ptr<GridFunction> gf_base = gf->GetComponent(0);
    BaseVector & bv_base = gf_base->GetVector();
    FlatVector<> vbase = bv_base.FVDouble();

    shared_ptr<GridFunction> gf_x = gf->GetComponent(1);
    BaseVector & bv_x = gf_x->GetVector();
    FlatVector<> vx = bv_x.FVDouble();

    auto xstdfes = dynamic_pointer_cast<CompoundFESpace>(gf->GetFESpace());
    if (!xstdfes)
      throw Exception("cast failed: not a CompoundFESpace");
    auto xfes = dynamic_pointer_cast<XFESpace>((*xstdfes)[1]);
    if (!xfes)
      throw Exception("cast failed: not an XFESpace");

    const int basendof = vneg.Size();
    for (int i = 0; i < basendof; ++i)
    {
      vneg(i) = vbase(i);
      vpos(i) = vbase(i);
      const int xdof = xfes->GetXDofOfBaseDof(i);
      if (xdof != -1)
      {
        if (xfes->GetDomOfDof(xdof) == POS)
          vpos(i) += vx(xdof);
        else
          vneg(i) += vx(xdof);
      }
    }
  }


  template <int D>
  T_XFESpace<D> :: T_XFESpace (shared_ptr<MeshAccess> ama, shared_ptr<FESpace> basefes,
                               shared_ptr<CutInformation> cutinfo, const Flags & flags)
    : XFESpace (ama, basefes, cutinfo, flags)
  {
    if (flags.GetDefineFlag("trace"))
      trace = true;
    evaluator[VOL] = make_shared<T_DifferentialOperator<DiffOpX<D,DIFFOPX::EXTEND>>>();
    flux_evaluator[VOL] = make_shared<T_DifferentialOperator<DiffOpX<D,DIFFOPX::EXTEND_GRAD>>>();

    private_cutinfo = false;
  }

  template <int D>
  T_XFESpace<D> :: T_XFESpace (shared_ptr<MeshAccess> ama, shared_ptr<FESpace> basefes,
                               shared_ptr<CoefficientFunction> lset, const Flags & flags)
    : XFESpace (ama, basefes, lset, flags)
  {
    if (flags.GetDefineFlag("trace"))
      trace = true;
    evaluator[VOL] = make_shared<T_DifferentialOperator<DiffOpX<D,DIFFOPX::EXTEND>>>();
    flux_evaluator[VOL] = make_shared<T_DifferentialOperator<DiffOpX<D,DIFFOPX::EXTEND_GRAD>>>();

    private_cutinfo = true;
    coef_lset = lset;
    cutinfo = make_shared<CutInformation>(ma);
  }

  template <int D>
  SymbolTable<shared_ptr<DifferentialOperator>>
  T_XFESpace<D> :: GetAdditionalEvaluators () const
  {
    SymbolTable<shared_ptr<DifferentialOperator>> additional;
    switch (ma->GetDimension())
    {
    case 1 :
      throw Exception("dim==1 not implemented"); break;
    case 2 :
      additional.Set ("extend", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::EXTEND>>> ());
      additional.Set ("pos", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::RPOS>>> ());
      additional.Set ("neg", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::RNEG>>> ());
      additional.Set ("extendgrad", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::EXTEND_GRAD>>> ());
      additional.Set ("posgrad", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::RPOS_GRAD>>> ());
      additional.Set ("neggrad", make_shared<T_DifferentialOperator<DiffOpX<2,DIFFOPX::RNEG_GRAD>>> ()); break;
    case 3 :
      additional.Set ("extend", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::EXTEND>>> ());
      additional.Set ("pos", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::RPOS>>> ());
      additional.Set ("neg", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::RNEG>>> ());
      additional.Set ("extendgrad", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::EXTEND_GRAD>>> ());
      additional.Set ("posgrad", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::RPOS_GRAD>>> ());
      additional.Set ("neggrad", make_shared<T_DifferentialOperator<DiffOpX<3,DIFFOPX::RNEG_GRAD>>> ()); break;
    default :
      ;
    }
    return additional;
  }


  template <int D>
  T_XFESpace<D> :: ~T_XFESpace ()
  {
    CleanUp();
    // if (eval_lset) delete eval_lset;
  }


  void IterateRange (int ne, LocalHeap & clh,
                     const function<void(int,LocalHeap&)> & func)
  {
#ifndef WIN32
    if (task_manager)
    {
      SharedLoop2 sl(ne);
      task_manager -> CreateJob
        ( [&] (const TaskInfo & ti)
      {
        LocalHeap lh = clh.Split(ti.thread_nr, ti.nthreads);
        for (int elnr : sl)
        {
          HeapReset hr(lh);
          func (elnr,lh);
        }

      } );
    }
    else
#endif // WIN32
    {
      for (int elnr = 0; elnr < ne; elnr++)
      {
        HeapReset hr(clh);
        func (elnr,clh);
      }
    }
  }

  template <int D>
  void T_XFESpace<D> :: Update()
  {
    CleanUp();

    if (private_cutinfo)
    {
      cout << IM(4) << " Calling cutinfo-Update from within XFESpace-Update " << endl;
      LocalHeapMem<100000> lh("T_XFESpace<D>::Update(private_cutinfo)");
      cutinfo->Update(coef_lset,-1,lh);
    }

    static Timer timer ("XFESpace::Update");
    RegionTimer reg (timer);

    FESpace::Update();

    int ne=ma->GetNE();
    int nedges=ma->GetNEdges();
    int nf=ma->GetNFaces();
    int nv=ma->GetNV();
    int nse=ma->GetNSE();

    BitArray activedofs(basefes->GetNDof());
    activedofs.Clear();

    static int first = -1;
    first++;

    Array<double> kappa_pos(ne);
    BitArray element_most_pos(ne);
    element_most_pos.Clear();

    for ( VorB vb : {VOL,BND})
    {
      int ne = ma->GetNE(vb);

      TableCreator<int> creator;
      for (; !creator.Done(); creator++)
      {
        for (int elnr = 0; elnr < ne; ++elnr)
        {
          if (! cutinfo->GetElementsOfDomainType(IF,vb)->Test(elnr))
            continue;
          Array<int> basednums;
          basefes->GetDofNrs(ElementId(vb,elnr),basednums);
          for (int k = 0; k < basednums.Size(); ++k)
          {
            activedofs.Set(basednums[k]);
            creator.Add(elnr,basednums[k]);
          }
        }
      }
      if (vb == VOL)
        el2dofs = make_shared<Table<int>>(creator.MoveTable());
      else
        sel2dofs = make_shared<Table<int>>(creator.MoveTable());
    }

    int nbdofs = basefes->GetNDof();
    basedof2xdof.SetSize(nbdofs);
    basedof2xdof = -1;
    ndof = 0;

    for (int i = 0; i < nbdofs; i++)
    {
      if (activedofs.Test(i))
        basedof2xdof[i] = ndof++;
    }

    xdof2basedof.SetSize(ndof);
    xdof2basedof = -1;

    ndof = 0;
    for (int i = 0; i < nbdofs; i++)
    {
      if (activedofs.Test(i))
        xdof2basedof[ndof++] = i;
    }

    for (int i = 0; i < ne; ++i)
    {
      if (cutinfo->GetElementsOfDomainType(IF,VOL)->Test(i))
      {
        FlatArray<int> dofs = (*el2dofs)[i];
        for (int j = 0; j < (*el2dofs)[i].Size(); ++j)
          (*el2dofs)[i][j] = basedof2xdof[dofs[j] ];
      }
    }

    for (int i = 0; i < nse; ++i)
    {
      if (cutinfo->GetElementsOfDomainType(IF,BND)->Test(i))
      {
        FlatArray<int> dofs = (*sel2dofs)[i];
        for (int j = 0; j < (*sel2dofs)[i].Size(); ++j)
          (*sel2dofs)[i][j] = basedof2xdof[dofs[j] ];
      }
    }

    *testout << " x ndof : " << ndof << endl;

    // domain of dof
    domofdof.SetSize(ndof);
    domofdof = NEG;

    Array<int> dnums;
    for (NODE_TYPE nt : {NT_CELL,NT_FACE,NT_EDGE,NT_VERTEX})
    {
      for (int nnr : ma->Nodes(nt))
      {
        DOMAIN_TYPE dt = (*cutinfo->dom_of_node[nt])[nnr];
        if (dt != IF)
        {
          basefes->GetDofNrs(NodeId(nt,nnr), dnums);
          for (int l = 0; l < dnums.Size(); ++l)
          {
            int xdof = basedof2xdof[dnums[l]];
            if ( xdof != -1)
              domofdof[xdof] = INVERT(dt);
          }
        }
      }
    }

    BitArray dofs_with_cut_on_boundary(GetNDof());
    dofs_with_cut_on_boundary.Clear();

    for (int selnr = 0; selnr < nse; ++selnr)
    {
      ElementId ei(BND,selnr);
      DOMAIN_TYPE dt = cutinfo->DomainTypeOfElement(ei);
      if (dt!=IF) continue;

      Array<int> dnums;
      GetDofNrs(ei, dnums);

      for (int i = 0; i < dnums.Size(); ++i)
      {
        const int xdof = dnums[i];
        dofs_with_cut_on_boundary.Set(xdof);
      }
    }

    UpdateCouplingDofArray();
    FinalizeUpdate ();

    dirichlet_dofs.SetSize (GetNDof());
    dirichlet_dofs.Clear();

    for (int i = 0; i < basedof2xdof.Size(); ++i)
    {
      const int dof = basedof2xdof[i];
      if (dof != -1 && basefes->IsDirichletDof(i))
        if (dofs_with_cut_on_boundary.Test(dof))
          dirichlet_dofs.Set (dof);
    }

    free_dofs->SetSize (GetNDof());
    *free_dofs = dirichlet_dofs;
    free_dofs->Invert();

    *testout << "ndof = " << ndof << endl;
    *testout << "basedof2xdof = " << basedof2xdof << endl;
    *testout << "el2dofs = " << *el2dofs << endl;
    *testout << "sel2dofs = " << *sel2dofs << endl;
    *testout << "domain of dofs = " << domofdof << endl;

    *testout << " basefes = " << basefes << endl;
    *testout << "basefes -> free_dofs = " << basefes->GetFreeDofs() << endl;
    if (basefes->GetFreeDofs())
      *testout << "*(basefes -> free_dofs) = " << *(basefes->GetFreeDofs()) << endl;
    *testout << "free_dofs = " << *free_dofs << endl;
  }

  template <int D>
  FiniteElement & T_XFESpace<D> :: GetFE (ElementId ei, Allocator & alloc) const
  {
    if (cutinfo->GetElementsOfDomainType(IF,ei.VB())->Test(ei.Nr()))
    {
      Array<DOMAIN_TYPE> domnrs;
      GetDomainNrs(ei,domnrs);
      return *(new (alloc) XFiniteElement(basefes->GetFE(ei,alloc),domnrs,alloc));
    }
    else
    {
      DOMAIN_TYPE dt = cutinfo->DomainTypeOfElement(ei);
      Ngs_Element ngsel = ma->GetElement(ei);
      ELEMENT_TYPE eltype = ngsel.GetType();
      return *(new (alloc) XDummyFE(dt,eltype));
    }
  }

  template class T_XFESpace<2>;
  template class T_XFESpace<3>;

}

