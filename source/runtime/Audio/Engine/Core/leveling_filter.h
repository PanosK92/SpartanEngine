#ifndef ATG_ENGINE_SIM_LEVELING_FILTER_H
#define ATG_ENGINE_SIM_LEVELING_FILTER_H

#include "filter.h"

#include "function.h"

class LevelingFilter : public Filter {
    public:
        LevelingFilter();
        virtual ~LevelingFilter();

        virtual float f(float sample);
        float getAttenuation() const { return m_attenuation; }

        // peak hold and gain recovery in seconds, both need the sample rate to become coefficients
        void setTimeConstants(float holdSeconds, float releaseSeconds, float sampleRate);

    protected:
        float m_peak;
        float m_attenuation;

    public:
        float p_maxLevel;
        float p_minLevel;
        float p_target;
        // per sample peak decay, near one so the peak survives the gap between firing pulses
        float p_decay;
        // per sample gain recovery, only ever applied when the gain is climbing back up
        float p_release;
};

#endif /* ATG_ENGINE_SIM_LEVELING_FILTER_H */
