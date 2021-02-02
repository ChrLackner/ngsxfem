"""
In this example we solve a scalar *unfitted* interface problem with a
similar discretisation method to the one used in cutfem.py and nxfem.py.
However, we use a higher order method this time.

Implementational aspects:
-------------------------
* Geometry approximation: To approximate the implicitly described
  geometry we use the piecewise (multi-) linear interpolation of the
  level set function as a basis. For this geometry approximation
  (arbitrary order) accurate numerical integration is provided.
  To improve this domain approximation (which is only second order
  accurate) we apply an additional mesh transformation as in [1] which
  improves the geometrically accuracy. Integrals are (automatically)
  transformed to the basis configuration where the numerical integration
  is applied.

Extensions:
-----------
* A Stokes interface problem based on similar techniques and a
  Taylor-Hood discretisation is used in stokescutfem.py
* To be robust w.r.t. high contrasts we could apply a edge-based
  stabilization. In fictdom_ghostpen.py this stabilization is used for
  a one-domain (higher order) problem.

Literature:
-----------
[1] C. Lehrenfeld, High order unfitted finite element methods on level
    set domains using isoparametric mappings, Comp. Meth. Appl. Mech.
    Eng., 300:716-733, 2016.
"""

# ------------------------------ LOAD LIBRARIES -------------------------------
from netgen.geom2d import SplineGeometry
from ngsolve import *
from ngsolve.internal import *
from xfem import *
from xfem.lsetcurv import *

from math import pi


# -------------------------------- PARAMETERS ---------------------------------
# Domain corners
ll, ur = (-1.5, -1.5), (1.5, 1.5)
# mesh size
maxh = 0.2
# Finite element order
order = 2
# diffusion coefficients for the sub-domains (NEG/POS):
alpha = [1.0, 2.0]
# Nitsche penalty parameter
lambda_nitsche = 10


# ----------------------------------- MAIN ------------------------------------
# We generate the background mesh of the domain and use a simplicial
# triangulation to obtain a mesh with quadrilaterals use
# 'quad_dominated=True'.
square = SplineGeometry()
square.AddRectangle(ll, ur, bc=1)
mesh = Mesh(square.GenerateMesh(maxh=maxh, quad_dominated=False))

# manufactured solution and corresponding r.h.s. data CoefficientFunctions:
r44 = (x**4 + y**4)
r41 = sqrt(sqrt(x**4 + y**4))
r4m3 = (1.0 / (r41 * r41 * r41))
r66 = (x**6 + y**6)
r63 = sqrt(r66)
r22 = (x**2 + y**2)
r21 = sqrt(r22)
solution = [1 + pi / 2 - sqrt(2.0) * cos(pi / 4 * r44), pi / 2 * r41]
coef_f = [(-1 * sqrt(2) * pi * (pi * cos(pi / 4 * (r44)) * (r66)
                                + 3 * sin(pi / 4 * (r44)) * (r22))),
          (-2 * pi * 3 / 2 * (r4m3) * (-(r66) / (r44) + (r22)))]


# level set function of the domain (phi = ||x||_4 - 1):
levelset = (sqrt(sqrt(x**4 + y**4)) - 1.0)


# class to compute the mesh transformation needed for higher order accuracy
#  * order: order of the mesh deformation function
#  * threshold: barrier for maximum deformation (to ensure shape regularity)
lsetmeshadap = LevelSetMeshAdaptation(mesh, order=order, threshold=1000,
                                      discontinuous_qn=True)

# compute the mesh transformation (applied later)
deformation = lsetmeshadap.CalcDeformation(levelset)

# the piecewise linear interpolation used in the level set adaptation class
lsetp1 = lsetmeshadap.lset_p1

# Gathering information on cut elements:
#  * domain of (volume/boundary) element:
#    * NEG= only negative level set values
#    * POS= only positive level set values
#    * IF= cut element (negative and positive) level set values
#  * cut ratio:
#    If element is cut this describes the ratio between the measure of
#    part in the negative domain and the measure of the full element.
ci = CutInfo(mesh, lsetp1)

# extended FESpace

Vh = H1(mesh, order=order, dirichlet=[1, 2, 3, 4])
Vhx = XFESpace(Vh, lsetp1)
VhG = FESpace([Vh, Vhx])

# coefficients / parameters:
n = 1.0 / grad(lsetp1).Norm() * grad(lsetp1)
h = specialcf.mesh_size

# the cut ratio extracted from the cutinfo-class
kappa = (CutRatioGF(ci), 1.0 - CutRatioGF(ci))
# Nitsche stabilization parameter:
stab = lambda_nitsche * (alpha[1] + alpha[0]) * (order + 1) * order / h

