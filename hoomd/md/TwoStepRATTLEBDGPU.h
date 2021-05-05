// Copyright (c) 2009-2019 The Regents of the University of Michigan
// This file is part of the HOOMD-blue project, released under the BSD 3-Clause License.


// Maintainer: joaander

#ifdef ENABLE_HIP

#include "TwoStepRATTLEBD.h"
#include "TwoStepRATTLEBDGPU.cuh"

#ifdef ENABLE_MPI
#include "hoomd/HOOMDMPI.h"
#endif

#include "hoomd/Autotuner.h"

#include <pybind11/pybind11.h>

#ifdef __HIPCC__
#error This header cannot be compiled by nvcc
#endif

namespace py = pybind11;

using namespace std;

//! Implements Brownian dynamics on the GPU
/*! GPU accelerated version of TwoStepBD

    \ingroup updaters
*/
template<class Manifold>
class PYBIND11_EXPORT TwoStepRATTLEBDGPU : public TwoStepRATTLEBD<Manifold>
    {
    public:
        //! Constructs the integration method and associates it with the system
        TwoStepRATTLEBDGPU(std::shared_ptr<SystemDefinition> sysdef,
                     std::shared_ptr<ParticleGroup> group,
                     Manifold manifold,
                     std::shared_ptr<Variant> T,
                     Scalar tolerance);

        virtual ~TwoStepRATTLEBDGPU() {};

        //! Performs the first step of the integration
        virtual void integrateStepOne(uint64_t timestep);

        //! Performs the second step of the integration
        virtual void integrateStepTwo(uint64_t timestep){};

        //! Includes the RATTLE forces to the virial/net force
        virtual void includeRATTLEForce(uint64_t timestep);

    protected:
        unsigned int m_block_size;               //!< block size
    };

/*! \param timestep Current time step
    \post Particle positions are moved forward a full time step and velocities are redrawn from the proper distribution.
*/

template<class Manifold>
TwoStepRATTLEBDGPU<Manifold>::TwoStepRATTLEBDGPU(std::shared_ptr<SystemDefinition> sysdef,
                     std::shared_ptr<ParticleGroup> group,
                     Manifold manifold,
                     std::shared_ptr<Variant> T,
                     Scalar tolerance)
    : TwoStepRATTLEBD<Manifold>(sysdef, group, manifold,T, tolerance)
    {
    if (!this->m_exec_conf->isCUDAEnabled())
        {
        this->m_exec_conf->msg->error() << "Creating a TwoStepRATTLEBDGPU while CUDA is disabled" << endl;
        throw std::runtime_error("Error initializing TwoStepRATTLEBDGPU");
        }

    m_block_size = 256;
    }


