/***************************************************************************************************

Copyright (c) 2015 Intellectual Ventures Property Holdings, LLC (IVPH) All rights reserved.

EMOD is licensed under the Creative Commons Attribution-Noncommercial-ShareAlike 4.0 License.
To view a copy of this license, visit https://creativecommons.org/licenses/by-nc-sa/4.0/legalcode.

***************************************************************************************************/

#include "stdafx.h"
#include "MalariaAntibody.h"
#include "MathFunctions.h"
#include "SusceptibilityMalaria.h"

#define NON_TRIVIAL_ANTIBODY_THRESHOLD  (0.0000001)
#define TWENTY_DAY_DECAY_CONSTANT       (0.05f)
#define B_CELL_PROLIFERATION_THRESHOLD  (0.4)
#define B_CELL_PROLIFERATION_CONSTANT   (0.33f)
#define ANTIBODY_RELEASE_THRESHOLD      (0.3)
#define ANTIBODY_RELEASE_FACTOR         (4)

namespace Kernel
{

    BEGIN_QUERY_INTERFACE_BODY(MalariaAntibody)
#if USER_JSON_SERIALIZATION || USE_JSON_MPI    
        HANDLE_INTERFACE(IJsonSerializable)
#endif
    END_QUERY_INTERFACE_BODY(MalariaAntibody)

    MalariaAntibody::MalariaAntibody()
        : m_antigen_count(0)
        , m_antigen_present(false)
    {
    }

    void MalariaAntibody::Initialize( MalariaAntibodyType::Enum type, int variant, float capacity, float concentration )
    {
        m_antibody_type          = type;
        m_antibody_variant       = variant;
        m_antibody_capacity      = capacity;
        m_antibody_concentration = concentration;
    }

    void MalariaAntibody::Decay( float dt, SusceptibilityMalariaConfig* params )
    {
        // don't do multiplication and subtraction unless antibody levels non-trivial
        if ( m_antibody_concentration > NON_TRIVIAL_ANTIBODY_THRESHOLD )
        {
            m_antibody_concentration -= m_antibody_concentration * TWENTY_DAY_DECAY_CONSTANT * dt;  //twenty day decay constant
        }

        // antibody capacity decays to a medium value (.3) dropping below .4 in ~120 days from 1.0
        if ( m_antibody_capacity > params->memory_level )
        {
            m_antibody_capacity -= ( m_antibody_capacity - params->memory_level) * params->hyperimmune_decay_rate * dt;
        }
    }

    void MalariaAntibodyCSP::Decay( float dt, SusceptibilityMalariaConfig* params )
    {
        // allow the decay of anti-CSP concentrations greater than unity (e.g. after boosting by vaccine)
        // TODO: this might become the default when boosting extends to other antibody types?
        if ( m_antibody_concentration > m_antibody_capacity )
        {
            m_antibody_concentration -= m_antibody_concentration * dt / params->antibody_csp_decay_days;
        }
        else
        {
            // otherwise do the normal behavior of decaying antibody concentration based on capacity
            MalariaAntibody::Decay( dt, params );
        }
    }

    float MalariaAntibody::StimulateCytokines( float dt, float inv_uL_blood )
    {
        // Cytokines released at low antibody concentration (if capacity hasn't switched into high proliferation rate yet)
        return ( 1 - m_antibody_concentration ) * float(m_antigen_count) * inv_uL_blood;
    }

    // Let's use the MSP version of antibody growth in the base class ...
    void MalariaAntibody::UpdateAntibodyCapacity( float dt, SusceptibilityMalariaConfig* params, float inv_uL_blood )
    {
        float growth_rate = params->MSP1_antibody_growthrate;
        float threshold   = params->antibody_stimulation_c50;

        m_antibody_capacity += growth_rate  * (1.0f - m_antibody_capacity) * (float)Sigmoid::basic_sigmoid( threshold, float(m_antigen_count) * inv_uL_blood);

        // rapid B cell proliferation above a threshold given stimulation
        if (m_antibody_capacity > B_CELL_PROLIFERATION_THRESHOLD)
        {
            m_antibody_capacity += ( 1.0f - m_antibody_capacity ) * B_CELL_PROLIFERATION_CONSTANT * dt;
        }

        if (m_antibody_capacity > 1.0)
        {
            m_antibody_capacity = 1.0;
        }
    }

