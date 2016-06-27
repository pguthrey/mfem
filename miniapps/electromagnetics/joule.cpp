//                       J O U L E
//
// Usage:
//    srun -n 8 -p pdebug joule -m rod2eb3sshex8.gen -o 2 -dt 0.5 -s 22 -tf 200.0
//
// Options:
// -m [string]   the mesh file name
// -o [int]      the order of the basis
// -rs [int]     number of times to serially refine the mesh
// -rp [int]     number of times to refine the mesh in parallel
// -s [int]      time integrator 1=backward Euler, 2=SDIRK2, 3=SDIRK3, 22=Midpoint, 23=SDIRK23, 24=SDIRK34
// -tf [double]  the final time
// -dt [double]  time step
// -mu [double]  the magnetic permeability
// -cnd [double] the electrical conductivity
// -f [double]   the frequency of the applied EM BC
// -vis [int]    GLVis -vis = true -no-vis = false
// -vs [int]     visualization step
// -k [string]   base file name for output file
// -print [int]  print solution (gridfunctions) to disk  0 = no, 1 = yes
// -amr [int]    0 = no amr, 1 = amr
// -sc [int]     0 = no static condensation, 1 = use static condensation
// -p [string]   specify the problem to run, "rod", "coil", etc.
//
// Description:  This examples solves a time dependent eddy current
//               problem, resulting in Joule heating.
//
//               This version has electrostatic potential, Phi, which is a source
//               term in the EM diffusion equation. The potenation itself is
//               driven by essential BC's
//
//               Div sigma Grad Phi = 0
//               sigma E  =  Curl B/mu - sigma grad Phi
//               dB/dt = - Curl E
//               F = -k Grad T
//               c dT/dt = -Div(F) + sigma E.E,
//
//               where B is the magnetic flux, E is the electric field,
//               T is the temperature, F is the thermal flux,
//               sigma is electrical conductivity, mu is the magnetic
//               permeability, and alpha is the thermal diffusivity.
//               The geometry of the domain is assumed to be as follows:
//
//
//                                   boundary attribute 3
//                                 +---------------------+
//                    boundary --->|                     | boundary
//                    attribute 1  |                     | atribute 2
//                    (driven)     +---------------------+
//
//               The voltage BC condition is essential BC on atribute 1 (front) and 2 (rear)
//               given by function p_bc() at bottom of this file.
//
//               The E-field boundary condition specifies the essential BC (n cross E)
//               on  atribute 1 (front) and 2 (rear) given by function edot_bc at bottom of this file.
//               The E-field can be set on attribute 3 also.
//
//               The thermal boundary condition for the flux F is the natural BC on  atribute 1 (front) and 2 (rear)
//               This means that dT/dt = 0 on the boundaries, and the initial T = 0.
//
//               See section 2.5 for how the material propertied are assigned to mesh attribiutes, this
//               needs to be changed for different applications.
//
//               See section 8.0 for how the boundary conditions are assigned to mesh attributes, this
//               needs to be changed for different applications.
//
// NOTE:         This code is set up to solve two example problems, 1) a straight metal rod surrounded by air,
//               2) a metal rod surrounded by a metal coil all surrounded by air. To specify prioblem (1) use the command
//               line options "-p rod -m rod2eb3sshex8.gen", to specify problem (2) use the command line options
//               "-p coil -m coil_centered_tet10.gen". problem (1) has two materials and problem (2) has three materials,
//               and the BC's are different.
//
//
//
// NOTE:         We write out, optionally, grid functions for P, E, B, W, F, and T. These can be visualized using
//               glvis -np 4 -m mesh.mesh -g E, assuming we used 4 processors
//


#include "mfem.hpp"
#include <memory>
#include <iostream>
#include <fstream>
#include "joule_solver.hpp"
#include "joule_globals.hpp"

using namespace std;
using namespace mfem;

void visualize(ostream &out, ParMesh *mesh,
               ParGridFunction *field, bool vec_field,
               const char *field_name = NULL,
               double range = -1.0, int pal = 13,
               bool init_vis = false);

void print_banner();

