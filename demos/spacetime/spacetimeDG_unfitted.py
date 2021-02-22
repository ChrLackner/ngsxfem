"""
unfitted Heat equation with Neumann b.c. solved with an unfitted isoparametric
space-time discretisation.
"""

# ------------------------------ LOAD LIBRARIES -------------------------------
from ngsolve import *
from netgen.geom2d import SplineGeometry
from xfem import *
from math import pi
from xfem.lset_spacetime import *
ngsglobals.msg_level = 1

# -------------------------------- PARAMETERS ---------------------------------

# DISCRETIZATION PARAMETERS:
# parameter for refinement study:
i = 2
n_steps = 2**i
space_refs = i

# polynomial order in time
k_t = 2
# polynomial order in space
k_s = k_t
# polynomial order in time for level set approximation
lset_order_time = k_t
# integration order in time
time_order = 2 * k_t
# time stepping parameters
tstart = 0
tend = 0.5
delta_t = (tend - tstart) / n_steps
maxh = 0.5
# ghost penalty parameter
gamma = 0.05
# map from reference time to physical time
told = Parameter(tstart)
t = told + delta_t * tref

# PROBLEM SETUP:

# outer domain:
rect = SplineGeometry()
rect.AddRectangle([-0.6, -1], [0.6, 1])

# level set geometry
# radius of disk (the geometry)
R = 0.5
# position shift of the geometry in time
rho = (1 / (pi)) * sin(2 * pi * t)
# convection velocity:
w = CoefficientFunction((0, rho.Diff(t)))
# level set
r = sqrt(x**2 + (y - rho)**2)
levelset = r - R

# diffusion coeff
alpha = 1
# solution
u_exact = cos(pi * r / R) * sin(pi * t)
# r.h.s.
coeff_f = (u_exact.Diff(t)
           - alpha * (u_exact.Diff(x).Diff(x) + u_exact.Diff(y).Diff(y))
           + w[0] * u_exact.Diff(x) + w[1] * u_exact.Diff(y)).Compile()

# ----------------------------------- MAIN ------------------------------------

ngmesh = rect.GenerateMesh(maxh=maxh, quad_dominated=False)
for j in range(space_refs):
    ngmesh.Refine()
mesh = Mesh(ngmesh)

# spatial FESpace for solution
fes1 = H1(mesh, order=k_s, dgjumps=True)
# time finite element (nodal!)
tfe = ScalarTimeFE(k_t)
# (tensor product) space-time finite element space 
st_fes = tfe * fes1

# Space time version of Levelset Mesh Adapation object. Also offers integrator
# helper functions that involve the correct mesh deformation
lsetadap = LevelSetMeshAdaptation_Spacetime(mesh, order_space=k_s,
                                            order_time=lset_order_time,
                                            threshold=0.5,
                                            discontinuous_qn=True)

gfu = GridFunction(st_fes)
u_last = CreateTimeRestrictedGF(gfu, 1)

scene = DrawDC(lsetadap.levelsetp1[TOP], u_last, 0, mesh, "u_last",
               deformation=lsetadap.deformation[TOP])

u, v = st_fes.TnT()
h = specialcf.mesh_size

ba_facets = BitArray(mesh.nfacet)
ci = CutInfo(mesh, time_order=0)

dQ = delta_t * dCut(lsetadap.levelsetp1[INTERVAL], NEG, time_order=time_order,
                    deformation=lsetadap.deformation[INTERVAL],
                    definedonelements=ci.GetElementsOfType(HASNEG))
dOmold = dCut(lsetadap.levelsetp1[BOTTOM], NEG,
              deformation=lsetadap.deformation[BOTTOM],
              definedonelements=ci.GetElementsOfType(HASNEG))
dOmnew = dCut(lsetadap.levelsetp1[TOP], NEG,
              deformation=lsetadap.deformation[TOP],
              definedonelements=ci.GetElementsOfType(HASNEG))
dw = delta_t * dFacetPatch(definedonelements=ba_facets, time_order=time_order,
                           deformation=lsetadap.deformation[INTERVAL])


def dt(u): return 1.0 / delta_t * dtref(u)


a = RestrictedBilinearForm(st_fes, "a", check_unused=False,
                           element_restriction=ci.GetElementsOfType(HASNEG),
                           facet_restriction=ba_facets)
a += v * (dt(u) - dt(lsetadap.deform) * grad(u)) * dQ
a += (alpha * InnerProduct(grad(u), grad(v))) * dQ
a += (v * InnerProduct(w, grad(u))) * dQ
a += (fix_tref(u, 0) * fix_tref(v, 0)) * dOmold
a += h**(-2) * (1 + delta_t / h) * gamma * \
    (u - u.Other()) * (v - v.Other()) * dw

f = LinearForm(st_fes)
f += coeff_f * v * dQ
f += u_last * fix_tref(v, 0) * dOmold

# set initial values
u_last.Set(fix_tref(u_exact, 0))
# project u_last at the beginning of each time step
lsetadap.ProjectOnUpdate(u_last)

while tend - told.Get() > delta_t / 2:
    lsetadap.CalcDeformation(levelset)

    # update markers in (space-time) mesh
    ci.Update(lsetadap.levelsetp1[INTERVAL], time_order=0)

    # re-compute the facets for stabilization:
    ba_facets[:] = GetFacetsWithNeighborTypes(mesh,
                                              a=ci.GetElementsOfType(HASNEG),
                                              b=ci.GetElementsOfType(IF))
    active_dofs = GetDofsOfElements(st_fes, ci.GetElementsOfType(HASNEG))

    a.Assemble(reallocate=True)
    f.Assemble()

    # solve linear system
    inv = a.mat.Inverse(active_dofs)
    gfu.vec.data = inv * f.vec.data

    # evaluate upper trace of solution for
    #  * for error evaluation
    #  * upwind-coupling to next time slab
    RestrictGFInTime(spacetime_gf=gfu, reference_time=1.0, space_gf=u_last)

    # compute error at final time
    l2error = sqrt(
        Integrate((fix_tref(u_exact, 1) - u_last)**2 * dOmnew, mesh))

    # update time variable (ParameterCL)
    told.Set(told.Get() + delta_t)
    print("\rt = {0:12.9f}, L2 error = {1:12.9e}".format(told.Get(), l2error))

    try:
        __builtin__
        __IPYTHON__
        scene.Redraw()
    except NameError:
        scene.Redraw(blocking=True)