    // Different arguments used by CSP update called directly from IndividualHumanMalaria::ExposeToInfectivity
    // and also in SusceptibilityMalaria::updateImmunityCSP
    void MalariaAntibody::UpdateAntibodyCapacity( float dt, float growth_rate )
    {
        m_antibody_capacity += growth_rate * dt * (1 - m_antibody_capacity);

        if (m_antibody_capacity > 1.0)
        {
            m_antibody_capacity = 1.0;
        }
    }

    // The minor PfEMP1 version is similar but not exactly the same...
    void MalariaAntibodyPfEMP1Minor::UpdateAntibodyCapacity( float dt, SusceptibilityMalariaConfig* params, float inv_uL_blood )
    {
        float min_stimulation = params->antibody_stimulation_c50 * params->minimum_adapted_response;
        float growth_rate     = params->antibody_capacity_growthrate * params->non_specific_growth;
        float threshold       = params->antibody_stimulation_c50;

        if (m_antibody_capacity <= B_CELL_PROLIFERATION_THRESHOLD)
        {
            m_antibody_capacity += growth_rate * dt * (1.0f - m_antibody_capacity) * (float)Sigmoid::basic_sigmoid(threshold, float(m_antigen_count) * inv_uL_blood + min_stimulation);
        }
        else
        {
            //rapid B cell proliferation above a threshold given stimulation
            m_antibody_capacity += (1.0f - m_antibody_capacity) * B_CELL_PROLIFERATION_CONSTANT * dt;
        }

        if (m_antibody_capacity > 1.0)
        {
            m_antibody_capacity = 1.0;
        }
    }

    // The major PfEMP1 version is slightly different again...
    void MalariaAntibodyPfEMP1Major::UpdateAntibodyCapacity( float dt, SusceptibilityMalariaConfig* params, float inv_uL_blood )
    {
        float min_stimulation = params->antibody_stimulation_c50 * params->minimum_adapted_response;
        float growth_rate     = params->antibody_capacity_growthrate;
        float threshold       = params->antibody_stimulation_c50;

        if (m_antibody_capacity <= B_CELL_PROLIFERATION_THRESHOLD)
        {
            //ability and number of B-cells to produce antibodies, with saturation
            m_antibody_capacity += growth_rate * dt * (1.0f - m_antibody_capacity) * (float)Sigmoid::basic_sigmoid(threshold, float(m_antigen_count) * inv_uL_blood + min_stimulation);

            // check for antibody capacity out of range
            if (m_antibody_capacity > 1.0)
            {
                m_antibody_capacity = 1.0;
            }
        }
        else
        {
            //rapid B cell proliferation above a threshold given stimulation
            m_antibody_capacity += (1.0f - m_antibody_capacity) * B_CELL_PROLIFERATION_CONSTANT * dt;
        }
    }

    void MalariaAntibody::UpdateAntibodyConcentration( float dt, SusceptibilityMalariaConfig* params )
    {
        // release of antibodies and effect of B cell proliferation on capacity
        // antibodies released after capacity passes 0.3
        // detection and proliferation in lymph nodes, etc...
        // and circulating memory cells
        if ( m_antibody_capacity > ANTIBODY_RELEASE_THRESHOLD )
        {
            m_antibody_concentration += ( m_antibody_capacity - m_antibody_concentration ) * ANTIBODY_RELEASE_FACTOR * dt;
        }

        if ( m_antibody_concentration > m_antibody_capacity )
        {
            m_antibody_concentration = m_antibody_capacity;
        }
    }

    void MalariaAntibodyCSP::UpdateAntibodyConcentration( float dt, SusceptibilityMalariaConfig* params )
    {
        // allow the decay of anti-CSP concentrations greater than unity (e.g. after boosting by vaccine)
        // TODO: this might become the default when boosting extends to other antibody types?
        if ( m_antibody_concentration > m_antibody_capacity )
        {
            m_antibody_concentration -= m_antibody_concentration * dt / params->antibody_csp_decay_days;
        }
        else
        {
            // otherwise do the normal behavior of incrementing antibody concentration based on capacity
            MalariaAntibody::UpdateAntibodyConcentration(dt, params);
        }
    }

    void MalariaAntibody::ResetCounters()
    {
        m_antigen_present = false;
        m_antigen_count   = 0;
    }