static double aj_ = 0.0;
static double mj_ = 0.0;
static double sj_ = 0.0;
static double wj_ = 0.0;
static double kj_ = 0.0;
static double hj_ = 0.0;
static double dtj_ = 0.0;

int main(int argc, char *argv[])
{
   // 1. Initialize MPI.
   MPI_Session mpi(argc, argv);
   int myid = mpi.WorldRank();

   // print the cool banner
   if (mpi.Root()) { print_banner(); }

   // 2. Parse command-line options.
   const char *mesh_file = "CylinderHex.mesh";
   int ser_ref_levels = 0;
   int par_ref_levels = 0;
   int order = 2;
   int ode_solver_type = 1;
   double t_final = 300.0;
   double dt = 3;
   double amp = 1.0;
   double mu = 1.0;
   double sigma = 2.0*M_PI*10;
   double Tcapacity = 1.0;
   double Tconductivity = 0.01;
   // Mark's alpha (for analytical solution) is the inverse of my alpha
   double alpha = Tconductivity/Tcapacity;
   double freq = 1.0/60.0;
   bool visualization = true;
   int vis_steps = 1;
   int gfprint = 0;
   const char *basename = "Joule";
   int amr = 0;
   int debug = 0;
   bool cubit = false;
   const char *problem = "rod";

   OptionsParser args(argc, argv);
   args.AddOption(&mesh_file, "-m", "--mesh",
                  "Mesh file to use.");
   args.AddOption(&ser_ref_levels, "-rs", "--refine-serial",
                  "Number of times to refine the mesh uniformly in serial.");
   args.AddOption(&par_ref_levels, "-rp", "--refine-parallel",
                  "Number of times to refine the mesh uniformly in parallel.");
   args.AddOption(&order, "-o", "--order",
                  "Order (degree) of the finite elements.");
   args.AddOption(&ode_solver_type, "-s", "--ode-solver",
                  "ODE solver: 1 - Backward Euler, 2 - SDIRK2, 3 - SDIRK3\n\t."
                  "\t   22 - Mid-Point, 23 - SDIRK23, 24 - SDIRK34.");
   args.AddOption(&t_final, "-tf", "--t-final",
                  "Final time; start time is 0.");
   args.AddOption(&dt, "-dt", "--time-step",
                  "Time step.");
   args.AddOption(&mu, "-mu", "--permeability",
                  "Magnetic permeability coefficient.");
   args.AddOption(&sigma, "-cnd", "--sigma",
                  "Conductivity coefficient.");
   args.AddOption(&freq, "-f", "--frequency",
                  "Frequency of oscillation.");
   args.AddOption(&visualization, "-vis", "--visualization", "-no-vis",
                  "--no-visualization",
                  "Enable or disable GLVis visualization.");
   args.AddOption(&vis_steps, "-vs", "--visualization-steps",
                  "Visualize every n-th timestep.");
   args.AddOption(&basename, "-k", "--outputfilename",
                  "Name of the visit dump files");
   args.AddOption(&gfprint, "-print", "--print",
                  "Print results (gridfunctions) to disk.");
   args.AddOption(&amr, "-amr", "--amr",
                  "Enable AMR");
   args.AddOption(&STATIC_COND, "-sc", "--static-condensation",
                  "Enable static condesnsation");
   args.AddOption(&debug, "-debug", "--debug",
                  "Print matrices and vectors to disk");
   args.AddOption(&SOLVERPRINTLEVEL, "-hl", "--hypre-print-level",
                  "Hypre print level");
   args.AddOption(&cubit, "-cubit", "--cubit", "-no-cubit",
                  "--no-cubit",
                  "Is the mesh a cubit (Netcdf) file.");
   args.AddOption(&problem, "-p", "--problem",
                  "Name of problem to run");

   args.Parse();
   if (!args.Good())
   {
      if (mpi.Root())
      {
         args.PrintUsage(cout);
      }
      return 1;
   }
   if (mpi.Root())
   {
      args.PrintOptions(cout);
   }

   aj_  = amp;
   mj_  = mu;
   sj_  = sigma;
   wj_  = 2.0*M_PI*freq;
   kj_  = sqrt(0.5*wj_*mj_*sj_);
   hj_  = alpha;
   dtj_ = dt;

   if (mpi.Root())
   {
      printf("\n");
      printf("Skin depth sqrt(2.0/(wj*mj*sj)) = %g\n",sqrt(2.0/(wj_*mj_*sj_)));
      printf("Skin depth sqrt(2.0*dt/(mj*sj)) = %g\n",sqrt(2.0*dt/(mj_*sj_)));
   }

   // 2.5
   //
   // Here I assign material properties to mesh attributes.
   // This code is not general, I assume the mesh has 3 regions
   // each with a different integer attribiute 1, 2 or 3.
   //
   // the coil problem has three regions 1) coil, 2) air, 3) the rod
   //
   // the rod problem has two regions 1) rod, 2) air

   // turns out for the rod and coil problem we can us ethe same material maps

   std::map<int, double> sigmaMap, InvTcondMap, TcapMap, InvTcapMap;
   double sigmaAir     = 1.0e-6 * sigma;
   double TcondAir     = 1.0e6 * Tconductivity;
   double TcapAir      = 1.0  * Tcapacity;

   if (strcmp(problem,"rod")==0 || strcmp(problem,"coil")==0)
   {

      sigmaMap.insert(pair<int, double>(1, sigma));
      sigmaMap.insert(pair<int, double>(2, sigmaAir));
      sigmaMap.insert(pair<int, double>(3, sigmaAir));

      InvTcondMap.insert(pair<int, double>(1, 1.0/Tconductivity));
      InvTcondMap.insert(pair<int, double>(2, 1.0/TcondAir));
      InvTcondMap.insert(pair<int, double>(3, 1.0/TcondAir));

      TcapMap.insert(pair<int, double>(1, Tcapacity));
      TcapMap.insert(pair<int, double>(2, TcapAir));
      TcapMap.insert(pair<int, double>(3, TcapAir));

      InvTcapMap.insert(pair<int, double>(1, 1.0/Tcapacity));
      InvTcapMap.insert(pair<int, double>(2, 1.0/TcapAir));
      InvTcapMap.insert(pair<int, double>(3, 1.0/TcapAir));
   }
   else
   {
      cerr << "Problem " << problem << " not recognized\n";
      mfem_error();
   }



   // 3. Read the serial mesh from the given mesh file on all processors. We can
   //    handle triangular, quadrilateral, tetrahedral and hexahedral meshes
   //    with the same code.
   Mesh *mesh;
   mesh = new Mesh(mesh_file, 1, 1);
   int dim = mesh->Dimension();

   //
   // 3.5 Assign the boundary conditions
   //
   Array<int> ess_bdr(mesh->bdr_attributes.Max());
   Array<int> thermal_ess_bdr(mesh->bdr_attributes.Max());
   Array<int> poisson_ess_bdr(mesh->bdr_attributes.Max());
   if (strcmp(problem,"coil")==0)
   {

      // BEGIN CODE FOR THE COIL PROBLEM
      // For the coil in a box problem we have surfaces 1) coil end (+), 2) coil end (-),
      // 3) five sides of box, 4) side of box with coil BC

      ess_bdr = 0;
      ess_bdr[0] = 1; // boundary attribute 4 (index 3) is fixed
      ess_bdr[1] = 1; // boundary attribute 4 (index 3) is fixed
      ess_bdr[2] = 1; // boundary attribute 4 (index 3) is fixed
      ess_bdr[3] = 1; // boundary attribute 4 (index 3) is fixed

      // Same as above, but this is for the thermal operator
      // for HDiv formulation the essetial BC is the flux

      thermal_ess_bdr = 0;
      thermal_ess_bdr[2] = 1; // boundary attribute 4 (index 3) is fixed

      // Same as above, but this is for the poisson eq
      // for H1 formulation the essetial BC is the value of Phi

      poisson_ess_bdr = 0;
      poisson_ess_bdr[0] = 1; // boundary attribute 1 (index 0) is fixed
      poisson_ess_bdr[1] = 1; // boundary attribute 2 (index 1) is fixed
      // END CODE FOR THE COIL PROBLEM
   }
   else if (strcmp(problem,"rod")==0)
   {

      // BEGIN CODE FOR THE STRAIGHT ROD PROBLEM
      // the boundary conditions below are for the straight rod problem
      // using mesh rod-tet.gen or rod-hex.gen

      ess_bdr = 0;
      ess_bdr[0] = 1; // boundary attribute 1 (index 0) is fixed (front)
      ess_bdr[1] = 1; // boundary attribute 2 (index 1) is fixed (rear)
      ess_bdr[2] = 1; // boundary attribute 3 (index 3) is fixed (outer)

      // Same as above, but this is for the thermal operator
      // for HDiv formulation the essetial BC is the flux, which is zero on the front and sides
      // Note the Natural BC is T = 0 on the outer surface

      thermal_ess_bdr = 0;
      thermal_ess_bdr[0] = 1; // boundary attribute 1 (index 0) is fixed (front)
      thermal_ess_bdr[1] = 1; // boundary attribute 2 (index 1) is fixed (rear)

      // Same as above, but this is for the poisson eq
      // for H1 formulation the essetial BC is the value of Phi

      poisson_ess_bdr = 0;
      poisson_ess_bdr[0] = 1; // boundary attribute 1 (index 0) is fixed (front)
      poisson_ess_bdr[1] = 1; // boundary attribute 2 (index 1) is fixed (back)
      // END CODE FOR THE STRAIGHT ROD PROBLEM
   }
   else
   {
      cerr << "Problem " << problem << " not recognized\n";
      mfem_error();
   }


   // The following is required for mesh refinement
   mesh->EnsureNCMesh();

   // 4. Define the ODE solver used for time integration. Several implicit
   //    methods are available, including singly diagonal implicit
   //    Runge-Kutta (SDIRK).
   ODESolver *ode_solver;
   switch (ode_solver_type)
   {
      // Implicit L-stable methods
      case 1:  ode_solver = new BackwardEulerSolver; break;
      case 2:  ode_solver = new SDIRK23Solver(2); break;
      case 3:  ode_solver = new SDIRK33Solver; break;
      // Implicit A-stable methods (not L-stable)
      case 22: ode_solver = new ImplicitMidpointSolver; break;
      case 23: ode_solver = new SDIRK23Solver; break;
      case 24: ode_solver = new SDIRK34Solver; break;
      default:
         if (mpi.Root())
         {
            cout << "Unknown ODE solver type: " << ode_solver_type << '\n';
         }
         delete mesh;
         return 3;
   }

   // 5. Refine the mesh in serial to increase the resolution. In this example
   //    we do 'ser_ref_levels' of uniform refinement, where 'ser_ref_levels' is
   //    a command-line parameter.
   for (int lev = 0; lev < ser_ref_levels; lev++)
   {
      mesh->UniformRefinement();
   }

   // 6. Define a parallel mesh by a partitioning of the serial mesh. Refine
   //    this mesh further in parallel to increase the resolution. Once the
   //    parallel mesh is defined, the serial mesh can be deleted.
   ParMesh *pmesh = new ParMesh(MPI_COMM_WORLD, *mesh);
   delete mesh;
   for (int lev = 0; lev < par_ref_levels; lev++)
   {
      pmesh->UniformRefinement();
   }


   // 6.5
   //    Apply non-uniform non-conforming mesh refinement to the mesh.
   //    The whole metal region is refined, i.e. this is not based on any error estimator


   if (amr == 1)
   {
      Array<int> ref_list;
      int numElems = pmesh->GetNE();
      for (int ielem = 0; ielem < numElems; ielem++)
      {
         int thisAtt = pmesh->GetAttribute(ielem);
         if (thisAtt == 1)
         {
            ref_list.Append(ielem);
         }
      }

      pmesh->GeneralRefinement(ref_list);

      ref_list.DeleteAll();
   }

   //
   // 6.625 Reorient the mesh.
   //
   // Must be done after refinement but before definition
   // of higher order Nedelec spaces

   pmesh->ReorientTetMesh();

   // 6.75 Rebalance the mesh
   //
   // Since the mesh was adaptivley refined in a non-uniform way it will be
   // computationally unbalanced.
   //

   if (pmesh->Nonconforming())
   {
      pmesh->Rebalance();
   }

   // 7. Define the parallel finite element spaces representing.
   //    We'll use H(curl) for electric field
   //    and H(div) for magbetic flux
   //    and H(div) for thermal flux
   //    and H(grad) for electrostatic potential
   //    and L2 for temperature

   // L2 is discontinous "cell-center" bases
   // type 2 is "positive"
   //L2_FECollection L2FEC(order-1, dim, 2);
   L2_FECollection L2FEC(order-1, dim);

   // ND stands for Nedelec
   ND_FECollection HCurlFEC(order, dim);

   // RT stands for Raviart-Thomas
   RT_FECollection HDivFEC(order-1, dim);

   // H1 is nodal continous "Lagrange" interpolatory bases
   H1_FECollection HGradFEC(order, dim);

   ParFiniteElementSpace    L2FESpace(pmesh, &L2FEC);
   ParFiniteElementSpace HCurlFESpace(pmesh, &HCurlFEC);
   ParFiniteElementSpace  HDivFESpace(pmesh, &HDivFEC);
   ParFiniteElementSpace  HGradFESpace(pmesh, &HGradFEC);

   // The terminology is TrueVSize is the unique (non-redundant) number of dofs
   HYPRE_Int glob_size_l2 =    L2FESpace.GlobalTrueVSize();
   HYPRE_Int glob_size_nd =    HCurlFESpace.GlobalTrueVSize();
   HYPRE_Int glob_size_rt =    HDivFESpace.GlobalTrueVSize();
   HYPRE_Int glob_size_h1 =    HGradFESpace.GlobalTrueVSize();

   if (mpi.Root())
   {
      cout << "Number of Temperature Flux unknowns:    " << glob_size_rt << endl;
      cout << "Number of Temperature unknowns:         " << glob_size_l2 << endl;
      cout << "Number of Electric Field unknowns:      " << glob_size_nd << endl;
      cout << "Number of Magnetic Field unknowns:      " << glob_size_rt << endl;
      cout << "Number of Electrostatic unknowns:       " << glob_size_h1 << endl;
   }

   int Vsize_l2 = L2FESpace.GetVSize();
   int Vsize_nd = HCurlFESpace.GetVSize();
   int Vsize_rt = HDivFESpace.GetVSize();
   int Vsize_h1 = HGradFESpace.GetVSize();

   /* the big BlockVector stores the fields as
   0 Temperture
   1 Temperature Flux
   2 P field
   3 E field
   4 B field
   5 Joule Heating
   */

   Array<int> true_offset(7);
   true_offset[0] = 0;
   true_offset[1] = true_offset[0] + Vsize_l2;
   true_offset[2] = true_offset[1] + Vsize_rt;
   true_offset[3] = true_offset[2] + Vsize_h1;
   true_offset[4] = true_offset[3] + Vsize_nd;
   true_offset[5] = true_offset[4] + Vsize_rt;
   true_offset[6] = true_offset[5] + Vsize_l2;


   // The BlockVector is a large contiguous chunck of memory for storing
   // the required data for the hyprevectors, in this case the temperature L2, the T-flux HDiv, the E-field
   // HCurl, and the B-field HDiv, and scalar potential P
   BlockVector F(true_offset);

   // grid functions E, B, T, F, P, and w which is the Joule heating
   ParGridFunction E_gf, B_gf, T_gf, F_gf, w_gf, P_gf;
   T_gf.MakeRef(&L2FESpace,F,   true_offset[0]);
   F_gf.MakeRef(&HDivFESpace,F, true_offset[1]);
   P_gf.MakeRef(&HGradFESpace,F,true_offset[2]);
   E_gf.MakeRef(&HCurlFESpace,F,true_offset[3]);
   B_gf.MakeRef(&HDivFESpace,F, true_offset[4]);
   w_gf.MakeRef(&L2FESpace,F,   true_offset[5]);

   // This is for visit visualization of exact solution
   ParGridFunction Eexact_gf(&HCurlFESpace);
   ParGridFunction Texact_gf(&L2FESpace);

   // 8. Get the boundary conditions, set up the exact solution grid functions
   //
   // These VectorCoefficients have an Eval function.
   // Note that e_exact anf b_exact in this case are exact analytical
   // solutions, taking a 3-vector point as input and returning a 3-vector field
   VectorFunctionCoefficient E_exact(3, e_exact);
   VectorFunctionCoefficient B_exact(3, b_exact);
   FunctionCoefficient T_exact(t_exact);
   Eexact_gf.ProjectCoefficient(E_exact);
   Texact_gf.ProjectCoefficient(T_exact);




   // 9. Initialize the Diffusion operator, the GLVis visualization and print
   //    the initial energies.

   MagneticDiffusionEOperator oper(true_offset[6], L2FESpace, HCurlFESpace,
                                   HDivFESpace, HGradFESpace,
                                   ess_bdr, thermal_ess_bdr, poisson_ess_bdr,
                                   mu, sigmaMap, TcapMap, InvTcapMap, InvTcondMap);

   // This function initializes all the fields to zero or some provided IC
   oper.Init(F);

   socketstream vis_T, vis_E, vis_B, vis_w, vis_P;
   if (visualization)
   {
      char vishost[] = "localhost";
      int  visport   = 19916;
      vis_T.open(vishost, visport);
      vis_T.precision(8);
      vis_E.open(vishost, visport);
      vis_E.precision(8);
      vis_B.open(vishost, visport);
      vis_B.precision(8);
      vis_P.open(vishost, visport);
      vis_P.precision(8);
      visualize(vis_T, pmesh, &T_gf, false, "Temperature", 100.0, 6, true);
      visualize(vis_E, pmesh, &E_gf, true, "Electric Field", amp, 13, true);
      visualize(vis_B, pmesh, &B_gf, true, "Magnetic Flux",1.0, 13, true);
      visualize(vis_P, pmesh, &P_gf, false, "Electrostatic",1.0, 13, true);

      // Make sure all ranks have sent their 'v' solution before initiating
      // another set of GLVis connections (one from each rank):
      MPI_Barrier(pmesh->GetComm());

      vis_w.open(vishost, visport);
      vis_w.precision(8);
      visualize(vis_w, pmesh, &w_gf, false, "Energy Deposition", sigma*amp*amp, 13,
                true);

   }

   // visit visualization
   VisItDataCollection visit_dc(basename, pmesh);
   visit_dc.RegisterField("E", &E_gf);
   visit_dc.RegisterField("B", &B_gf);
   visit_dc.RegisterField("T", &T_gf);
   visit_dc.RegisterField("w", &w_gf);
   visit_dc.RegisterField("Phi", &P_gf);
   visit_dc.RegisterField("F", &F_gf);
   visit_dc.RegisterField("Eexact", &Eexact_gf);
   visit_dc.RegisterField("Texact", &Texact_gf);
   bool visit = true;
   if (visit)
   {
      visit_dc.SetCycle(0);
      visit_dc.SetTime(0.0);
      visit_dc.Save();
   }

   Vector zero_vec(3); zero_vec = 0.0;
   VectorConstantCoefficient Zero_vec(zero_vec);
   ConstantCoefficient Zero(0.0);
   double eng_E0 = E_gf.ComputeL2Error(Zero_vec);
   double eng_B0 = B_gf.ComputeL2Error(Zero_vec);
   // double eng_T0 = T_gf.ComputeL2Error(Zero);

   double err_E0 = E_gf.ComputeL2Error(E_exact);
   double err_B0 = B_gf.ComputeL2Error(B_exact);
   // double err_T0 = T_gf.ComputeL2Error(T_exact);

   //double me0 = oper.MagneticEnergy(B_gf);
   double el0 = oper.ElectricLosses(E_gf);

   if (mpi.Root())
   {
      cout << scientific  << setprecision(3) << "initial electric L2 error    = " <<
           err_E0/(eng_E0+1.0e-20) << endl;
      cout << scientific  << setprecision(3) << "initial magnetic L2 error    = " <<
           err_B0/(eng_B0+1.0e-20) << endl;
      cout << scientific  << setprecision(3) << "initial electric losses (EL) = " <<
           el0 << endl;
   }

   // 10. Perform time-integration (looping over the time iterations, ti, with a
   //     time-step dt).
   //
   // oper is the MagneticDiffusionOperator which has a Mult() method and an ImplicitSolve()
   // method which are used by the time integrators.
   ode_solver->Init(oper);
   double t = 0.0;


   bool last_step = false;
   for (int ti = 1; !last_step; ti++)
   {
      if (t + dt >= t_final - dt/2)
      {
         last_step = true;
      }

      // F is the vector of dofs, t is the current time, and dt is the
      // time step to advance.
      ode_solver->Step(F, t, dt);

      // update the exact solution GF
      Eexact_gf.ProjectCoefficient(E_exact);
      Texact_gf.ProjectCoefficient(T_exact);

      if (debug == 1)
      {
         oper.Debug(basename,t);
      }

      if (gfprint == 1)
      {

         ostringstream T_name, E_name, B_name, F_name, w_name, P_name, mesh_name;
         T_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "T." <<
                setfill('0') << setw(6) << myid;
         E_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "E." <<
                setfill('0') << setw(6) << myid;
         B_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "B." <<
                setfill('0') << setw(6) << myid;
         F_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "F." <<
                setfill('0') << setw(6) << myid;
         w_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "w." <<
                setfill('0') << setw(6) << myid;
         P_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "P." <<
                setfill('0') << setw(6) << myid;
         mesh_name << basename << "_"  << setfill('0') << setw(6) << t << "_" << "mesh."
                   << setfill('0') << setw(6) << myid;

         ofstream mesh_ofs(mesh_name.str().c_str());
         mesh_ofs.precision(8);
         pmesh->Print(mesh_ofs);
         mesh_ofs.close();

         ofstream T_ofs(T_name.str().c_str());
         T_ofs.precision(8);
         T_gf.Save(T_ofs);
         T_ofs.close();

         ofstream E_ofs(E_name.str().c_str());
         E_ofs.precision(8);
         E_gf.Save(E_ofs);
         E_ofs.close();

         ofstream B_ofs(B_name.str().c_str());
         B_ofs.precision(8);
         B_gf.Save(B_ofs);
         B_ofs.close();

         ofstream F_ofs(F_name.str().c_str());
         F_ofs.precision(8);
         F_gf.Save(B_ofs);
         F_ofs.close();

         ofstream P_ofs(P_name.str().c_str());
         P_ofs.precision(8);
         P_gf.Save(B_ofs);
         P_ofs.close();

         ofstream w_ofs(w_name.str().c_str());
         w_ofs.precision(8);
         w_gf.Save(w_ofs);
         w_ofs.close();
      }

      if (last_step || (ti % vis_steps) == 0)
      {

         double eng_E = E_gf.ComputeL2Error(Zero_vec);
         double eng_B = B_gf.ComputeL2Error(Zero_vec);
         double eng_T = T_gf.ComputeL2Error(Zero);

         double err_E = E_gf.ComputeL2Error(E_exact);
         double err_B = B_gf.ComputeL2Error(B_exact);
         double err_T = T_gf.ComputeL2Error(T_exact);

         //double me = oper.MagneticEnergy(B_gf);
         double el = oper.ElectricLosses(E_gf);

         if (mpi.Root())
         {
            cout << fixed;
            cout << "step " << setw(6) << ti << " t = " << setw(6) << setprecision(3) << t
                 << " relative errors "  << scientific << setprecision(3) << err_E/
                 (eng_E+1.0e-20) << " "
                 << setprecision(3) << err_B/(eng_B+1.0e-20) << " "
                 << setprecision(3) << err_T/(eng_T+1.0e-20) << endl;
            //cout << scientific  << setprecision(3) << "magnetic energy (ME) = " << me << endl;
            cout << scientific  << setprecision(3) << "electric losses (EL) = " << el <<
                 endl;

         }

         // Make sure all ranks have sent their 'v' solution before initiating
         // another set of GLVis connections (one from each rank):
         MPI_Barrier(pmesh->GetComm());

         if (visualization)
         {
            visualize(vis_T, pmesh, &T_gf, false);
            visualize(vis_E, pmesh, &E_gf, true);
            visualize(vis_B, pmesh, &B_gf, true);
            visualize(vis_P, pmesh, &P_gf, false);
            visualize(vis_w, pmesh, &w_gf, false);
         }

         if (visit)
         {
            visit_dc.SetCycle(ti);
            visit_dc.SetTime(t);
            visit_dc.Save();
         }
      }
   }
   if (visualization)
   {
      vis_T.close();
      vis_E.close();
      vis_B.close();
      vis_w.close();
      vis_P.close();
   }


   // 10. Free the used memory.
   delete ode_solver;
   delete pmesh;

   return 0;
}

