#include "pch.h"
#include "leveling_filter.h"

#include <cmath>

LevelingFilter::LevelingFilter() {
    m_peak = 30000.0f;
    m_attenuation = 1.0f;
    p_target = 30000.0f;
    p_minLevel = 0.0f;
    p_maxLevel = 1.0f;
    // defaults for 44.1 khz, setTimeConstants overrides them once the real rate is known
    p_decay = 0.99985f;
    p_release = 0.00028f;
}

LevelingFilter::~LevelingFilter() {
    /* void */
}

void LevelingFilter::setTimeConstants(float holdSeconds, float releaseSeconds, float sampleRate) {
    const float rate = sampleRate > 1.0f ? sampleRate : 1.0f;
    const float hold = holdSeconds > 0.0001f ? holdSeconds : 0.0001f;
    const float release = releaseSeconds > 0.0001f ? releaseSeconds : 0.0001f;
    p_decay = std::exp(-1.0f / (hold * rate));
    p_release = 1.0f - std::exp(-1.0f / (release * rate));
}

// a limiter, not a fader. the previous version smoothed the gain in both directions, so a transient
// was multiplied by the gain the quieter passage before it had set and clipped its own leading edge
float LevelingFilter::f(float sample) {
    const float magnitude = std::abs(sample);

    m_peak *= p_decay;
    if (magnitude > m_peak) {
        m_peak = magnitude;
    }

    if (m_peak <= 0.0f) {
        return 0.0f;
    }

    float target = p_target / m_peak;
    if (target < p_minLevel) {
        target = p_minLevel;
    }
    else if (target > p_maxLevel) {
        target = p_maxLevel;
    }

    // turning down happens on the sample that needs it, which is what bounds the output at p_target.
    // only the recovery is smoothed, and slowly, otherwise the gain pumps once per firing pulse
    if (target < m_attenuation) {
        m_attenuation = target;
    }
    else {
        m_attenuation += (target - m_attenuation) * p_release;
    }

    return sample * m_attenuation;
}