# expressions of test and trial functions:

u_std, u_x = VhG.TrialFunction()
v_std, v_x = VhG.TestFunction()

u = [u_std + op(u_x) for op in [neg, pos]]
v = [v_std + op(v_x) for op in [neg, pos]]

gradu = [grad(u_std) + op(u_x) for op in [neg_grad, pos_grad]]
gradv = [grad(v_std) + op(v_x) for op in [neg_grad, pos_grad]]

average_flux_u = sum([- kappa[i] * alpha[i] * gradu[i] * n for i in [0, 1]])
average_flux_v = sum([- kappa[i] * alpha[i] * gradv[i] * n for i in [0, 1]])

# Integration domains for integration on negative/positive sub-domains
# and on the interface: Here, the integration is (geometrically) exact
# if the "levelset"-argument is a piecewise (multi-)linear function.
# The integration order is chosen according to the arguments in the
# multi-linear forms (but can be overwritten with "force_intorder" in
# the integration domain). If the "levelset"-argument is not a
# (multi-)linear function, you can use the "subdivlvl" argument to add
# additional refinement levels for the geometry approximation.
lset_neg = {"levelset": lsetp1, "domain_type": NEG, "subdivlvl": 0}
lset_pos = {"levelset": lsetp1, "domain_type": POS, "subdivlvl": 0}
lset_if = {"levelset": lsetp1, "domain_type": IF, "subdivlvl": 0}

# bilinear forms:

a = BilinearForm(VhG, symmetric=True)
# l.h.s. domain integrals:
a += SymbolicBFI(levelset_domain=lset_neg, form=alpha[0] * gradu[0] * gradv[0])
a += SymbolicBFI(levelset_domain=lset_pos, form=alpha[1] * gradu[1] * gradv[1])
# Nitsche integrals:
a += SymbolicBFI(levelset_domain=lset_if, form=average_flux_u * (v[0] - v[1])
                 + average_flux_v * (u[0] - u[1])
                 + stab * (u[0] - u[1]) * (v[0] - v[1]))

f = LinearForm(VhG)
# r.h.s. domain integrals:
f += SymbolicLFI(levelset_domain=lset_neg, form=coef_f[0] * v[0])
f += SymbolicLFI(levelset_domain=lset_pos, form=coef_f[1] * v[1])

# solution vector
gfu = GridFunction(VhG)

# setting domain boundary conditions:
gfu.components[0].Set(solution[1], BND)

# activate the mesh deformation (changes the integrals via corresponding
# transformation):
mesh.SetDeformation(deformation)

# setting up matrix and vector
a.Assemble()
f.Assemble()

# homogenization of boundary data and solution of linear system
rhs = gfu.vec.CreateVector()
rhs.data = f.vec - a.mat * gfu.vec
update = gfu.vec.CreateVector()
update.data = a.mat.Inverse(VhG.FreeDofs()) * rhs
gfu.vec.data += update

# visualization of (discrete) solution: Wherever (interpolated) level
# set function is negative visualize the first component, where it is
# positive visualize the second component
u_coef = gfu.components[0] + \
    IfPos(lsetp1, pos(gfu.components[1]), neg(gfu.components[1]))
u = [gfu.components[0] + op(gfu.components[1]) for op in [neg, pos]]

# visualize levelset, interpolated levelset and discrete solution:
# (Note that the visualization does not respect the discontinuities.
# They are smeared out. To see kinks or jumps more clearly increase the
# subdivision option in the visualization.) drawing deformation allows
# to deform the mesh to obtain the geometry that is used in the
# computations:
Draw(levelset, mesh, "levelset")
Draw(lsetp1, mesh, "lsetp1")
Draw(lsetmeshadap.deform, mesh, "deformation")
Draw(u_coef, mesh, "u")
Draw(CoefficientFunction((lsetmeshadap.deform[0],
                          lsetmeshadap.deform[1], u_coef)),
     mesh, "graph_of_u", sd=3)
visoptions.deformation = 1

err_sqr_coefs = [(u[i] - solution[i]) * (u[i] - solution[i]) for i in [0, 1]]

l2error = sqrt(Integrate(levelset_domain=lset_neg, cf=err_sqr_coefs[0],
                         mesh=mesh, order=2 * order, heapsize=1000000)
               + Integrate(levelset_domain=lset_pos, cf=err_sqr_coefs[1],
                           mesh=mesh, order=2 * order, heapsize=1000000))

print("L2 error : ", l2error)
# for later refinements etc. its always a good idea to deactivate the
# mesh deformation
mesh.UnsetDeformation()