void visualize(ostream &out, ParMesh *mesh,
               ParGridFunction *field, bool vec_field,
               const char *field_name,
               double range, int pal, bool init_vis)
{
   if (!out)
   {
      return;
   }

   out << "parallel " << mesh->GetNRanks() << " " << mesh->GetMyRank() << "\n";
   out << "solution\n" << *mesh << *field;

   if (init_vis)
   {
      int wd = 400;
      out << "window_size " << wd << " " << wd << "\n";
      out << "window_title '" << field_name << "'\n";
      out << "palette " << pal << "\n";
      if (mesh->SpaceDimension() == 2)
      {
         out << "view 0 0\n"; // view from top
         out << "keys jl\n";  // turn off perspective and light
      }
      if ( vec_field )
      {
         out << "keys cmv\n";   // show colorbar, mesh, and vectors
      }
      else
      {
         out << "keys cm\n";   // show colorbar and mesh
      }
      if ( range <= 0.0 )
      {
         out << "autoscale value\n"; // update value-range; keep mesh-extents fixed
      }
      else
      {
         out << "autoscale off\n"; // update value-range; keep mesh-extents fixed
         out << "valuerange " << 0.0 << " " << range <<
             "\n"; // update value-range; keep mesh-extents fixed
      }
      out << "pause\n";
   }
   out << flush;
}


