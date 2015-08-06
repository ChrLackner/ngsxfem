/*********************************************************************/
/* File:   vtkoutput.hpp                                             */
/* Author: Christoph Lehrenfeld                                      */
/* Date:   1. June 2014                                              */
/*********************************************************************/

#include <solve.hpp>

using namespace ngsolve;
using namespace ngfem;

namespace ngcomp
{ 

  class ValueField : public Array<double>
  {
    int dim = 1;
    string name = "none";
  public:
    ValueField(){;};
    ValueField(int adim, string aname);
    void SetDimension(int adim){ dim = adim; }
    int Dimension(){ return dim;}
    void SetName(string aname){ name = aname; }
    string Name(){ return name;}
  };
  
/* ---------------------------------------- 
   numproc
   ---------------------------------------- */

  class BaseVTKOutput
  {
  public:
    virtual void Do (LocalHeap & lh) = 0;
  };
  
  template <int D> 
  class VTKOutput : public BaseVTKOutput
  {
  protected:

    shared_ptr<MeshAccess> ma = nullptr;
    Array<shared_ptr<CoefficientFunction>> coefs;
    Array<string> fieldnames;
    string filename;
    int subdivision;

    Array<shared_ptr<ValueField>> value_field;
    Array<Vec<D>> points;
    Array<INT<D+1>> cells;

    shared_ptr<ofstream> fileout;
    
  public:

    VTKOutput (const Array<shared_ptr<CoefficientFunction>> &,
               const Flags &,shared_ptr<MeshAccess>);

    VTKOutput (shared_ptr<MeshAccess>, const Array<shared_ptr<CoefficientFunction>> &,
               const Array<string> &, string, int);
    
    void ResetArrays();
    
    void FillReferenceData2D(Array<IntegrationPoint> & ref_coords, Array<INT<D+1>> & ref_trigs);    
    void FillReferenceData3D(Array<IntegrationPoint> & ref_coords, Array<INT<D+1>> & ref_tets);
    void PrintPoints();
    void PrintCells();
    void PrintCellTypes();
    void PrintFieldData();    

    virtual void Do (LocalHeap & lh);
  };


  class NumProcVTKOutput : public NumProc
  {
  protected:
    shared_ptr<BaseVTKOutput> vtkout = nullptr;
  public:
    NumProcVTKOutput (shared_ptr<PDE> apde, const Flags & flags);
    virtual ~NumProcVTKOutput() { }

    virtual string GetClassName () const
    {
      return "NumProcVTKOutput";
    }
    
    virtual void Do (LocalHeap & lh);
  };
  
}
