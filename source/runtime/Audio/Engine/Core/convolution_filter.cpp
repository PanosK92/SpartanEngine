#include "convolution_filter.h"

#include <assert.h>
#include <immintrin.h>
#include <string.h>

namespace
{
    float dot_product(
        const float* left,
        const float* right,
        int count
    )
    {
        int i = 0;
        float result = 0.0f;

#if defined(__AVX2__) || defined(_M_AVX2)
        __m256 sum = _mm256_setzero_ps();
        for (; i + 8 <= count; i += 8)
        {
            const __m256 left_values =
                _mm256_loadu_ps(left + i);
            const __m256 right_values =
                _mm256_loadu_ps(right + i);
            sum = _mm256_add_ps(
                sum,
                _mm256_mul_ps(
                    left_values,
                    right_values
                )
            );
        }

        const __m128 sum_low =
            _mm256_castps256_ps128(sum);
        const __m128 sum_high =
            _mm256_extractf128_ps(sum, 1);
        __m128 sum_128 =
            _mm_add_ps(sum_low, sum_high);
        sum_128 = _mm_hadd_ps(sum_128, sum_128);
        sum_128 = _mm_hadd_ps(sum_128, sum_128);
        result = _mm_cvtss_f32(sum_128);
#endif

        for (; i < count; i++)
        {
            result +=
                left[i] *
                right[i];
        }

        return result;
    }
}

ConvolutionFilter::ConvolutionFilter() {
    m_shiftRegister = nullptr;
    m_impulseResponse = nullptr;

    m_shiftOffset = 0;
    m_sampleCount = 0;
}

ConvolutionFilter::~ConvolutionFilter() {
    assert(m_shiftRegister == nullptr);
    assert(m_impulseResponse == nullptr);
}

void ConvolutionFilter::initialize(int samples) {
    m_sampleCount = samples;
    m_shiftOffset = 0;
    m_shiftRegister = new float[samples];
    m_impulseResponse = new float[samples];

    memset(m_shiftRegister, 0, sizeof(float) * samples);
    memset(m_impulseResponse, 0, sizeof(float) * samples);
}

void ConvolutionFilter::destroy() {
    delete[] m_shiftRegister;
    delete[] m_impulseResponse;

    m_shiftRegister = nullptr;
    m_impulseResponse = nullptr;
}

float ConvolutionFilter::f(float sample) {
    m_shiftRegister[m_shiftOffset] = sample;

    const int first_count =
        m_sampleCount -
        m_shiftOffset;
    const float result =
        dot_product(
            m_impulseResponse,
            m_shiftRegister + m_shiftOffset,
            first_count
        ) +
        dot_product(
            m_impulseResponse + first_count,
            m_shiftRegister,
            m_shiftOffset
        );

    m_shiftOffset = (m_shiftOffset - 1 + m_sampleCount) % m_sampleCount;

    return result;
}