    void MalariaAntibody::IncreaseAntigenCount( int64_t antigenCount )
    {
        if( antigenCount > 0 )
        {
            m_antigen_count += antigenCount;
            m_antigen_present = true;
        }
    }

    void MalariaAntibody::SetAntigenicPresence( bool antigenPresent )
    {
        m_antigen_present = antigenPresent;
    }

    int64_t MalariaAntibody::GetAntigenCount() const
    {
        return m_antigen_count;
    }

    bool MalariaAntibody::GetAntigenicPresence() const
    {
        return m_antigen_present;
    }

    float MalariaAntibody::GetAntibodyCapacity() const
    {
        return m_antibody_capacity;
    }

    float MalariaAntibody::GetAntibodyConcentration() const
    {
        return m_antibody_concentration;
    }

    void MalariaAntibody::SetAntibodyCapacity( float antibody_capacity )
    {
        m_antibody_capacity = antibody_capacity;
    }

    void MalariaAntibody::SetAntibodyConcentration( float antibody_concentration )
    {
        m_antibody_concentration = antibody_concentration;
    }

    MalariaAntibodyType::Enum MalariaAntibody::GetAntibodyType() const
    {
        return m_antibody_type;
    }

    int MalariaAntibody::GetAntibodyVariant() const
    {
        return m_antibody_variant;
    }

    //------------------------------------------------------------------

    IMalariaAntibody* MalariaAntibodyCSP::CreateAntibody( int variant, float capacity )
    {
        MalariaAntibodyCSP * antibody = _new_ MalariaAntibodyCSP();
        antibody->Initialize( MalariaAntibodyType::CSP, variant, capacity );

        return antibody;
    }

    IMalariaAntibody* MalariaAntibodyMSP::CreateAntibody( int variant, float capacity )
    {
        MalariaAntibodyMSP * antibody = _new_ MalariaAntibodyMSP();
        antibody->Initialize( MalariaAntibodyType::MSP1, variant, capacity );

        return antibody;
    }

    IMalariaAntibody* MalariaAntibodyPfEMP1Minor::CreateAntibody( int variant, float capacity )
    {
        MalariaAntibodyPfEMP1Minor * antibody = _new_ MalariaAntibodyPfEMP1Minor();
        antibody->Initialize( MalariaAntibodyType::PfEMP1_minor, variant, capacity );

        return antibody;
    }

    IMalariaAntibody* MalariaAntibodyPfEMP1Major::CreateAntibody( int variant, float capacity )
    {
        MalariaAntibodyPfEMP1Major * antibody = _new_ MalariaAntibodyPfEMP1Major();
        antibody->Initialize( MalariaAntibodyType::PfEMP1_major, variant, capacity );

        return antibody;
    }

}

namespace Kernel {

#if USE_JSON_SERIALIZATION || USE_JSON_MPI
     void MalariaAntibody::JSerialize( IJsonObjectAdapter* root, JSerializer* helper ) const
     {
         root->BeginObject();
         root->Insert("m_antibody_capacity", m_antibody_capacity);
         root->Insert("m_antibody_concentration", m_antibody_concentration);
         root->Insert("m_antigen_count", m_antigen_count);
         root->Insert("m_antigen_present", m_antigen_present);
         root->Insert("m_antibody_type", (int) m_antibody_type);
         root->Insert("m_antibody_variant", m_antibody_variant);
         root->EndObject();
     }
        
     void MalariaAntibody::JDeserialize( IJsonObjectAdapter* root, JSerializer* helper )
     {
#if 0
        m_antibody_capacity = (float) helper->FindValue(root, "m_antibody_capacity").get_real();
        m_antibody_concentration = (float) helper->FindValue(root, "m_antibody_concentration").get_real();
        m_antigen_count = helper->FindValue(root, "m_antigen_count").get_int64();
        m_antigen_present = helper->FindValue(root, "m_antigen_present").get_bool();
        m_antibody_type = static_cast<MalariaAntibodyType::Enum>(helper->FindValue(root, "m_antibody_type").get_int());
        m_antibody_variant = helper->FindValue(root, "m_antibody_variant").get_int();
#endif
     }

     void MalariaAntibodyCSP::JSerialize( IJsonObjectAdapter* root, JSerializer* helper ) const
     {
        MalariaAntibody::JSerialize( root, helper );
     }
        
     void MalariaAntibodyCSP::JDeserialize( IJsonObjectAdapter* root, JSerializer* helper )
     {
        MalariaAntibody::JDeserialize( root, helper );
     }