void edot_bc(const Vector &x, Vector &E)
{
   E = 0.0;
}

void e_exact(const Vector &x, Vector &E)
{
   E = 0.0;
}

void b_exact(const Vector &x, Vector &B)
{
   B = 0.0;
}

void Jz(const Vector &x, Vector &J)
{
   J = 0.0;
}

double t_exact(Vector &x)
{
   double T = 0.0;
   return T;
}

double p_bc(const Vector &x, double t)
{

   // the value
   double T;
   if (x[2] < 0.0)
   {
      T = 1.0;
   }
   else
   {
      T = -1.0;
   }

   return T*cos(wj_ * t);
}

void print_banner()
{

   char banner[219] =
   {
      32,
      32,
      32,
      32,
      32,
      95,
      95,
      95,
      95,
      46,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      46,
      95,
      95,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      10,
      32,
      32,
      32,
      32,
      124,
      32,
      32,
      32,
      32,
      124,
      32,
      95,
      95,
      95,
      95,
      32,
      32,
      95,
      95,
      32,
      95,
      95,
      124,
      32,
      32,
      124,
      32,
      32,
      32,
      95,
      95,
      95,
      95,
      32,
      32,
      10,
      32,
      32,
      32,
      32,
      124,
      32,
      32,
      32,
      32,
      124,
      47,
      32,
      32,
      95,
      32,
      92,
      124,
      32,
      32,
      124,
      32,
      32,
      92,
      32,
      32,
      124,
      32,
      95,
      47,
      32,
      95,
      95,
      32,
      92,
      32,
      10,
      47,
      92,
      95,
      95,
      124,
      32,
      32,
      32,
      32,
      40,
      32,
      32,
      60,
      95,
      62,
      32,
      41,
      32,
      32,
      124,
      32,
      32,
      47,
      32,
      32,
      124,
      95,
      92,
      32,
      32,
      95,
      95,
      95,
      47,
      32,
      10,
      92,
      95,
      95,
      95,
      95,
      95,
      95,
      95,
      95,
      124,
      92,
      95,
      95,
      95,
      95,
      47,
      124,
      95,
      95,
      95,
      95,
      47,
      124,
      95,
      95,
      95,
      95,
      47,
      92,
      95,
      95,
      95,
      32,
      32,
      62,
      10,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      32,
      92,
      47,
      32,
      10,
      10,
      10,
      0
   };

   printf("%s",banner);

}