template<class Manifold>
void TwoStepRATTLEBDGPU<Manifold>::integrateStepOne(uint64_t timestep)
    {
    // profile this step
    if (this->m_prof)
        this->m_prof->push(this->m_exec_conf, "BD step 1");

    if(!m_manifold.fitsInsideBox(m_pdata->getGlobalBox()))
        {
        throw std::runtime_error("Parts of the manifold are outside the box");
        }

    // access all the needed data
    ArrayHandle< unsigned int > d_index_array(this->m_group->getIndexArray(), access_location::device, access_mode::read);
    unsigned int group_size = this->m_group->getNumMembers();
    const unsigned int D = this->m_sysdef->getNDimensions();
    const GlobalArray< Scalar4 >& net_force = this->m_pdata->getNetForce();

    ArrayHandle<Scalar4> d_pos(this->m_pdata->getPositions(), access_location::device, access_mode::readwrite);
    ArrayHandle<int3> d_image(this->m_pdata->getImages(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar4> d_vel(this->m_pdata->getVelocities(), access_location::device, access_mode::readwrite);

    ArrayHandle<Scalar4> d_net_force(net_force, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_gamma(this->m_gamma, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_diameter(this->m_pdata->getDiameters(), access_location::device, access_mode::read);
    ArrayHandle<unsigned int> d_tag(this->m_pdata->getTags(), access_location::device, access_mode::read);

    // for rotational noise
    ArrayHandle<Scalar3> d_gamma_r(this->m_gamma_r, access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_orientation(this->m_pdata->getOrientationArray(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar4> d_torque(this->m_pdata->getNetTorqueArray(), access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar3> d_inertia(this->m_pdata->getMomentsOfInertiaArray(), access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_angmom(this->m_pdata->getAngularMomentumArray(), access_location::device, access_mode::readwrite);


    rattle_bd_step_one_args args;
    args.d_gamma = d_gamma.data;
    args.n_types = this->m_gamma.getNumElements();
    args.use_alpha = this->m_use_alpha;
    args.alpha = this->m_alpha;
    args.T = (*this->m_T)(timestep);
    args.tolerance = this->m_tolerance;
    args.timestep = timestep;
    args.seed = this->m_sysdef->getSeed();


    bool aniso = this->m_aniso;

    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // prefetch gammas
        auto& gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            cudaMemPrefetchAsync(this->m_gamma.get(), sizeof(Scalar)*this->m_gamma.getNumElements(), gpu_map[idev]);
            cudaMemPrefetchAsync(this->m_gamma_r.get(), sizeof(Scalar)*this->m_gamma_r.getNumElements(), gpu_map[idev]);
            }
        if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
            CHECK_CUDA_ERROR();
        }


    this->m_exec_conf->beginMultiGPU();

    // perform the update on the GPU
    gpu_rattle_brownian_step_one(d_pos.data,
                          d_image.data,
                          d_vel.data,
                          m_pdata->getBox(),
                          d_diameter.data,
                          d_tag.data,
                          d_index_array.data,
                          group_size,
                          d_net_force.data,
                          d_gamma_r.data,
                          d_orientation.data,
                          d_torque.data,
                          d_inertia.data,
                          d_angmom.data,
                          args,
                          this->m_manifold,
                          aniso,
                          this->m_deltaT,
                          D,
                          this->m_noiseless_t,
                          this->m_noiseless_r,
                          this->m_group->getGPUPartition());

    if(this->m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    this->m_exec_conf->endMultiGPU();

    // done profiling
    if (this->m_prof)
        this->m_prof->pop(this->m_exec_conf);
    }


/*! \param timestep Current time step
    \post Forces are pre-calculated for pressure and virial calculations
*/
template<class Manifold>
void TwoStepRATTLEBDGPU<Manifold>::includeRATTLEForce(uint64_t timestep)
    {

    // access all the needed data
    ArrayHandle< unsigned int > d_index_array(this->m_group->getIndexArray(), access_location::device, access_mode::read);
    unsigned int group_size = this->m_group->getNumMembers();
    const GlobalArray< Scalar4 >& net_force = this->m_pdata->getNetForce();
    const GlobalArray<Scalar>&  net_virial = this->m_pdata->getNetVirial();

    ArrayHandle<Scalar4> d_pos(this->m_pdata->getPositions(), access_location::device, access_mode::read);
    ArrayHandle<Scalar4> d_net_force(net_force, access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar> d_net_virial(net_virial, access_location::device, access_mode::readwrite);
    ArrayHandle<Scalar> d_gamma(this->m_gamma, access_location::device, access_mode::read);
    ArrayHandle<Scalar> d_diameter(this->m_pdata->getDiameters(), access_location::device, access_mode::read);
    ArrayHandle<unsigned int> d_tag(this->m_pdata->getTags(), access_location::device, access_mode::read);

    size_t net_virial_pitch = net_virial.getPitch();


    rattle_bd_step_one_args args;
    args.d_gamma = d_gamma.data;
    args.n_types = this->m_gamma.getNumElements();
    args.use_alpha = this->m_use_alpha;
    args.alpha = this->m_alpha;
    args.T = (*this->m_T)(timestep);
    args.tolerance = this->m_tolerance;
    args.timestep = timestep;
    args.seed = this->m_sysdef->getSeed();


    if (this->m_exec_conf->allConcurrentManagedAccess())
        {
        // prefetch gammas
        auto& gpu_map = this->m_exec_conf->getGPUIds();
        for (unsigned int idev = 0; idev < this->m_exec_conf->getNumActiveGPUs(); ++idev)
            {
            cudaMemPrefetchAsync(this->m_gamma.get(), sizeof(Scalar)*this->m_gamma.getNumElements(), gpu_map[idev]);
            }
        if (this->m_exec_conf->isCUDAErrorCheckingEnabled())
            CHECK_CUDA_ERROR();
        }


    this->m_exec_conf->beginMultiGPU();

    // perform the update on the GPU
    gpu_include_rattle_force_bd<Manifold>(d_pos.data,
                          d_net_force.data,
                          d_net_virial.data,
                          d_diameter.data,
                          d_tag.data,
                          d_index_array.data,
                          group_size,
                          args,
                          this->m_manifold,
                          net_virial_pitch,
                          this->m_deltaT,
                          this->m_noiseless_t,
                          this->m_group->getGPUPartition());

    if(this->m_exec_conf->isCUDAErrorCheckingEnabled())
        CHECK_CUDA_ERROR();

    this->m_exec_conf->endMultiGPU();

    // done profiling
    if (this->m_prof)
        this->m_prof->pop(this->m_exec_conf);
    }


//! Exports the TwoStepRATTLEBDGPU class to python
template<class Manifold>
void export_TwoStepRATTLEBDGPU(py::module& m, const std::string& name)
    {
    py::class_<TwoStepRATTLEBDGPU<Manifold>, TwoStepRATTLEBD<Manifold>, std::shared_ptr<TwoStepRATTLEBDGPU<Manifold> > >(m, name.c_str())
        .def(py::init< std::shared_ptr<SystemDefinition>,
                               std::shared_ptr<ParticleGroup>,
                               Manifold,
                               std::shared_ptr<Variant>,
			       Scalar>())
        ;
    }
#endif // ENABLE_HIP
