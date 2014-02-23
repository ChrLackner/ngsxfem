
/// from ngxfem
#include "xfiniteelement.hpp"

namespace ngfem
{

  XDummyFE::XDummyFE (DOMAIN_TYPE a_sign, ELEMENT_TYPE a_et)
    : // FiniteElement(),
    sign(a_sign), et(a_et) { ndof = 0; }

  XFiniteElement::XFiniteElement(const FiniteElement & a_base, const Array<DOMAIN_TYPE>& a_localsigns, 
                                 const XLocalGeometryInformation* a_localgeom,
                                 FlatXLocalGeometryInformation a_fxgeom,
                                 LocalHeap & lh)
    : base(a_base), 
      localsigns(a_localsigns.Size(),lh), 
      localgeom(a_localgeom),
      fxgeom(a_fxgeom)
  { 
    ndof = base.GetNDof(); 
    for (int l = 0; l < localsigns.Size(); ++l)
      localsigns[l] = a_localsigns[l];
  };


  XFiniteElement::~XFiniteElement() { ; };

  /// the name
  string XFiniteElement::ClassName(void) const {return "X-"+base.ClassName();};

  const FlatArray<DOMAIN_TYPE>& XFiniteElement::GetSignsOfDof() const  
  {
    return localsigns;
  };

  const XLocalGeometryInformation* XFiniteElement::GetLocalGeometry() const
  {
    return localgeom;
  };

  // template class XDummyFE<1>;
  // template class XDummyFE<2>;
  // template class XDummyFE<3>;

  // template class XFiniteElement<1>;
  // template class XFiniteElement<2>;
  // template class XFiniteElement<3>;


} // end of namespace