     void MalariaAntibodyMSP::JSerialize( IJsonObjectAdapter* root, JSerializer* helper ) const
     {
        MalariaAntibody::JSerialize( root, helper );
     }
        
     void MalariaAntibodyMSP::JDeserialize( IJsonObjectAdapter* root, JSerializer* helper )
     {
        MalariaAntibody::JDeserialize( root, helper );
     }

     void MalariaAntibodyPfEMP1Minor::JSerialize( IJsonObjectAdapter* root, JSerializer* helper ) const
     {
        MalariaAntibody::JSerialize( root, helper );
     }
        
     void MalariaAntibodyPfEMP1Minor::JDeserialize( IJsonObjectAdapter* root, JSerializer* helper )
     {
        MalariaAntibody::JDeserialize( root, helper );
     }

     void MalariaAntibodyPfEMP1Major::JSerialize( IJsonObjectAdapter* root, JSerializer* helper ) const
     {
        MalariaAntibody::JSerialize( root, helper );
     }

     void MalariaAntibodyPfEMP1Major::JDeserialize( IJsonObjectAdapter* root, JSerializer* helper )
     {
        MalariaAntibody::JDeserialize( root, helper );
     }
#endif
}

#if USE_BOOST_SERIALIZATION || USE_BOOST_MPI
BOOST_CLASS_EXPORT(Kernel::MalariaAntibody)
BOOST_CLASS_EXPORT(Kernel::MalariaAntibodyCSP)
BOOST_CLASS_EXPORT(Kernel::MalariaAntibodyMSP)
BOOST_CLASS_EXPORT(Kernel::MalariaAntibodyPfEMP1Minor)
BOOST_CLASS_EXPORT(Kernel::MalariaAntibodyPfEMP1Major)
namespace Kernel
{
    // need to register derived types, so containers of abstract base class can serialize properly
    REGISTER_SERIALIZATION_VOID_CAST(MalariaAntibodyCSP, IMalariaAntibody)
    REGISTER_SERIALIZATION_VOID_CAST(MalariaAntibodyMSP, IMalariaAntibody)
    REGISTER_SERIALIZATION_VOID_CAST(MalariaAntibodyPfEMP1Minor, IMalariaAntibody)
    REGISTER_SERIALIZATION_VOID_CAST(MalariaAntibodyPfEMP1Major, IMalariaAntibody)

    template<class Archive>
    void serialize(Archive &ar, MalariaAntibody &ab, const unsigned int v)
    {
        static const char * _module = "MalariaAntibody";
        LOG_DEBUG("(De)serializing MalariaAntibody\n");
        ar & ab.m_antibody_capacity;
        ar & ab.m_antibody_concentration;
        ar & ab.m_antigen_count;
        ar & ab.m_antigen_present;
        ar & ab.m_antibody_type;
        ar & ab.m_antibody_variant;
    }

    template<class Archive>
    void serialize(Archive &ar, MalariaAntibodyCSP &ab, const unsigned int v)
    {
        static const char * _module = "MalariaAntibodyCSP";
        LOG_DEBUG("(De)serializing MalariaAntibodyCSP\n");
        ar & boost::serialization::base_object<MalariaAntibody>(ab);
    }

    template<class Archive>
    void serialize(Archive &ar, MalariaAntibodyMSP &ab, const unsigned int v)
    {
        static const char * _module = "MalariaAntibodyMSP";
        LOG_DEBUG("(De)serializing MalariaAntibodyMSP\n");
        ar & boost::serialization::base_object<MalariaAntibody>(ab);
    }

    template<class Archive>
    void serialize(Archive &ar, MalariaAntibodyPfEMP1Minor &ab, const unsigned int v)
    {
        static const char * _module = "MalariaAntibodyPfEMP1Minor";
        LOG_DEBUG("(De)serializing MalariaAntibodyPfEMP1Minor\n");
        ar & boost::serialization::base_object<MalariaAntibody>(ab);
    }

    template<class Archive>
    void serialize(Archive &ar, MalariaAntibodyPfEMP1Major &ab, const unsigned int v)
    {
        static const char * _module = "MalariaAntibodyPfEMP1Major";
        LOG_DEBUG("(De)serializing MalariaAntibodyPfEMP1Major\n");
        ar & boost::serialization::base_object<MalariaAntibody>(ab);
    }
}
#endif