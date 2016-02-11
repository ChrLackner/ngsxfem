
geometry = cube_simple.geo
#mesh = cube.vol.gz
mesh = cube_simple.vol.gz


#load xfem-library and python-bindings
shared = libngsxfem_xfem
shared = libngsxfem_tracefem
shared = libngsxfem_lsetcurving


constant heapsize = 1e9

constant R = 1.0
constant one = 1.0
        
####levelsets
coefficient lset
( sqrt(x*x+y*y+z*z) - R),

fespace fes_lset_ho -type=h1ho -order=4
gridfunction lset_ho -fespace=fes_lset_ho
numproc setvalues npsv1 -gridfunction=lset_ho -coefficient=lset
        
###########################################################################
########################### quasi-normal field ############################
###########################################################################
fespace fes_normal -type=h1ho -order=4 -vec #-dirichlet=[1,2,3,4,5,6]
gridfunction qn -fespace=fes_normal
numproc setvalues npsv3 -gridfunction=qn -coefficient=grad_lset_ho

                
# bilinearform mqn -fespace=fes_normal
# mass 1.0 --comp=1        
# mass 1.0 --comp=2
# mass 1.0 --comp=3

# coefficient test1
# (grad_lset_ho*(1,0,0))
# coefficient test2
# (grad_lset_ho*(0,1,0))
# coefficient test3
# (grad_lset_ho*(0,0,1))
                
# linearform fqn -fespace=fes_normal
# source test1 --comp=1
# source test2 --comp=2
# source test3 --comp=3

# define preconditioner cqn -type=direct -bilinearform=mqn
              
# numproc bvp npbvpqn -gridfunction=qn -bilinearform=mqn -linearform=fqn -solver=cg -preconditioner=cqn -maxsteps=1000 -prec=1e-6 # -print
        
###########################################################################
########################### quasi-normal field ############################
###########################################################################
        
### project this level set function into a finite element space of order 1
fespace fes_lset_p1 -type=h1ho -order=1
gridfunction lset_p1 -fespace=fes_lset_p1
numproc interpolatep1 npipp1b -gridfunction_ho=lset_ho -gridfunction_p1=lset_p1

### determine the deformation 
fespace fes_deform -type=h1ho -order=4 -vec
gridfunction deform -fespace=fes_deform

constant lset_lower_bound = 0.0
constant lset_upper_bound = 0.0
constant one = 1.0
constant threshold = 10.0

numproc projectshift nppsh -levelset=lset_ho -levelset_p1=lset_p1 -deform=deform -quasinormal=qn -lset_lower_bound=0.0 -lset_upper_bound=0.0 -threshold=1.0
                                                

###########################################################################
########################### deformation ###################################
###########################################################################









#numproc setdeformation npudef -gridfunction=deform
                
define fespace fesh1
       -type=h1ho
       -order=2
       -dirichlet=[1,2,3,4]

# use an "extended" continuous finite element space
# you may change the order here
define fespace tracefes
       -type=xfespace
       -type_std=h1ho
       -ref_space=0
       -dirichlet=[1,2,3,4]
       -trace

#update "extended" part of XFE space:
numproc informxfem npix
        -xfespace=tracefes
        -fespace=fesh1
        -coef_levelset=lset_p1

gridfunction gf_u -fespace=tracefes



bilinearform a -fespace=tracefes
tracemass 1.0
tracelaplacebeltrami 1.0
#tracelaplace 1.0
# tracediv conv

linearform f -fespace=tracefes
# tracesource (sin(pi*z)*(1+pi*pi*(1-z*z*z))+cos(pi*z)*4*pi*z)
tracesource (z*z+6*z*z-2)#solution u=z**2
#tracesource (z) #solution u=z ???
#tracesource (sin(pi*z)*(pi*pi*(1-z*z)+1)+cos(pi*z)*2*pi*z) #solution u=sin(pi*z)
# tracesource (sin(pi*z))

coefficient u_sol
(z*z),

# coefficient u_sol
# (sin(pi*z)),

coefficient gradu_gamma_sol
((-x*pi*z*cos(pi*z)),(-y*pi*z*cos(pi*z)),(pi*cos(pi*z)-z*pi*z*cos(pi*z))),
        
#define preconditioner c -type=local -bilinearform=a -test #-block
define preconditioner c -type=direct -bilinearform=a -inverse=pardiso -test

numproc bvp npbvp -gridfunction=gf_u -bilinearform=a -linearform=f -solver=cg -preconditioner=c -maxsteps=1000 -prec=1e-6
# define preconditioner c -type=direct -bilinearform=a -inverse=pardiso -test

# numproc traceoutput npto -gridfunction=u -levelset=lset -subdivision=0 -reset -instat
# numproc traceoutput npto -gridfunction=u -levelset=lset -subdivision=0 -instat
# numproc parabolic3d np1 -bilinearforma=a -bilinearformm=m -linearform=f -visnumproc=npto -gridfunction=u -dt=0.01 -tend=3


bilinearform evalu -fespace=tracefes -nonassemble
exttrace 1.0

numproc drawflux npdf -solution=gf_u -bilinearform=evalu -applyd -label=u

numproc visualization npviz -scalarfunction=u
    -minval=-1.5 -maxval=1.5
    -nolineartexture -deformationscale=0.25 -subdivision=0

numproc calcerrors npcalcerr -levelset_ho=lset_ho -levelset_p1=lset_p1 -deform=deform
        
numproc tracediff3d npxd 
        -gridfunction=gf_u 
        -coef=u_sol
        -intorder=6

                
numproc unsetdeformation npudef
        
# numproc vtkoutput npout -filename=simple
#         -coefficients=[lset,lset_ho,lset_p1,deform,gf_u,u_sol]
#         -fieldnames=[lset,lsetho,lsetp1,deformation,u,u_sol]
#         -subdivision=2

numproc levelsetrefine nplsref -levelset=lset_p1

# numproc markinterface npmi -fespace=tracefes
        